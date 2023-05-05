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
#include "../acceptor.h"
#include "common.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tubus_acceptor);

const boost::system::error_code NONE_ERROR;

BOOST_AUTO_TEST_CASE(core)
{
    tubus::endpoint se(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    tubus::endpoint c1(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint c2(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus::acceptor server(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(server.open(se));

    std::promise<void> ap1;
    std::future<void> af1 = ap1.get_future();

    tubus::socket peer1(g_reactor.io);
    server.async_accept(peer1, [&](const boost::system::error_code& code)
    {
        BOOST_CHECK_EQUAL(code, NONE_ERROR);
        BOOST_CHECK_EQUAL(peer1.remote_endpoint(), c1);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(4096, boost::asio::read(peer1, tubus::mutable_buffer(4096), ec));
        BOOST_CHECK_EQUAL(ec, NONE_ERROR);
        BOOST_CHECK_EQUAL(peer1.available(), 0);

        ap1.set_value();
    });

    std::promise<void> ap2;
    std::future<void> af2 = ap2.get_future();

    tubus::socket peer2(g_reactor.io);
    server.async_accept(peer2, [&](const boost::system::error_code& code)
    {
        BOOST_CHECK_EQUAL(code, NONE_ERROR);
        BOOST_CHECK_EQUAL(peer2.remote_endpoint(), c2);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(1024, boost::asio::read(peer2, tubus::mutable_buffer(1024), ec));
        BOOST_CHECK_EQUAL(ec, NONE_ERROR);
        BOOST_CHECK_EQUAL(peer2.available(), 0);

        ap2.set_value();
    });

    tubus::socket client1(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(client1.open(c1));
    BOOST_REQUIRE_NO_THROW(client1.connect(se));

    tubus::socket client2(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(client2.open(c2));
    BOOST_REQUIRE_NO_THROW(client2.connect(se));

    boost::system::error_code ec;
    BOOST_CHECK_EQUAL(4096, boost::asio::write(client1, tubus::mutable_buffer(4096), ec));
    BOOST_CHECK_EQUAL(1024, boost::asio::write(client2, tubus::mutable_buffer(1024), ec));

    BOOST_CHECK_NO_THROW(af1.get());
    BOOST_CHECK_NO_THROW(af2.get());

    BOOST_CHECK_NO_THROW(peer1.close());
    BOOST_CHECK_NO_THROW(peer2.close());

    BOOST_CHECK_NO_THROW(client1.close());
    BOOST_CHECK_NO_THROW(client2.close());
}

BOOST_AUTO_TEST_SUITE_END();
