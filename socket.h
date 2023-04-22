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
#include <type_traits>

namespace tubus { namespace detail {

template<class async_method, class handler>
void post_method(channel_ptr channel, async_method method, boost::asio::io_context& io, handler&& callback) noexcept(true)
{
    if (!channel)
    {
        io.post(std::bind(callback, boost::asio::error::broken_pipe));
        return;
    }

    (channel.get()->*method)(callback);
}

template<class async_method>
void exec_method(channel_ptr channel, async_method method, boost::system::error_code& ec) noexcept(true)
{ 
    if (channel) 
    {
        std::promise<boost::system::error_code> promise; 
        std::future<boost::system::error_code> future = promise.get_future(); 
        (channel.get()->*method)([&promise](const boost::system::error_code& error) 
        { 
            promise.set_value(error); 
        }); 
        ec = future.get(); 
        return; 
    } 
    ec = boost::asio::error::broken_pipe; 
}

template<class io_method, class limit_method, class data_buffer, class handler>
void post_io_method(channel_ptr channel, io_method method, limit_method limiter, const data_buffer& buffer, boost::asio::io_context& io, handler&& callback, size_t result = 0) noexcept(true)
{
    if (!channel)
    {
        io.post(std::bind(callback, boost::asio::error::broken_pipe, result));
        return;
    }

    if (buffer.size() == 0)
    {
        io.post(std::bind(callback, boost::system::error_code(), result));
        return;
    }

    auto part = tubus::buffer(buffer.data(), std::min((channel.get()->*limiter)(), buffer.size()));
    if (part.size() > 0 || result == 0)
    {
        (channel.get()->*method)(part, [channel, method, limiter, buffer, &io, callback, result](const boost::system::error_code& error, size_t size)
        {
            if (error)
            {
                io.post(std::bind(callback, error, result + size));
                return;
            }

            auto next = buffer;
            next += size;

            post_io_method(channel, method, limiter, next, io, callback, result + size);
        });
    }
    else
    {
        io.post(std::bind(callback, boost::system::error_code(), result));
    }
}

template<class io_method, class limit_method, class buffer_iterator, class handler>
void post_io_method(channel_ptr channel, io_method method, limit_method limiter, buffer_iterator begin, buffer_iterator end, boost::asio::io_context& io, handler&& callback, size_t result = 0) noexcept(true)
{
    if (!channel)
    {
        io.post(std::bind(callback, boost::asio::error::broken_pipe, result));
        return;
    }

    if (begin == end)
    {
        io.post(std::bind(callback, boost::system::error_code(), result));
        return;
    }

    post_io_method(channel, method, limiter, *begin, io, [channel, method, limiter, begin, end, &io, callback, result](const boost::system::error_code& error, size_t size)
    {
        if (error)
        {
            io.post(std::bind(callback, error, result + size));
            return;
        }

        post_io_method(channel, method, limiter, std::next(begin, 1), end, io, callback, result + size);
    }, result);
}

template<class io_method, class limit_method, class buffer_iterator>
size_t exec_io_method(channel_ptr channel, io_method method, limit_method limiter, buffer_iterator begin, buffer_iterator end, boost::asio::io_context& io, boost::system::error_code& ec) noexcept(true)
{
    if (!channel)
    {
        ec = boost::asio::error::broken_pipe;
        return 0;
    }

    std::promise<size_t> promise;
    std::future<size_t> future = promise.get_future();

    post_io_method(channel, method, limiter, begin, end, io, [&](const boost::system::error_code& error, size_t size)
    {
        ec = error;
        promise.set_value(size);
    }, 0);

    return future.get();
}

}

typedef boost::asio::ip::udp::endpoint endpoint;

class socket
{
    struct context
    {
        boost::asio::io_context& asio;
        endpoint local;
        endpoint remote;
        channel_ptr channel;

        context(boost::asio::io_context& io) : asio(io)
        {
        }

        context(context&& other) 
            : asio(other.asio)
            , local(other.local)
            , remote(other.remote)
            , channel(other.channel)
        {
            other.local = endpoint();
            other.remote = endpoint();
            other.channel.reset();
        }

        context(const context&) = delete;
        context& operator=(const context&) = delete;
    };

    std::unique_ptr<context> m_ctx;

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

public:

    typedef socket lowest_layer_type;
    typedef boost::asio::io_context::executor_type executor_type;

    socket(boost::asio::io_context& io) noexcept(true) 
        : m_ctx(new context(io))
    {
    }

    socket(socket&& other) noexcept(true)
        : m_ctx(new context(std::move(*(other.m_ctx))))
    {
    }

    socket& operator=(socket&& other) noexcept(true)
    {
        m_ctx = std::make_unique<context>(std::move(*(other.m_ctx)));
        return *this;
    }

    void open(const endpoint& le, const endpoint& re, uint64_t secret) noexcept(false)
    {
        if (m_ctx->channel == 0)
        {
            m_ctx->local = le;
            m_ctx->remote = re;
            m_ctx->channel = create_channel(m_ctx->asio, le, re, secret);
            m_ctx->channel->open();
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
        if (m_ctx->channel)
        {
            m_ctx->channel->close();
            m_ctx->channel.reset();
        }
    }

    void connect() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_ctx->channel, &channel::connect, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "connect");
    }

    void connect(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_ctx->channel, &channel::connect, ec);
    }

    template<class connect_handler>
    void async_connect(connect_handler&& callback) noexcept(true)
    {
        detail::post_method(m_ctx->channel, &channel::connect, m_ctx->asio, callback);
    }

    void accept() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_ctx->channel, &channel::accept, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "accept");
    }

    void accept(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_ctx->channel, &channel::accept, ec);
    }

    template<class accept_handler>
    void async_accept(accept_handler&& callback) noexcept(true)
    {
        detail::post_method(m_ctx->channel, &channel::accept, m_ctx->asio, callback);
    }

    void shutdown() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_ctx->channel, &channel::shutdown, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "shutdown");
    }

    void shutdown(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_ctx->channel, &channel::shutdown, ec);
    }

    template<class shutdown_handler>
    void async_shutdown(shutdown_handler&& callback) noexcept(true)
    {
        detail::post_method(m_ctx->channel, &channel::shutdown, m_ctx->asio, callback);
    }

    socket& lowest_layer() noexcept(true)
    {
        return *this;
    }

    template<class mutable_buffers>
    size_t read_some(const mutable_buffers& buffer) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = detail::exec_io_method(m_ctx->channel, &channel::read, &channel::readable, buffer.begin(), buffer.end(), m_ctx->asio, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "read_some");
        
        return size;
    }

    template<class mutable_buffers>
    size_t read_some(const mutable_buffers& buffer, boost::system::error_code& ec) noexcept(true)
    {
        return detail::exec_io_method(m_ctx->channel, &channel::read, &channel::readable, buffer.begin(), buffer.end(), m_ctx->asio, ec);
    }

    template<class const_buffers>
    size_t write_some(const const_buffers& buffer) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = detail::exec_io_method(m_ctx->channel, &channel::write, &channel::writable, buffer.begin(), buffer.end(), m_ctx->asio, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "write_some");

        return size;
    }

    template<class const_buffers>
    size_t write_some(const const_buffers& buffer, boost::system::error_code& ec) noexcept(true)
    {
        return detail::exec_io_method(m_ctx->channel, &channel::write, &channel::writable, buffer.begin(), buffer.end(), m_ctx->asio, ec);
    }

    template<class mutable_buffers, class read_handler>
    void async_read_some(const mutable_buffers& buffer, read_handler&& callback) noexcept(true)
    {
        detail::post_io_method(m_ctx->channel, &channel::read, &channel::readable, buffer.begin(), buffer.end(), m_ctx->asio, callback);
    }

    template<class const_buffers, class write_handler>
    void async_write_some(const const_buffers& buffer, write_handler&& callback) noexcept(true)
    {
        detail::post_io_method(m_ctx->channel, &channel::write, &channel::writable, buffer.begin(), buffer.end(), m_ctx->asio, callback);
    }

    executor_type get_executor() const noexcept(true)
    {
        return m_ctx->asio.get_executor();
    }

    endpoint local_endpoint() const noexcept(true)
    {
        return m_ctx->local;
    }

    endpoint remote_endpoint() const noexcept(true)
    {
        return m_ctx->remote;
    }
};

}
