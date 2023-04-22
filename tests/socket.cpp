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
#include "common.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tubus_socket);

const boost::system::error_code NO_ERROR;

BOOST_AUTO_TEST_CASE(core)
{
    tubus::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus::socket left(g_reactor.io);
    tubus::socket right(g_reactor.io);

    boost::system::error_code ec;
    left.open(le, re, 1234567890, ec);
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    right.open(re, le, 1234567890, ec);
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    std::promise<void> rp;
    std::future<void> rf = rp.get_future();

    std::promise<void> wp;
    std::future<void> wf = wp.get_future();

    stream_source source;
    stream_sink sink;

    left.async_accept([&](const boost::system::error_code& error)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);
        tubus::mutable_buffer mb(stream_source::chunk_size);

        size_t size = 0;
        BOOST_CHECK_NO_THROW(size = boost::asio::read(left, mb));
        BOOST_CHECK_EQUAL(stream_source::chunk_size, size);
        BOOST_CHECK_NO_THROW(sink.write_next(mb));

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(stream_source::chunk_size, boost::asio::read(left, mb, ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);
        BOOST_CHECK_NO_THROW(sink.write_next(mb));

        boost::asio::async_read(left, mb, [&, mb](const boost::system::error_code& error, size_t size)
        {
            BOOST_CHECK_EQUAL(error, NO_ERROR);
            BOOST_CHECK_EQUAL(stream_source::chunk_size, size);
            BOOST_CHECK_NO_THROW(sink.write_next(mb));

            rp.set_value();
        });
    });

    right.async_connect([&](const boost::system::error_code& error)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);

        size_t size = 0;
        BOOST_CHECK_NO_THROW(size = boost::asio::write(right, source.read_next()));
        BOOST_CHECK_EQUAL(stream_source::chunk_size, size);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(stream_source::chunk_size, boost::asio::write(right, source.read_next(), ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);

        boost::asio::async_write(right, source.read_next(), [&](const boost::system::error_code& error, size_t size)
        {
            BOOST_CHECK_EQUAL(error, NO_ERROR);
            BOOST_CHECK_EQUAL(stream_source::chunk_size, size);

            wp.set_value();
        });
    });

    BOOST_CHECK_NO_THROW(rf.get());
    BOOST_CHECK_NO_THROW(wf.get());

    BOOST_CHECK_EQUAL(source.read(), sink.written());

    BOOST_REQUIRE_EQUAL(stream_source::chunk_size, right.write_some(tubus::mutable_buffer(stream_source::chunk_size), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    BOOST_REQUIRE_EQUAL(stream_source::chunk_size / 2, left.read_some(tubus::mutable_buffer(stream_source::chunk_size / 2), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    BOOST_REQUIRE_EQUAL(stream_source::chunk_size / 2, left.read_some(tubus::mutable_buffer(stream_source::chunk_size), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    left.shutdown(ec);
    right.shutdown(ec);
}

BOOST_AUTO_TEST_SUITE_END();
