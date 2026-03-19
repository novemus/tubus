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

#include <tubus/utils.h>
#include <tubus/buffer.h>
#include <random>
#include <future>
#include <boost/asio/ip/tcp.hpp>

namespace tubus { namespace tcp { namespace proto {

static constexpr uint64_t head_size = sizeof(uint64_t) * 2;
static constexpr uint64_t signature = 0x09090001;

typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

class stream : public std::enable_shared_from_this<stream>
{
    boost::asio::ip::tcp::socket m_socket;
    uint64_t m_secret = 0;

    struct {
        uint64_t salt = 0;
        uint64_t inverter = 0;
        uint64_t shift = 0;
    } m_ostate;

    struct {
        uint64_t salt = 0;
        uint64_t inverter = 0;
        uint64_t shift = 0;
    } m_istate;

public:

    stream(boost::asio::io_context& io, uint64_t secret) noexcept(true)
        : m_socket(io)
        , m_secret(secret)
    {
    }

    boost::asio::ip::tcp::socket& socket() noexcept(true)
    {
        return m_socket;
    }

    void async_write(const const_buffer& chunk, const io_callback& callback) noexcept(true)
    {
        if (m_secret != 0 && m_ostate.salt == 0)
        {
            mutable_buffer buffer(chunk.size() + proto::head_size);
            buffer.fill(proto::head_size, chunk.size(), chunk.data());
            write_head(buffer.data(), proto::signature);
            alter_data(static_cast<uint8_t*>(buffer.data()) + head_size, chunk.size(), true);
            boost::asio::async_write(m_socket, buffer, [callback](const boost::system::error_code& ec, size_t count)
            {
                if (!ec)
                {
                    if (count < proto::head_size)
                    {
                        callback(boost::asio::error::not_connected, count);
                        return;
                    }
                    count -= proto::head_size;
                }

                callback(ec, count);
            });
        }
        else if (m_secret != 0)
        {
            mutable_buffer buffer(chunk.size());
            buffer.fill(0, chunk.size(), chunk.data());
            alter_data(buffer.data(), buffer.size(), true);
            boost::asio::async_write(m_socket, buffer, callback);
        }
        else
        {
            boost::asio::async_write(m_socket, chunk, callback);
        }
    }

    void async_read(const mutable_buffer& chunk, const io_callback& callback) noexcept(true)
    {
        if (m_secret != 0 && m_istate.salt == 0)
        {
            mutable_buffer header(proto::head_size);

            std::vector<boost::asio::mutable_buffer> buffers;
            buffers.push_back(header);
            buffers.push_back(chunk);

            boost::asio::async_read(m_socket, buffers, [this, weak = weak_from_this(), header, chunk, callback](const boost::system::error_code& ec, size_t size)
            {
                if (auto ptr = weak.lock())
                {
                    if (size < proto::head_size)
                    {
                        callback(ec ? ec : boost::asio::error::not_connected, 0);
                        return;
                    }

                    size -= proto::head_size;

                    uint64_t sign = 0;
                    read_head(header.data(), sign);

                    if (sign != proto::signature)
                    {
                        callback(ec ? ec : boost::asio::error::no_recovery, size);
                        return;
                    }

                    alter_data(chunk.data(), size, false);
                    callback(ec, size);
                    return;
                }

                callback(boost::asio::error::network_reset, 0);
            });
        }
        else if (m_secret != 0)
        {
            boost::asio::async_read(m_socket, chunk, [this, weak = weak_from_this(), chunk, callback](const boost::system::error_code& ec, size_t size)
            {
                if (auto ptr = weak.lock())
                {
                    alter_data(chunk.data(), size, false);
                    callback(ec, size);
                    return;
                }

                callback(boost::asio::error::network_reset, 0);
            });
        }
        else
        {
            boost::asio::async_read(m_socket, chunk, callback);
        }
    }

    size_t read(const mutable_buffer& buffer, boost::system::error_code& ec) noexcept(true)
    {
        std::promise<size_t> promise;
        std::future<size_t> future = promise.get_future();

        async_read(buffer, [&](const boost::system::error_code& error, size_t size)
        {
            ec = error;
            promise.set_value(size);
        });

        return future.get();
    }

    size_t write(const const_buffer& buffer, boost::system::error_code& ec) noexcept(true)
    {
        std::promise<size_t> promise;
        std::future<size_t> future = promise.get_future();

        async_write(buffer, [&](const boost::system::error_code& error, size_t size)
        {
            ec = error;
            promise.set_value(size);
        });

        return future.get();
    }

    size_t available(boost::system::error_code& ec) const noexcept(true)
    {
        size_t count = m_socket.available(ec);
        if (m_secret != 0 && m_istate.salt == 0)
            return count >= proto::head_size ? count - proto::head_size : 0;
        return count;
    }

private:

    void write_head(void* data, uint64_t sign)
    {
        uint64_t* first = static_cast<uint64_t*>(data);
        uint64_t* second = first + 1;

        std::random_device dev;
        std::mt19937_64 gen(dev());

        m_ostate.salt = static_cast<uint64_t>(gen());
        m_ostate.inverter = tubus::make_inverter(m_secret, m_ostate.salt, true);

        *first = htobe64(m_ostate.salt ^ m_secret);
        *second = htobe64(sign ^ m_ostate.inverter);
    }

    void read_head(void* data, uint64_t& sign)
    {
        uint64_t* first = static_cast<uint64_t*>(data);
        uint64_t* second = first + 1;

        m_istate.salt = be64toh(*first) ^ m_secret;
        m_istate.inverter = tubus::make_inverter(m_secret, m_istate.salt, false);

        sign = be64toh(*second) ^ m_istate.inverter;
    }

    void alter_data(void* data, uint64_t size, bool dim) noexcept(true)
    {
        uint64_t& inverter = dim ? m_ostate.inverter : m_istate.inverter;
        uint64_t& salt = dim ? m_ostate.salt : m_istate.salt;
        uint64_t& shift = dim ? m_ostate.shift : m_istate.shift;

        uint8_t* ptr = (uint8_t*)data;
        uint8_t* end = ptr + size;

        if (shift > 0)
        {
            uint8_t* inv = reinterpret_cast<uint8_t*>(&inverter) + shift;
            while (shift < sizeof(uint64_t) && ptr < end)
            {
                *ptr ^= *inv;
                ++ptr;
                ++inv;
                ++shift;
            }

            if (shift == sizeof(uint64_t))
                shift = 0;
        }

        if (ptr < end)
        {
            inverter = tubus::make_inverter(inverter, salt, dim);
            while (ptr + sizeof(uint64_t) <= end)
            {
                *(uint64_t*)ptr ^= inverter;
                ptr += sizeof(uint64_t);

                if (ptr < end)
                    inverter = tubus::make_inverter(inverter, salt, dim);
            }

            uint8_t* inv = reinterpret_cast<uint8_t*>(&inverter);
            while (ptr < end)
            {
                *ptr ^= *inv;
                ++ptr;
                ++inv;
                ++shift;
            }
        }
    }
};

}}}
