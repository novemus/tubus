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

#include "../buffer.h"
#include <boost/asio.hpp>

struct executor
{
    boost::asio::io_context io;

    executor() : work(io)
    {
        for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
        {
            boost::asio::post(pool, [&]()
            {
                io.run();
            });
        }
    }

    ~executor()
    {
        io.stop();
    }

private:

    boost::asio::io_context::work work;
    boost::asio::thread_pool pool;
};

extern executor g_reactor;

class stream_source
{
    size_t m_read = 0;

public:

    static constexpr size_t chunk_size = 1024ul * 1024ul;

    tubus::const_buffer read_next()
    {
        tubus::mutable_buffer chunk(chunk_size);

        uint8_t* ptr = (uint8_t*)chunk.data();
        uint8_t* end = ptr + chunk.size();

        while (ptr < end)
        {
            *ptr = (m_read++) % 256;
            ++ptr;
        }

        return chunk;
    }

    size_t read() const
    {
        return m_read;
    }
};

class stream_sink
{
    size_t m_written = 0;

public:

    void write_next(const tubus::const_buffer& chunk)
    {
        const uint8_t* ptr = (const uint8_t*)chunk.data();
        const uint8_t* end = ptr + chunk.size();

        while (ptr < end)
        {
            if (*ptr != (m_written++) % 256)
                throw std::runtime_error("bad stream");
            ++ptr;
        }
    }

    size_t written() const
    {
        return m_written;
    }
};
