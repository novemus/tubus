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

    void async_read() noexcept(true)
    {
        stop_io_timer(true);

        if (m_readers.empty())
            return;

        auto op = m_readers.front();
        m_readers.pop();
        m_rb_size -= op.buffer.size();

        start_io_timer(true);

        m_stream->async_read(op.buffer, [this, weak = weak_from_this(), callback = op.callback](const boost::system::error_code& error, size_t count)
        {
            if (auto ptr = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                async_read();
            }
            callback(error, count);
        });
    }

    void async_write() noexcept(true)
    {
        stop_io_timer(false);

        if (m_writers.empty())
            return;

        auto op = m_writers.front();
        m_writers.pop();
        m_wb_size -= op.buffer.size();

        start_io_timer(false);

        m_stream->async_write(op.buffer, [this, weak = weak_from_this(), callback = op.callback](const boost::system::error_code& error, size_t count)
        {
            if (auto ptr = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                async_write();
            }
            callback(error, count);
        });
    }

    void clear() noexcept(true)
    {
        while (not m_readers.empty())
        {
            auto op = m_readers.front();
            m_readers.pop();
            boost::asio::post(m_io, [callback = op.callback]()
            {
                callback(boost::asio::error::operation_aborted, 0);
            });
        }

        while (not m_writers.empty())
        {
            auto op = m_writers.front();
            m_writers.pop();
            boost::asio::post(m_io, [callback = op.callback]()
            {
                callback(boost::asio::error::operation_aborted, 0);
            });
        }

        boost::system::error_code ec;
        m_read_timer.cancel(ec);
        m_write_timer.cancel(ec);
        m_conn_timer.cancel(ec);
        m_acceptor.cancel(ec);
    }

    void start_io_timer(bool read) noexcept(true)
    {
        boost::asio::deadline_timer& timer = read ? m_read_timer : m_write_timer;
        timer.expires_from_now(qos::io_timeout());
        timer.async_wait([this, weak = weak_from_this()](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                if (not error)
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    boost::system::error_code ec;
                    m_stream->socket().cancel(ec);
                    clear();
                }
            }
        });
    }

    void stop_io_timer(bool read) noexcept(true)
    {
        boost::asio::deadline_timer& timer = read ? m_read_timer : m_write_timer;
        boost::system::error_code ec;
        timer.cancel(ec);
    }

    void connect(const endpoint& remote, const callback& handler, const boost::posix_time::ptime& deadline, bool bind = true) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (bind)
        {
            m_stream->socket().open(m_bind.protocol());
            m_stream->socket().non_blocking(true);
            m_stream->socket().set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
            m_stream->socket().set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
            m_stream->socket().set_option(boost::asio::socket_base::reuse_address(true));
            m_stream->socket().bind(m_bind);
        }

        m_stream->socket().async_connect(remote, [this, weak = weak_from_this(), remote, handler, deadline](const boost::system::error_code& error)
        {
            if (auto ptr = weak.lock())
            {
                if (error != boost::asio::error::connection_aborted && error != boost::asio::error::connection_refused)
                {
                    handler(error);

                    std::unique_lock<std::mutex> lock(m_mutex);
                    boost::system::error_code ec;
                    m_conn_timer.cancel(ec);
                    return;
                }

                if (boost::posix_time::microsec_clock::universal_time() > deadline)
                {
                    handler(boost::asio::error::timed_out);

                    std::unique_lock<std::mutex> lock(m_mutex);
                    boost::system::error_code ec;
                    m_conn_timer.cancel(ec);
                    return;
                }

                m_conn_timer.expires_from_now(boost::posix_time::seconds(1));
                m_conn_timer.async_wait([weak, remote, handler, deadline](const boost::system::error_code& error)
                {
                    if (auto ptr = weak.lock())
                    {
                        if (not error)
                        {
                            ptr->connect(remote, handler, deadline, false);
                        }
                        else
                        {
                            handler(error);
                        }
                    }
                    else
                    {
                        handler(boost::asio::error::shut_down);
                    }
                });
                return;
            }

            handler(boost::asio::error::shut_down);
        });
    }

    void accept(const endpoint& remote, const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_acceptor.open(m_bind.protocol());
        m_acceptor.non_blocking(true);
        m_acceptor.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_acceptor.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
        m_acceptor.bind(m_bind);
        m_acceptor.listen();

        m_acceptor.async_accept(m_stream->socket(), [this, weak = weak_from_this(), remote, handler, deadline](const boost::system::error_code& error)
        {
            if (auto ptr = weak.lock())
            {
                if (not error && remote != endpoint() && m_stream->socket().remote_endpoint() != remote)
                {
                    handler(boost::asio::error::no_permission);
                }
                else
                {
                    handler(error);
                }

                std::unique_lock<std::mutex> lock(m_mutex);
                boost::system::error_code ec;
                m_conn_timer.cancel(ec);
                m_acceptor.close(ec);
                return;
            }

            handler(boost::asio::error::shut_down);
        });

        m_conn_timer.expires_at(deadline);
        m_conn_timer.async_wait([this, weak = weak_from_this()](const boost::system::error_code& error)
        {
            if (not error)
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

public:

    transport(boost::asio::io_context& io, uint64_t secret) noexcept(true)
        : m_io(io)
        , m_stream(std::make_shared<proto::stream>(io, secret))
        , m_acceptor(io)
        , m_conn_timer(m_stream->socket().get_executor())
        , m_read_timer(m_stream->socket().get_executor())
        , m_write_timer(m_stream->socket().get_executor())
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
        m_stream->socket().close(ec);
        clear();
    }

    void shutdown(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        boost::system::error_code ec;
        m_stream->socket().shutdown(boost::asio::socket_base::shutdown_both, ec);
        boost::asio::post(m_io, [handler, ec]()
        {
            handler(ec);
        });
        clear();
    }

    void connect(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        connect(remote, handler, boost::posix_time::microsec_clock::universal_time() + qos::connect_timeout());
    }

    void accept(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        accept(remote, handler, boost::posix_time::microsec_clock::universal_time() + qos::connect_timeout());
    }

    void read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_wb_size + buffer.size() > qos::receive_buffer_size())
        {
            boost::asio::post(m_io, [handler]()
            {
                handler(boost::asio::error::message_size, 0);
            });
            return;
        }

        m_readers.emplace(buffer, handler);
        m_rb_size += buffer.size();

        if (m_readers.size() == 1)
            async_read();
    }

    void write(const const_buffer& buffer, const io_callback& handler) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_wb_size + buffer.size() > qos::send_buffer_size())
        {
            boost::asio::post(m_io, [handler]()
            {
                handler(boost::asio::error::message_size, 0);
            });
            return;
        }

        m_writers.emplace(buffer, handler);
        m_wb_size += buffer.size();

        if (m_writers.size() == 1)
            async_write();
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
    std::shared_ptr<proto::stream> m_stream;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::endpoint m_bind;
    boost::asio::deadline_timer m_conn_timer;
    boost::asio::deadline_timer m_read_timer;
    boost::asio::deadline_timer m_write_timer;
    std::queue<transport::read_task> m_readers;
    std::queue<transport::write_task> m_writers;
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
