/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "common.h"
#include <tubus/utils.h>
#include <tubus/buffer.h>
#include <tubus/channel.h>
#include <future>
#include <functional>
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

template<class channel> class tubus_wrapper
{
    typename channel::endpoint m_bind;
    typename channel::endpoint m_peer;
    uint64_t m_secret;
    std::shared_ptr<channel> m_channel;

public:

    tubus_wrapper(const typename channel::endpoint& b, const typename channel::endpoint& p, uint64_t s)
        : m_bind(b)
        , m_peer(p)
        , m_secret(s)
    {
    }

    ~tubus_wrapper()
    {
        if (m_channel)
            m_channel->close();
    }

    void open()
    {
        m_channel = channel::create(g_reactor.io, m_secret);
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

class udp_router : public std::enable_shared_from_this<udp_router>
{
    boost::asio::ip::udp::endpoint m_le;
    boost::asio::ip::udp::endpoint m_re;
    boost::asio::ip::udp::socket m_bs;
    boost::asio::ip::udp::endpoint m_ep;
    tubus::mutable_buffer m_rb;

    void receive()
    {
        std::weak_ptr<udp_router> weak = shared_from_this();
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

    udp_router(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& l, const boost::asio::ip::udp::endpoint& r)
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

template<class channel> void make_core_test(size_t receive_buffer_size)
{
    typename channel::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3001);
    typename channel::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3002);

    tubus_wrapper<channel> left(le, re, 1234567890);
    tubus_wrapper<channel> right(re, le, 1234567890);

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

    BOOST_CHECK_EQUAL(left.writable(), receive_buffer_size);
    BOOST_CHECK_EQUAL(right.writable(), receive_buffer_size);

    for(size_t i = 0; i < sizeof(data); ++i)
    {
        BOOST_REQUIRE_NO_THROW(left.async_write(lb.slice(i, 1)).get());
        BOOST_REQUIRE_NO_THROW(right.async_write(rb.slice(i, 1)).get());
    }

    BOOST_CHECK_EQUAL(left.readable(), sizeof(data));
    BOOST_CHECK_EQUAL(right.readable(), sizeof(data));

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

template<class channel> void make_speed_test()
{
    typename channel::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3021);
    typename channel::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3022);

    boost::asio::io_context io;
    auto left = channel::create(g_reactor.io, 0);
    auto right = channel::create(g_reactor.io, 0);

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

template<class channel> void make_order_test()
{
    typename channel::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3021);
    typename channel::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3022);

    boost::asio::io_context io;
    auto left = channel::create(g_reactor.io, 1234567890);
    auto right = channel::create(g_reactor.io, 1234567890);

    left->open(le);
    right->open(re);

    tubus::const_buffer wb1("first");
    tubus::const_buffer wb2("second");
    tubus::const_buffer wb3("third");

    std::promise<void> wp;
    auto write_count = [&, counter = 0]() mutable
    {
        if (++counter == 3)
            wp.set_value();
        return counter;
    };

    left->accept(re, [&](const boost::system::error_code& err)
    {
        BOOST_REQUIRE_EQUAL(err, boost::system::error_code());

        left->write(wb1, [&](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(size, wb1.size());
            BOOST_CHECK_EQUAL(write_count(), 1);
        });

        left->write(wb2, [&](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(size, wb2.size());
            BOOST_CHECK_EQUAL(write_count(), 2);
        });

        left->write(wb3, [&](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(size, wb3.size());
            BOOST_CHECK_EQUAL(write_count(), 3);
        });
    });

    std::promise<void> rp;
    auto read_count = [&, counter = 0]() mutable
    {
        if (++counter == 3)
            rp.set_value();
        return counter;
    };

    right->connect(le, [&](const boost::system::error_code& err)
    {
        BOOST_REQUIRE_EQUAL(err, boost::system::error_code());

        tubus::mutable_buffer rb1(wb1.size());
        right->read(rb1, [&, rb1](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(rb1.size() == size && std::memcmp(rb1.data(), wb1.data(), rb1.size()) == 0, true);
            BOOST_CHECK_EQUAL(read_count(), 1);
        });

        tubus::mutable_buffer rb2(wb2.size());
        right->read(rb2, [&, rb2](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(rb2.size() == size && std::memcmp(rb2.data(), wb2.data(), rb2.size()) == 0, true);
            BOOST_CHECK_EQUAL(read_count(), 2);
        });

        tubus::mutable_buffer rb3(wb3.size());
        right->read(rb3, [&, rb3](const boost::system::error_code& err, size_t size)
        {
            BOOST_CHECK_EQUAL(err, boost::system::error_code());
            BOOST_CHECK_EQUAL(rb3.size() == size && std::memcmp(rb3.data(), wb3.data(), rb3.size()) == 0, true);
            BOOST_CHECK_EQUAL(read_count(), 3);
        });
    });

    BOOST_REQUIRE_NO_THROW(wp.get_future().get());
    BOOST_REQUIRE_NO_THROW(rp.get_future().get());

    BOOST_REQUIRE_NO_THROW(left->close());
    BOOST_REQUIRE_NO_THROW(right->close());
}

BOOST_AUTO_TEST_SUITE(udp_channel);

BOOST_AUTO_TEST_CASE(core)
{
    make_core_test<tubus::udp_channel>(tubus::udp::qos::receive_buffer_size());
}

BOOST_AUTO_TEST_CASE(connectivity)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3002);

    tubus_wrapper<tubus::udp_channel> left(le, re, 1234567890);
    BOOST_REQUIRE_NO_THROW(left.open());

    tubus_wrapper<tubus::udp_channel> right(re, le, 1234567890);
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
    boost::asio::ip::udp::endpoint pe(boost::asio::ip::make_address("127.0.0.1"), 3000);
    boost::asio::ip::udp::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3002);

    auto router = std::make_shared<udp_router>(pe, le, re);
    router->start();

    tubus_wrapper<tubus::udp_channel> left(le, pe, 1234567890);
    tubus_wrapper<tubus::udp_channel> right(re, pe, 1234567890);

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

    BOOST_CHECK_EQUAL(left.writable(), tubus::udp::qos::receive_buffer_size() - source.read());
    BOOST_CHECK_EQUAL(right.readable(), source.read());

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
    boost::asio::ip::udp::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3002);

    tubus_wrapper<tubus::udp_channel> left(le, re, 1234567890);
    tubus_wrapper<tubus::udp_channel> right(re, le, 2143658709);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    tubus::mutable_buffer buffer(1024 * 1024);

    // wrong read/write
    BOOST_REQUIRE_THROW(left.async_write(buffer).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_read(buffer).get(), boost::system::system_error);

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(BOOST_CHECK_EQUAL((int)la.wait_for(std::chrono::seconds(1)), (int)std::future_status::timeout));
    BOOST_REQUIRE_NO_THROW(BOOST_CHECK_EQUAL((int)rc.wait_for(std::chrono::seconds(1)), (int)std::future_status::timeout));

    // send buffer overflow
    BOOST_REQUIRE_THROW(left.async_write(tubus::mutable_buffer(1024 * 1024 * 6)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());
}

BOOST_AUTO_TEST_CASE(order)
{
    make_order_test<tubus::udp_channel>();
}

BOOST_AUTO_TEST_CASE(speed)
{
    make_speed_test<tubus::udp_channel>();
}

BOOST_AUTO_TEST_SUITE_END();

BOOST_AUTO_TEST_SUITE(tcp_channel);
    
BOOST_AUTO_TEST_CASE(core)
{
    make_core_test<tubus::tcp_channel>(tubus::tcp::qos::receive_buffer_size());
}

BOOST_AUTO_TEST_CASE(fall)
{
    boost::asio::ip::tcp::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3011);
    boost::asio::ip::tcp::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3012);

    tubus_wrapper<tubus::tcp_channel> left(le, re, 1234567890);
    BOOST_REQUIRE_NO_THROW(left.open());

    tubus_wrapper<tubus::tcp_channel> right(re, le, 2143658709);
    BOOST_REQUIRE_NO_THROW(right.open());

    auto rc = right.async_connect();
    auto la = left.async_accept();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    auto rw = right.async_write(tubus::mutable_buffer(1));
    auto lr = left.async_read(tubus::mutable_buffer(1));

    BOOST_REQUIRE_NO_THROW(rw.get());
    BOOST_REQUIRE_THROW(lr.get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());
    BOOST_REQUIRE_NO_THROW(left.close());

    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
    BOOST_REQUIRE_NO_THROW(right.close());

    BOOST_REQUIRE_THROW(left.async_shutdown().get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_shutdown().get(), boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(integrity)
{
    boost::asio::ip::tcp::endpoint le(boost::asio::ip::make_address("127.0.0.1"), 3001);
    boost::asio::ip::tcp::endpoint re(boost::asio::ip::make_address("127.0.0.1"), 3002);

    tubus_wrapper<tubus::tcp_channel> left(le, re, 1234567890UL);
    tubus_wrapper<tubus::tcp_channel> right(re, le, 1234567890UL);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    stream_source source;
    stream_sink sink;
    tubus::mutable_buffer buffer(stream_source::chunk_size);

    auto wf = left.async_write(source.read_next());
    auto rf = right.async_read(buffer);

    BOOST_REQUIRE_NO_THROW(wf.get());
    BOOST_REQUIRE_NO_THROW(rf.get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));

    wf = left.async_write(source.read_next());
    rf = right.async_read(buffer);
    
    BOOST_REQUIRE_NO_THROW(wf.get());
    BOOST_REQUIRE_NO_THROW(rf.get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));

    wf = left.async_write(source.read_next());
    rf = right.async_read(buffer);
    
    BOOST_REQUIRE_NO_THROW(wf.get());
    BOOST_REQUIRE_NO_THROW(rf.get());
    BOOST_REQUIRE_NO_THROW(sink.write_next(buffer));

    BOOST_CHECK_EQUAL(source.read(), sink.written());

    auto rr = right.async_read(buffer.slice(0, 1));
    BOOST_CHECK_EQUAL((int)rr.wait_for(std::chrono::seconds(1)), (int)std::future_status::timeout);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(order)
{
    make_order_test<tubus::tcp_channel>();
}

BOOST_AUTO_TEST_CASE(speed)
{
    make_speed_test<tubus::tcp_channel>();
}

BOOST_AUTO_TEST_SUITE_END();
