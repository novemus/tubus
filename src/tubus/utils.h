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

#include <tubus/buffer.h>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>

namespace tubus {

uint64_t make_inverter(uint64_t secret, uint64_t salt, bool dim) noexcept(true);

namespace udp { namespace qos {
    size_t receive_buffer_size() noexcept(true);
    size_t send_buffer_size() noexcept(true);
    size_t max_packet_size() noexcept(true);
    boost::posix_time::time_duration ping_timeout() noexcept(true);
    boost::posix_time::time_duration shutdown_timeout() noexcept(true);
    boost::posix_time::time_duration connect_timeout() noexcept(true);
    boost::posix_time::time_duration accept_timeout() noexcept(true);
    size_t snippet_flight() noexcept(true);
    size_t move_attempts() noexcept(true);
    size_t receive_buffer_size() noexcept(true);
    size_t send_buffer_size() noexcept(true);
}}

namespace tcp { namespace qos {
    boost::posix_time::time_duration connect_timeout() noexcept(true);
    boost::posix_time::time_duration accept_timeout() noexcept(true);
    size_t receive_buffer_size() noexcept(true);
    size_t send_buffer_size() noexcept(true);
}}

}
