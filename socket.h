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
    boost::asio::io_context& m_asio;
    boost::asio::ip::udp::endpoint m_local;
    boost::asio::ip::udp::endpoint m_remote;
    channel_ptr m_channel;

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

public:

    typedef socket lowest_layer_type;
    typedef boost::asio::io_context::executor_type executor_type;

    socket(boost::asio::io_context& io) noexcept(true) 
        : m_asio(io)
    {
    }

    socket(boost::asio::io_context& io, const endpoint& le, const endpoint& re, uint64_t secret) noexcept(false) 
        : socket(io)
    {
        open(le, re, secret);
    }

    bool is_open() const noexcept(false) 
    {
        return !!m_channel;
    }

    void open(const endpoint& le, const endpoint& re, uint64_t secret) noexcept(false)
    {
        if (!m_channel)
        {
            m_local = le;
            m_remote = re;
            m_channel = create_channel(m_asio, le, re, secret);
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
        {
            m_channel->close();
            m_channel.reset();
        }
    }

    void connect() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_channel, &channel::connect, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "connect");
    }

    void connect(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_channel, &channel::connect, ec);
    }

    template<class connect_handler>
    void async_connect(connect_handler&& callback) noexcept(true)
    {
        detail::post_method(m_channel, &channel::connect, m_asio, callback);
    }

    void accept() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_channel, &channel::accept, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "accept");
    }

    void accept(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_channel, &channel::accept, ec);
    }

    template<class accept_handler>
    void async_accept(accept_handler&& callback) noexcept(true)
    {
        detail::post_method(m_channel, &channel::accept, m_asio, callback);
    }

    void shutdown() noexcept(false)
    {
        boost::system::error_code ec;
        detail::exec_method(m_channel, &channel::shutdown, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "shutdown");
    }

    void shutdown(boost::system::error_code& ec) noexcept(true)
    {
        detail::exec_method(m_channel, &channel::shutdown, ec);
    }

    template<class shutdown_handler>
    void async_shutdown(shutdown_handler&& callback) noexcept(true)
    {
        detail::post_method(m_channel, &channel::shutdown, m_asio, callback);
    }

    socket& lowest_layer() noexcept(true)
    {
        return *this;
    }

    template<class mutable_buffers>
    size_t read_some(const mutable_buffers& buffers) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = detail::exec_io_method(
            m_channel,
            &channel::read,
            &channel::readable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            ec
            );

        if (ec)
            boost::asio::detail::throw_error(ec, "read_some");
        
        return size;
    }

    template<class mutable_buffers>
    size_t read_some(const mutable_buffers& buffers, boost::system::error_code& ec) noexcept(true)
    {
        return detail::exec_io_method(
            m_channel,
            &channel::read,
            &channel::readable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            ec
            );
    }

    template<class const_buffers>
    size_t write_some(const const_buffers& buffers) noexcept(false)
    {
        boost::system::error_code ec;
        auto size = detail::exec_io_method(
            m_channel,
            &channel::write,
            &channel::writable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            ec
            );

        if (ec)
            boost::asio::detail::throw_error(ec, "write_some");

        return size;
    }

    template<class const_buffers>
    size_t write_some(const const_buffers& buffers, boost::system::error_code& ec) noexcept(true)
    {
        return detail::exec_io_method(
            m_channel,
            &channel::write,
            &channel::writable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            ec
            );
    }

    template<class mutable_buffers, class read_handler>
    void async_read_some(const mutable_buffers& buffers, read_handler&& callback) noexcept(true)
    {
        detail::post_io_method(
            m_channel,
            &channel::read,
            &channel::readable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            callback
            );
    }

    template<class const_buffers, class write_handler>
    void async_write_some(const const_buffers& buffers, write_handler&& callback) noexcept(true)
    {
        detail::post_io_method(
            m_channel,
            &channel::write,
            &channel::writable,
            boost::asio::buffer_sequence_begin(buffers),
            boost::asio::buffer_sequence_end(buffers),
            m_asio,
            callback
            );
    }

    executor_type get_executor() const noexcept(true)
    {
        return m_asio.get_executor();
    }

    endpoint local_endpoint() const noexcept(true)
    {
        return m_local;
    }

    endpoint remote_endpoint() const noexcept(true)
    {
        return m_remote;
    }
};

}
