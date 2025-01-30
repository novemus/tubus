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

#include <tubus/export.h>
#include <tubus/buffer.h>
#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>

namespace tubus {

typedef boost::asio::ip::udp::endpoint endpoint;
typedef std::function<void(const boost::system::error_code&)> callback;
typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

struct TUBUS_EXPORT channel
{
    virtual ~channel() noexcept(true) {}
    virtual void close() noexcept(true) = 0;
    virtual void open(const endpoint& local) noexcept(false) = 0;
    virtual void connect(const endpoint& remote, const callback& handle) noexcept(true) = 0;
    virtual void accept(const endpoint& remote, const callback& handle) noexcept(true) = 0;
    virtual void read(const mutable_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void write(const const_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void shutdown(const callback& handle) noexcept(true) = 0;
    virtual size_t writable() const noexcept(true) = 0;
    virtual size_t readable() const noexcept(true) = 0;
    virtual endpoint host() const noexcept(false) = 0;
    virtual endpoint peer() const noexcept(false) = 0;
};

typedef std::shared_ptr<channel> channel_ptr;

TUBUS_EXPORT channel_ptr create_channel(boost::asio::io_context& io, uint64_t secret = 0) noexcept(true);

}
