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
#include "socket.h"
#include <future>

namespace tubus {

typedef boost::asio::ip::udp::endpoint endpoint;

class acceptor
{
    boost::asio::io_context& m_asio;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::ip::udp::endpoint m_local;
    uint64_t m_secret;

    acceptor(const acceptor&) = delete;
    acceptor& operator=(const acceptor&) = delete;

public:

    typedef boost::asio::io_context::executor_type executor_type;

    acceptor(boost::asio::io_context& io) noexcept(true) 
        : m_asio(io)
        , m_socket(io)
    {
    }

    acceptor(boost::asio::io_context& io, const endpoint& local, uint64_t secret) noexcept(false) 
        : acceptor(io)
    {
        bind(local, secret);
    }

    executor_type get_executor() const noexcept(true)
    {
        return m_asio.get_executor();
    }

    void bind(const endpoint& local, uint64_t secret) noexcept(false)
    {
        if (!m_socket.is_open())
        {
            m_socket.open(local.protocol());
            m_socket.set_option(boost::asio::socket_base::reuse_address(true));
            m_socket.bind(local);
            m_local = local;
            m_secret = secret;
            return;
        }

        boost::asio::detail::throw_error(boost::asio::error::operation_not_supported, "bind");
    }

    void bind(const endpoint& local, uint64_t secret, boost::system::error_code& ec) noexcept(true)
    {
        try
        {
            bind(local, secret);
        }
        catch( const boost::system::system_error& ex)
        {
            ec = ex.code();
        }
    }

    void close() noexcept(false)
    {
        if (m_socket.is_open())
            m_socket.close();
    }

    void close(boost::system::error_code& ec) noexcept(true)
    {
        if (m_socket.is_open())
            m_socket.close(ec);
    }

    void cancel() noexcept(false)
    {
        if (m_socket.is_open())
            m_socket.cancel();
    }

    void cancel(boost::system::error_code& ec) noexcept(true)
    {
        if (m_socket.is_open())
            m_socket.cancel(ec);
    }

    template <typename socket_option>
    void set_option(const socket_option& option) noexcept(false)
    {
        m_socket.set_option(option);
    }

    template <typename socket_option>
    void set_option(const socket_option& option, boost::system::error_code& ec) noexcept(true)
    {
        m_socket.set_option(option, ec);
    }

    template <typename socket_option>
    void get_option(socket_option& option) const noexcept(false)
    {
        m_socket.get_option(option);
    }

    template <typename socket_option>
    void get_option(socket_option& option, boost::system::error_code& ec) const noexcept(true)
    {
        m_socket.get_option(option, ec);
    }

    template <typename io_control_command>
    void io_control(io_control_command& command) noexcept(false)
    {
        m_socket.io_control(command);
    }

    template <typename io_control_command>
    void io_control(io_control_command& command, boost::system::error_code& ec) noexcept(true)
    {
        m_socket.io_control(command, ec);
    }

    bool non_blocking() const noexcept(true)
    {
        return m_socket.non_blocking();
    }

    void non_blocking(bool mode) noexcept(false)
    {
        m_socket.non_blocking(mode);
    }

    void non_blocking(bool mode, boost::system::error_code& ec) noexcept(true)
    {
        m_socket.non_blocking(mode, ec);
    }

    bool native_non_blocking() const noexcept(true)
    {
        return m_socket.native_non_blocking();
    }

    void native_non_blocking(bool mode) noexcept(false)
    {
        m_socket.native_non_blocking(mode);
    }

    void native_non_blocking(bool mode, boost::system::error_code& ec) noexcept(true)
    {
        m_socket.native_non_blocking(mode, ec);
    }

    endpoint local_endpoint() const noexcept(true)
    {
        return m_local;
    }

    void wait(boost::asio::socket_base::wait_type w) noexcept(false)
    {
        m_socket.wait(w);
    }

    void wait(boost::asio::socket_base::wait_type w, boost::system::error_code& ec) noexcept(true)
    {
        m_socket.wait(w, ec);
    }

    template<class wait_handler>
    void async_wait(boost::asio::socket_base::wait_type w, wait_handler&& handler) noexcept(true)
    {
        m_socket.async_wait(w, handler);
    }

    void accept(socket& peer) noexcept(false)
    {
        boost::system::error_code ec;
        accept(peer, ec);

        if (ec)
            boost::asio::detail::throw_error(ec, "accept");
    }

    void accept(socket& peer, boost::system::error_code& ec) noexcept(true)
    {
        endpoint remote;
        m_socket.receive_from(mutable_buffer(), remote, 0, ec);

        if (ec)
            return;

        peer.open(m_local, remote, m_secret, ec);

        if (ec)
            return;

        std::promise<boost::system::error_code> promise;
        std::future<boost::system::error_code> future = promise.get_future();

        peer.async_accept([&promise](const boost::system::error_code& error)
        {
            promise.set_value(error);
        });

        ec = future.get();
    }

    template<class accept_handler>
    void async_accept(socket& peer, accept_handler&& callback) noexcept(true)
    {
        auto remote = std::make_shared<endpoint>();
        m_socket.async_receive_from(mutable_buffer(), *remote, [&peer, local = m_local, remote, secret = m_secret, callback](const boost::system::error_code& error, size_t size)
        {
            if (error)
            {
                callback(error);
                return;
            }

            boost::system::error_code code;
            peer.open(local, *remote, secret, code);

            if (code)
            {
                callback(code);
                return;
            }

            peer.async_accept(callback);
        });
    }
};

}
