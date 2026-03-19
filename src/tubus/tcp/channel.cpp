/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <tubus/utils.h>
#include <tubus/channel.h>
#include <tubus/tcp/proto.h>
#include <queue>
#include <iostream>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace tubus { namespace tcp {

class transport : public tubus::tcp_channel, public std::enable_shared_from_this<transport>
{
    static constexpr size_t SOCKET_BUFFER_SIZE = 1048576;

    template<class io_buffer>
    struct io_task
    {
        io_buffer buffer;
        io_callback callback;

        io_task(const io_buffer& buff, const io_callback& call)
            : buffer(buff)
            , callback(call)
        {
        }
    };

    using read_task = io_task<tubus::mutable_buffer>;
    using write_task = io_task<tubus::const_buffer>;

protected:

    void async_read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true)
    {
        m_stream->async_read(buffer, [this, weak = weak_from_this(), buffer, handler](const boost::system::error_code& error, size_t count)
        {
            if (auto ptr = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);

                m_rq.pop();
                m_rb_size -= buffer.size();
                
                if (!m_rq.empty())
                {
                    auto op = m_rq.front();
                    async_read(op.buffer, op.callback);
                }

                boost::asio::post(m_strand, std::bind(handler, error, count));
            }
            else
                handler(error, count);
        });
    }

    void async_write(const const_buffer& buffer, const io_callback& handler) noexcept(true)
    {
        m_stream->async_write(buffer, [this, weak = weak_from_this(), buffer, handler](const boost::system::error_code& error, size_t count)
        {
            if (auto ptr = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);

                m_wq.pop();
                m_wb_size -= buffer.size();
                
                if (!m_wq.empty())
                {
                    auto op = m_wq.front();
                    async_write(op.buffer, op.callback);
                }

                boost::asio::post(m_strand, std::bind(handler, error, count));
            }
            else
                handler(error, count);
        });
    }

    void do_connect(const endpoint& remote, const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_stream->socket().is_open())
        {
            boost::system::error_code ec;
            m_stream->socket().close(ec);
        }

        m_stream->socket().open(m_bind.protocol());
        m_stream->socket().non_blocking(true);
        m_stream->socket().set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_stream->socket().set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_stream->socket().set_option(boost::asio::socket_base::reuse_address(true));
        m_stream->socket().bind(m_bind);

        m_stream->socket().async_connect(remote, [this, weak = weak_from_this(), remote, handler, deadline](const boost::system::error_code& error)
        {
            if (auto ptr = weak.lock())
            {
                if (error != boost::asio::error::connection_refused || boost::posix_time::microsec_clock::universal_time() > deadline)
                {
                    handler(error);
                    return;
                }

                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_stream->socket().is_open())
                {
                    handler(boost::asio::error::operation_aborted);
                    return;
                }

                m_timer.expires_from_now(boost::posix_time::seconds(1));
                m_timer.async_wait([this, weak, remote, handler, deadline](const boost::system::error_code& error)
                {
                    if (auto ptr = weak.lock())
                    {
                        if (!error)
                            do_connect(remote, handler, deadline);
                        else
                            handler(error);
                    }
                    else
                        handler(boost::asio::error::operation_aborted);
                });
                return;
            }

            handler(boost::asio::error::operation_aborted);
        });
    }

    void do_accept(const endpoint& remote, const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_stream->socket().is_open())
        {
            boost::system::error_code ec;
            m_stream->socket().close(ec);
        }
        else
        {
            m_acceptor.open(m_bind.protocol());
            m_acceptor.non_blocking(true);
            m_acceptor.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
            m_acceptor.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
            m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
            m_acceptor.bind(m_bind);
            m_acceptor.listen();

            m_timer.expires_at(deadline);
            m_timer.async_wait([this, weak = weak_from_this()](const boost::system::error_code& error)
            {
                if (!error)
                {
                    if (auto ptr = weak.lock())
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        boost::system::error_code ec;
                        m_acceptor.close(ec);
                        return;
                    }
                }
            });
        }

        m_acceptor.async_accept(m_stream->socket(), [this, weak = weak_from_this(), remote, handler](const boost::system::error_code& error)
        {
            if (auto ptr = weak.lock())
            {
                auto expected = [&]()
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    auto actual = m_stream->socket().remote_endpoint();

                    if (remote.port() != 0 && remote.port() != actual.port())
                        return false;
                    if (!remote.address().is_unspecified() && remote.address() != actual.address())
                        return false;

                    return true;
                };

                if (error || expected())
                {
                    handler(error);

                    std::unique_lock<std::mutex> lock(m_mutex);
                    boost::system::error_code ec;
                    m_timer.cancel(ec);
                    m_acceptor.close(ec);
                    return;
                }

                do_accept(remote, handler, boost::posix_time::ptime());
                return;
            }

            handler(boost::asio::error::operation_aborted);
        });
    }

public:

    transport(boost::asio::io_context& io, uint64_t secret) noexcept(true)
        : m_io(io)
        , m_strand(io)
        , m_stream(std::make_shared<proto::stream>(io, secret))
        , m_acceptor(io)
        , m_timer(m_stream->socket().get_executor())
    {
    }

    ~transport() noexcept(true) override
    {
        close();
    }

    void open(const endpoint& local) noexcept(false) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_bind = local;
    }

    void close() noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        boost::system::error_code ec;
        m_timer.cancel(ec);
        m_acceptor.close(ec);
        m_stream->socket().close(ec);
    }

    void shutdown(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        boost::system::error_code ec;
        m_timer.cancel(ec);
        m_acceptor.close(ec);
        m_stream->socket().shutdown(boost::asio::socket_base::shutdown_both, ec);
        boost::asio::post(m_io, std::bind(handler, ec));
    }

    void connect(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        do_connect(remote, handler, boost::posix_time::microsec_clock::universal_time() + qos::connect_timeout());
    }

    void accept(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        do_accept(remote, handler, boost::posix_time::microsec_clock::universal_time() + qos::accept_timeout());
    }

    void read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_stream->socket().is_open())
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::not_connected, 0));
            return;
        }
    
        if (m_wb_size + buffer.size() > qos::receive_buffer_size())
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::message_size, 0));
            return;
        }

        m_rq.emplace(buffer, handler);
        m_rb_size += buffer.size();

        if (m_rq.size() == 1)
            async_read(buffer, handler);
    }

    void write(const const_buffer& buffer, const io_callback& handler) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_stream->socket().is_open())
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::not_connected, 0));
            return;
        }

        if (m_wb_size + buffer.size() > qos::send_buffer_size())
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::message_size, 0));
            return;
        }

        m_wq.emplace(buffer, handler);
        m_wb_size += buffer.size();

        if (m_wq.size() == 1)
            async_write(buffer, handler);
    }

    size_t writable() const noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return qos::send_buffer_size() - m_wb_size;
    }

    size_t readable() const noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        boost::system::error_code ec;
        auto size = m_stream->available(ec);
        return size > m_rb_size ? size - m_rb_size : 0;
    }

    endpoint host() const noexcept(false) override
    {
        return m_stream->socket().local_endpoint();
    }

    endpoint peer() const noexcept(false) override
    {
        return m_stream->socket().remote_endpoint();
    }

private:

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    std::shared_ptr<proto::stream> m_stream;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::endpoint m_bind;
    boost::asio::deadline_timer m_timer;
    std::queue<transport::read_task> m_rq;
    std::queue<transport::write_task> m_wq;
    size_t m_rb_size = 0;
    size_t m_wb_size = 0;
    mutable std::mutex m_mutex;
};

}

template<> std::shared_ptr<tcp_channel> tcp_channel::create(boost::asio::io_context& io, uint64_t secret) noexcept(true)
{
    return std::make_shared<tcp::transport>(io, secret);
}

}
