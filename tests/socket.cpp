/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../buffer.h"
#include "../socket.h"
#include "executor.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tubus_socket);

const boost::system::error_code NO_ERROR;
const size_t MB = 1024 * 1024;

BOOST_AUTO_TEST_CASE(peer_to_peer)
{
    tubus::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus::socket left(g_reactor.io);
    tubus::socket right(g_reactor.io);

    left.open(le, re, 0);
    right.open(re, le, 0);

    std::promise<void> rp;
    std::future<void> rf = rp.get_future();

    std::promise<void> wp;
    std::future<void> wf = wp.get_future();

    tubus::mutable_buffer rb(MB);
    tubus::mutable_buffer wb(MB);

    left.async_accept([&](const boost::system::error_code& error)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);
        
        size_t size = 0;
        BOOST_CHECK_NO_THROW(size = boost::asio::read(left, rb));
        BOOST_CHECK_EQUAL(rb.size(), size);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(rb.size(), boost::asio::read(left, rb, ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);

        boost::asio::async_read(left, rb, [&](const boost::system::error_code& error, size_t size)
        {
            BOOST_CHECK_EQUAL(error, NO_ERROR);
            BOOST_CHECK_EQUAL(rb.size(), size);

            rp.set_value();
        });
    });

    boost::system::error_code ec;
    right.connect(ec);

    BOOST_CHECK_EQUAL(ec, NO_ERROR);

    size_t size = 0;
    BOOST_CHECK_NO_THROW(size = boost::asio::write(right, wb));
    BOOST_CHECK_EQUAL(wb.size(), size);

    BOOST_CHECK_EQUAL(wb.size(), boost::asio::write(right, wb, ec));
    BOOST_CHECK_EQUAL(ec, NO_ERROR);

    boost::asio::async_write(right, wb, [&](const boost::system::error_code& error, size_t size)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);
        BOOST_CHECK_EQUAL(wb.size(), size);

        wp.set_value();
    });

    BOOST_CHECK_NO_THROW(rf.wait());
    BOOST_CHECK_NO_THROW(wf.wait());

    left.shutdown(ec);
    right.shutdown(ec);
}

BOOST_AUTO_TEST_SUITE_END();
