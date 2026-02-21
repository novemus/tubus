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
#include <memory>

namespace tubus {

mutable_buffer create_buffer(size_t size) noexcept(true)
{
    static std::mutex s_mutex;
    static std::map<size_t, std::shared_ptr<buffer_factory>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto res = s_pool.emplace(size, std::make_shared<buffer_factory>(size));
    return res.first->second->obtain();
}

uint64_t make_inverter(uint64_t secret, uint64_t salt, bool dim) noexcept(true)
{
    uint64_t base = secret + salt;
    uint64_t shift = (base & 0x3F) | 0x01;
    return dim ? htobe64(((base >> shift) | (base << (64 - shift))) ^ salt) : be64toh(((base >> shift) | (base << (64 - shift))) ^ salt);
}

template<class value_type> value_type getenv(const std::string& name, const value_type& def) noexcept(true)
{
    try
    {
        const char *env = std::getenv(name.c_str());
        return env ? boost::lexical_cast<value_type>(env) : def;
    }
    catch (const boost::bad_lexical_cast& ex)
    {
        std::cerr << ex.what() << std::endl;
    }

    return def;
}

namespace udp { namespace qos {

size_t receive_buffer_size() noexcept(true)
{
    static size_t s_size(getenv("TUBUS_UDP_RECEIVE_BUFFER_SIZE", 5ul * 1024ul * 1024ul));
    return s_size;
}

size_t send_buffer_size() noexcept(true)
{
    static size_t s_size(getenv("TUBUS_UDP_SEND_BUFFER_SIZE", 5ul * 1024ul * 1024ul));
    return s_size;
}

boost::posix_time::time_duration tcp_io_timeout() noexcept(true)
{
    static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_UDP_TCP_IO_TIMEOUT", 10000l));
    return s_timeout;
}

size_t max_packet_size() noexcept(true)
{
    static size_t s_size(getenv("TUBUS_UDP_MAX_PACKET_SIZE", 1406ul));
    return s_size;
}

boost::posix_time::time_duration ping_timeout() noexcept(true)
{
    static boost::posix_time::seconds s_timeout(getenv("TUBUS_UDP_PING_TIMEOUT", 15l));
    return s_timeout;
}

boost::posix_time::time_duration shutdown_timeout() noexcept(true)
{
    static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_UDP_SHUTDOWN_TIMEOUT", 2000l));
    return s_timeout;
}

boost::posix_time::time_duration connect_timeout() noexcept(true)
{
    static boost::posix_time::seconds s_timeout(getenv("TUBUS_UDP_CONNECT_TIMEOUT", 30l));
    return s_timeout;
}

boost::posix_time::time_duration accept_timeout() noexcept(true)
{
    static boost::posix_time::seconds s_timeout(getenv("TUBUS_UDP_ACCEPT_TIMEOUT", 30l));
    return s_timeout;
}

size_t snippet_flight() noexcept(true)
{
    static size_t s_flight(getenv("TUBUS_UDP_SNIPPET_FLIGHT", 1024ul));
    return s_flight;
}

size_t move_attempts() noexcept(true)
{
    static size_t s_attempts(getenv("TUBUS_UDP_MOVE_ATTEMPTS", 32ul));
    return s_attempts;
}

}}

namespace tcp { namespace qos {

size_t receive_buffer_size() noexcept(true)
{
    static size_t s_size(getenv("TUBUS_TCP_RECEIVE_BUFFER_SIZE", 5ul * 1024ul * 1024ul));
    return s_size;
}

size_t send_buffer_size() noexcept(true)
{
    static size_t s_size(getenv("TUBUS_TCP_SEND_BUFFER_SIZE", 5ul * 1024ul * 1024ul));
    return s_size;
}

boost::posix_time::time_duration connect_timeout() noexcept(true)
{
    static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_TCP_CONNECT_TIMEOUT", 10000l));
    return s_timeout;
}

boost::posix_time::time_duration io_timeout() noexcept(true)
{
    static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_TCP_IO_TIMEOUT", 10000l));
    return s_timeout;
}

}}

}
