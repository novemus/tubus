/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include "tubus.h"
#include <future>

#define exec_async_method(object, method, ec) [obj = object, &ec]() \
{ \
    if (obj) \
    { \
        std::promise<boost::system::error_code> promise; \
        std::future<boost::system::error_code> future = promise.get_future(); \
        obj->method([&promise](const boost::system::error_code& error) \
        { \
            promise.set_value(error); \
        }); \
        ec = future.get(); \
        return; \
    } \
    ec = boost::asio::error::broken_pipe; \
}() \

#define exec_async_io_method(object, method, buffer, ec) [obj = object, buf = buffer, &ec]() \
{ \
    if (obj) \
    { \
        std::promise<std::pair<boost::system::error_code, size_t>> promise; \
        std::future<std::pair<boost::system::error_code, size_t>> future = promise.get_future(); \
        obj->method(buf, [&promise](const boost::system::error_code& error, size_t size) \
        { \
            promise.set_value(std::make_pair(error, size)); \
        }); \
        auto res = future.get(); \
        ec = res.first; \
        return res.second; \
    } \
    ec = boost::asio::error::broken_pipe; \
    return 0ul; \
}() \

namespace tubus {

typedef boost::asio::ip::udp::endpoint endpoint;

class socket
{
    boost::asio::io_context& m_io;
    channel_ptr m_channel;

public:

    typedef socket lowest_layer_type;
    typedef boost::asio::io_context::executor_type executor_type;

    socket(boost::asio::io_context& io) 
        : m_io(io)
    {
    }

    void open(const endpoint& le, const endpoint& re, uint64_t secret) noexcept(false)
    {
        if (!m_channel)
        {
            m_channel = create_channel(m_io, le, re, secret);
            m_channel->open();
            return;
        }

        boost::asio::detail::throw_error(boost::asio::error::operation_not_supported, "open");
    }

    void open(const endpoint& le, const endpoint& re, uint64_t secret, boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            open(le, re, secret);
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    void close() noexcept(true)
    {
        if (m_channel)
            m_channel->close();

        m_channel.reset();
    }

    void connect() noexcept(false)
    {
        boost::system::error_code ec;
        exec_async_method(m_channel, connect, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "connect");
    }

    void connect(boost::system::error_code& ec) noexcept(true)
    {
        exec_async_method(m_channel, connect, ec);
    }

    void async_connect(const callback& handler) noexcept(true)
    {
        if (m_channel)
        {
            m_channel->connect(handler);
            return;
        }

        m_io.post(std::bind(handler, boost::asio::error::broken_pipe));
    }

    void accept() noexcept(false)
    {
        boost::system::error_code ec;
        exec_async_method(m_channel, accept, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "accept");
    }

    void accept(boost::system::error_code& ec) noexcept(true)
    {
        exec_async_method(m_channel, accept, ec);
    }

    void async_accept(const callback& handler) noexcept(true)
    {
        if (m_channel)
        {
            m_channel->accept(handler);
            return;
        }

        m_io.post(std::bind(handler, boost::asio::error::broken_pipe));
    }

    void shutdown() noexcept(false)
    {
        boost::system::error_code ec;
        exec_async_method(m_channel, shutdown, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "shutdown");
    }

    void shutdown(boost::system::error_code& ec) noexcept(true)
    {
        exec_async_method(m_channel, shutdown, ec);
    }

    void async_shutdown(const callback& handler) noexcept(true)
    {
        if (!m_channel)
        {
            m_io.post(std::bind(handler, boost::system::error_code()));
            return;
        }

        m_channel->shutdown(handler);
    }

    socket& lowest_layer() noexcept(true)
    {
        return *this;
    }

    // SyncReadStream, SyncWriteStream, AsyncReadStream and AsyncWriteStream concepts

    template<class mutable_buffer_type>
    size_t read_some(const mutable_buffer_type& mb) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = exec_async_io_method(m_channel, read, wrap_mutable_buffer(mb), ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "read_some");
        
        return size;
    }

    template<class mutable_buffer_type>
    size_t read_some(const mutable_buffer_type& mb, boost::system::error_code& ec) noexcept(true)
    {
        return exec_async_io_method(m_channel, read, wrap_mutable_buffer(mb), ec);
    }

    template<class const_buffer_type>
    size_t write_some(const const_buffer_type& cb) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = exec_async_io_method(m_channel, write, wrap_const_buffer(cb), ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "write_some");

        return size;
    }

    template<class const_buffer_type>
    size_t write_some(const const_buffer_type& cb, boost::system::error_code& ec) noexcept(true)
    {
        return exec_async_io_method(m_channel, write, wrap_const_buffer(cb), ec);
    }

    template<class mutable_buffer_type>
    void async_read_some(const mutable_buffer_type& mb, const io_callback& handler) noexcept(true)
    {
        if (!m_channel)
        {
            m_io.post(std::bind(handler, boost::asio::error::broken_pipe, 0));
            return;
        }

        m_channel->read(wrap_mutable_buffer(mb), handler);
    }

    template<class const_buffer_type>
    void async_write_some(const const_buffer_type& cb, const io_callback& handler) noexcept(true)
    {
        if (!m_channel)
        {
            m_io.post(std::bind(handler, boost::asio::error::broken_pipe, 0));
            return;
        }

        m_channel->write(wrap_const_buffer(cb), handler);
    }

    executor_type get_executor() noexcept(true)
    {
        return m_io.get_executor();
    }
};

}
