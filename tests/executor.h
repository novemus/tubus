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