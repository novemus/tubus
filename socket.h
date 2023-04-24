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

    socket(boost::asio::io_context& io, uint64_t secret = 0) noexcept(true) 
        : m_asio(io)
        , m_channel(create_channel(m_asio, secret))
    {
    }

    socket(socket&& other) noexcept(true) 
        : m_asio(other.m_asio)
        , m_local(other.m_local)
        , m_remote(other.m_remote)
        , m_channel(other.m_channel)
    {
        other.m_local = endpoint();
        other.m_remote = endpoint();
        other.m_channel.reset();
    }

    void bind(const endpoint& local) noexcept(false)
    {
        m_local = local;
        m_channel->bind(local);
    }

    void bind(const endpoint& local, boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            m_local = local;
            m_channel->bind(local);
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    void close() noexcept(true)
    {
        m_channel->close();
    }

    void connect(const endpoint& remote) noexcept(false)
    {
        m_remote = remote;

        std::promise<boost::system::error_code> promise; 
        std::future<boost::system::error_code> future = promise.get_future(); 

        m_channel->connect(remote, [&promise](const boost::system::error_code& code)
        {
            promise.set_value(code);
        });

        boost::system::error_code error = future.get();

        if (error)
            boost::asio::detail::throw_error(error, "connect");
    }

    void connect(const endpoint& remote, boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            connect(remote);
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    template<class connect_handler>
    void async_connect(const endpoint& remote, connect_handler&& callback) noexcept(true)
    {
        m_remote = remote;
        m_channel->connect(remote, std::move(callback));
    }

    void accept(const endpoint& remote) noexcept(false)
    {
        m_remote = remote;

        std::promise<boost::system::error_code> promise; 
        std::future<boost::system::error_code> future = promise.get_future(); 
        
        m_channel->accept(remote, [&promise](const boost::system::error_code& code)
        {
            promise.set_value(code);
        });

        boost::system::error_code error = future.get();

        if (error)
            boost::asio::detail::throw_error(error, "accept");
    }

    void accept(const endpoint& remote, boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            accept(remote);
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    template<class accept_handler>
    void async_accept(const endpoint& remote, accept_handler&& callback) noexcept(true)
    {
        m_remote = remote;
        m_channel->accept(remote, std::move(callback));
    }

    void shutdown() noexcept(false)
    {
        std::promise<boost::system::error_code> promise; 
        std::future<boost::system::error_code> future = promise.get_future(); 
        
        m_channel->shutdown([&promise](const boost::system::error_code& code)
        {
            promise.set_value(code);
        });

        boost::system::error_code error = future.get();

        if (error)
            boost::asio::detail::throw_error(error, "shutdown");
    }

    void shutdown(boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            shutdown();
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    template<class shutdown_handler>
    void async_shutdown(shutdown_handler&& callback) noexcept(true)
    {
        m_channel->shutdown(std::move(callback));
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
