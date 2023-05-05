/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../buffer.h"
#include "../channel.h"
#include "common.h"
#include <future>
#include <functional>
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define ASYNC_IO(object, method, buffer, filter) \
return std::async([obj = object, buffer]() \
{ \
    std::promise<void> promise; \
    std::future<void> future = promise.get_future(); \
    obj->method(buffer, [&promise](const boost::system::error_code& error, size_t size) \
    { \
        if (filter) \
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
        else \
            promise.set_value(); \
    }); \
    future.get(); \
}) \

#define ASYNC(object, method, filter, ...) \
return std::async([=, obj = object]() \
{ \
    std::promise<void> promise; \
    std::future<void> future = promise.get_future(); \
    obj->method(__VA_ARGS__[&promise](const boost::system::error_code& error) \
    { \
        if (filter) \
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
        else \
            promise.set_value(); \
    }); \
    future.get(); \
}) \

executor g_reactor;

class tubus_wrapper
{
    boost::asio::ip::udp::endpoint m_bind;
    boost::asio::ip::udp::endpoint m_peer;
    uint64_t m_secret;
    std::shared_ptr<tubus::channel> m_channel;

public:

    tubus_wrapper(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& p, uint64_t s)
        : m_bind(b)
        , m_peer(p)
        , m_secret(s)
    {
    }

    ~tubus_wrapper()
    {
        m_channel->close();
    }

    void open()
    {
        m_channel = tubus::create_channel(g_reactor.io, m_secret);
        m_channel->open(m_bind);
    }

    void close()
    {
        m_channel->close();
    }

    uint64_t readable() const
    {
        return m_channel->readable();
    }
        
    uint64_t writable() const
    {
        return m_channel->writable();
    }

    std::future<void> async_accept()
    {
        ASYNC(m_channel, accept, error, m_peer, );
    }

    std::future<void> async_connect()
    {
        ASYNC(m_channel, connect, error, m_peer, );
    }

    std::future<void> async_shutdown()
    {
        ASYNC(m_channel, shutdown, error && error != boost::asio::error::interrupted && error != boost::asio::error::connection_refused);
    }

    std::future<void> async_write(const tubus::const_buffer& buffer)
    {
        ASYNC_IO(m_channel, write, buffer, error);
    }

    std::future<void> async_read(const tubus::mutable_buffer& buffer)
    {
        ASYNC_IO(m_channel, read, buffer, error);
    }
};

class mediator : public std::enable_shared_from_this<mediator>
{
    boost::asio::ip::udp::endpoint m_le;
    boost::asio::ip::udp::endpoint m_re;
    boost::asio::ip::udp::socket m_bs;
    boost::asio::ip::udp::endpoint m_ep;
    tubus::mutable_buffer m_rb;

    void receive()
    {
        std::weak_ptr<mediator> weak = shared_from_this();
        m_bs.async_receive_from(m_rb, m_ep, [weak](const boost::system::error_code& e, size_t s)
        {
            auto ptr = weak.lock();
            if (ptr)
                ptr->on_received(e, s);
        });
    }

    void on_received(const boost::system::error_code& e, size_t s)
    {
        if(e)
        {
            if (e != boost::asio::error::operation_aborted)
                BOOST_TEST_MESSAGE("mediator: " << e.message());
            return;
        }

        if (std::rand() % 3)
        {
            boost::system::error_code ec;
            m_bs.send_to(m_rb.slice(0, s), m_ep == m_le ? m_re : m_le, 0, ec);

            if (ec)
            {
                if (e != boost::asio::error::operation_aborted)
                    BOOST_TEST_MESSAGE("mediator: " << e.message());
                return;
            }
        }

        receive();
    }

public:

    mediator(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& l, const boost::asio::ip::udp::endpoint& r)
        : m_le(l)
        , m_re(r)
        , m_bs(g_reactor.io, b.protocol())
        , m_rb(65507)
    {
        m_bs.set_option(boost::asio::socket_base::reuse_address(true));
        m_bs.bind(b);
    }

    void start()
    {
        receive();
    }
};

BOOST_AUTO_TEST_SUITE(tubus_channel);

BOOST_AUTO_TEST_CASE(core)
{
    const uint64_t default_receive_buffer = 5ull * 1024 * 1024;

    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_wrapper left(le, re, 1234567890);
    tubus_wrapper right(re, le, 1234567890);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    uint8_t data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    tubus::mutable_buffer lb(sizeof(data));
    tubus::mutable_buffer rb(sizeof(data));

    std::memcpy(lb.data(), data, lb.size());
    std::memcpy(rb.data(), data, rb.size());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    BOOST_CHECK_EQUAL(left.readable(), 0);
    BOOST_CHECK_EQUAL(right.readable(), 0);

    BOOST_CHECK_EQUAL(left.writable(), default_receive_buffer);
    BOOST_CHECK_EQUAL(right.writable(), default_receive_buffer);

    for(size_t i = 0; i < sizeof(data); ++i)
    {
        BOOST_REQUIRE_NO_THROW(left.async_write(lb.slice(i, 1)).get());
        BOOST_REQUIRE_NO_THROW(right.async_write(rb.slice(i, 1)).get());
    }

    BOOST_CHECK_EQUAL(left.readable(), sizeof(data));
    BOOST_CHECK_EQUAL(right.readable(), sizeof(data));

    BOOST_CHECK_EQUAL(left.writable(), default_receive_buffer - sizeof(data));
    BOOST_CHECK_EQUAL(right.writable(), default_receive_buffer - sizeof(data));

    std::memset(lb.data(), 0, lb.size());
    std::memset(rb.data(), 0, rb.size());

    BOOST_REQUIRE_NO_THROW(left.async_read(lb).get());
    BOOST_CHECK_EQUAL(std::memcmp(lb.data(), data, lb.size()), 0);

    BOOST_REQUIRE_NO_THROW(right.async_read(rb).get());
    BOOST_CHECK_EQUAL(std::memcmp(rb.data(), data, rb.size()), 0);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(connectivity)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_wrapper left(le, re, 1234567890);
    BOOST_REQUIRE_NO_THROW(left.open());

    tubus_wrapper right(re, le, 1234567890);
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    BOOST_CHECK_EQUAL((int)la.wait_for(std::chrono::seconds(3)), (int)std::future_status::timeout);
    BOOST_REQUIRE_THROW(left.async_shutdown().get(), boost::system::system_error);
    BOOST_REQUIRE_NO_THROW(left.close());

    auto rc = right.async_connect();
    BOOST_REQUIRE_THROW(rc.get(), boost::system::system_error);
    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
    BOOST_REQUIRE_NO_THROW(right.close());

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto a = left.async_accept();
    auto c = right.async_connect();

    BOOST_REQUIRE_NO_THROW(a.get());
    BOOST_REQUIRE_NO_THROW(c.get());

    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());

    BOOST_REQUIRE_THROW(left.async_read(tubus::mutable_buffer(1)).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_write(tubus::mutable_buffer(1)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
}

BOOST_AUTO_TEST_CASE(integrity)
{
    boost::asio::ip::udp::endpoint pe(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto proxy = std::make_shared<mediator>(pe, le, re);
    proxy->start();

    tubus_wrapper left(le, pe, 0);
    tubus_wrapper right(re, pe, 0);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    stream_source source;
    stream_sink sink;

    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_next()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_next()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_next()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_next()).get());

    tubus::mutable_buffer buffer(stream_source::chunk_size);

    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));

    BOOST_CHECK_EQUAL(source.read(), sink.written());

    auto rr = right.async_read(buffer.slice(0, 1));
    BOOST_CHECK_EQUAL((int)rr.wait_for(std::chrono::seconds(3)), (int)std::future_status::timeout);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(fall)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_wrapper left(le, re, 0);
    tubus_wrapper right(re, le, 0);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    tubus::mutable_buffer buffer(1024 * 1024);

    // wrong read/write
    BOOST_REQUIRE_THROW(left.async_write(buffer).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_read(buffer).get(), boost::system::system_error);

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    // send buffer overflow
    BOOST_REQUIRE_THROW(left.async_write(tubus::mutable_buffer(1024 * 1024 * 6)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());
}

BOOST_AUTO_TEST_CASE(speed)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    boost::asio::io_context io;
    auto left = tubus::create_channel(g_reactor.io, 0);
    auto right = tubus::create_channel(g_reactor.io, 0);

    const size_t MB = 1024 * 1024;
    const size_t TRAFFIC = 1024 * MB;

    tubus::mutable_buffer wb(MB);
    tubus::mutable_buffer rb(MB);

    size_t written = 0;

    std::promise<void> wp;
    std::future<void> wf = wp.get_future();

    tubus::io_callback on_write = [&](const boost::system::error_code& err, size_t size)
    {
        if (err)
        {
            wp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        written += size;

        if (written < TRAFFIC)
        {
            auto rest = TRAFFIC - written;
            left->write(wb.size() > rest ? wb.slice(0, rest) : wb, on_write);
        }
        else
        {
            wp.set_value();
        }
    };

    tubus::callback on_connect = [&](const boost::system::error_code& err)
    {
        if (err)
        {
            wp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        left->write(wb, on_write);
    };

    size_t read = 0;

    std::promise<void> rp;
    std::future<void> rf = rp.get_future();

    tubus::io_callback on_read = [&](const boost::system::error_code& err, size_t size)
    {
        if (err)
        {
            rp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        read += size;

        if (read < TRAFFIC)
        {
            auto rest = TRAFFIC - read;
            right->read(rb.size() > rest ? rb.slice(0, rest) : rb, on_read);
        }
        else
        {
            rp.set_value();
        }
    };

    tubus::callback on_accept = [&](const boost::system::error_code& err)
    {
        if (err)
        {
            rp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        right->read(rb, on_read);
    };

    right->open(re);
    left->open(le);

    right->accept(le, on_accept);
    left->connect(re, on_connect);

    auto begin = boost::posix_time::microsec_clock::local_time();

    BOOST_REQUIRE_NO_THROW(wf.get());
    BOOST_REQUIRE_NO_THROW(rf.get());

    auto time = boost::posix_time::microsec_clock::local_time() - begin;

    BOOST_TEST_MESSAGE(
        "traffic: " << float(TRAFFIC) / MB  << " MB\ntime: " << time  << "\nspeed: " << float(TRAFFIC) / time.total_milliseconds() * 1000 / MB * 8 << " Mb/s"
        );

    right->close();
    left->close();
}

BOOST_AUTO_TEST_SUITE_END();
