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
#include "../acceptor.h"
#include "common.h"
#include <boost/asio/ssl.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tubus_socket);

const boost::system::error_code NO_ERROR;

BOOST_AUTO_TEST_CASE(core)
{
    tubus::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus::socket left(g_reactor.io, 1234567890);
    tubus::socket right(g_reactor.io, 1234567890);

    BOOST_CHECK_NO_THROW(left.bind(le));
    BOOST_CHECK_NO_THROW(right.bind(re));

    std::promise<void> rp;
    std::future<void> rf = rp.get_future();

    std::promise<void> wp;
    std::future<void> wf = wp.get_future();

    stream_source source;
    stream_sink sink;

    left.async_accept(re, [&](const boost::system::error_code& error)
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

    right.async_connect(le, [&](const boost::system::error_code& error)
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

    boost::system::error_code ec;
    BOOST_REQUIRE_EQUAL(stream_source::chunk_size, right.write_some(tubus::mutable_buffer(stream_source::chunk_size), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    BOOST_REQUIRE_EQUAL(stream_source::chunk_size / 2, left.read_some(tubus::mutable_buffer(stream_source::chunk_size / 2), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    BOOST_REQUIRE_EQUAL(stream_source::chunk_size / 2, left.read_some(tubus::mutable_buffer(stream_source::chunk_size), ec));
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    left.shutdown(ec);
    BOOST_CHECK_EQUAL(ec, NO_ERROR);

    right.shutdown(ec);
    BOOST_CHECK_EQUAL(ec, NO_ERROR);
}

BOOST_AUTO_TEST_CASE(ssl)
{
    tubus::endpoint se(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint ce(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    boost::asio::ssl::context srv(boost::asio::ssl::context::sslv23);
    srv.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::sslv23_server);
    srv.use_certificate_file("./certs/server.crt", boost::asio::ssl::context::pem);
    srv.use_private_key_file("./certs/server.key", boost::asio::ssl::context::pem);
    srv.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once);
    srv.load_verify_file("./certs/ca.crt");

    boost::asio::ssl::stream<tubus::socket> server(tubus::socket(g_reactor.io, 1234567890), srv);

    boost::asio::ssl::context clt(boost::asio::ssl::context::sslv23);
    clt.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::sslv23_client);
    clt.use_certificate_file("./certs/client.crt", boost::asio::ssl::context::pem);
    clt.use_private_key_file("./certs/client.key", boost::asio::ssl::context::pem);
    clt.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
    clt.load_verify_file("./certs/ca.crt");

    boost::asio::ssl::stream<tubus::socket> client(tubus::socket(g_reactor.io, 1234567890), clt);

    boost::system::error_code ec;
    server.lowest_layer().bind(se, ec);
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    client.lowest_layer().bind(ce, ec);
    BOOST_REQUIRE_EQUAL(ec, NO_ERROR);

    std::promise<void> sp;
    std::future<void> sf = sp.get_future();

    std::promise<void> cp;
    std::future<void> cf = cp.get_future();

    stream_source source;
    stream_sink sink;

    server.lowest_layer().async_accept(ce, [&](const boost::system::error_code& error)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);
        BOOST_CHECK_NO_THROW(server.handshake(boost::asio::ssl::stream_base::server));

        tubus::mutable_buffer mb(stream_source::chunk_size);

        size_t size = 0;
        BOOST_CHECK_NO_THROW(size = boost::asio::read(server, mb));
        BOOST_CHECK_EQUAL(stream_source::chunk_size, size);
        BOOST_CHECK_NO_THROW(sink.write_next(mb));

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(stream_source::chunk_size, boost::asio::read(server, mb, ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);
        BOOST_CHECK_NO_THROW(sink.write_next(mb));

        boost::asio::async_read(server, mb, [&, mb](const boost::system::error_code& error, size_t size)
        {
            BOOST_CHECK_EQUAL(error, NO_ERROR);
            BOOST_CHECK_EQUAL(stream_source::chunk_size, size);
            BOOST_CHECK_NO_THROW(sink.write_next(mb));

            boost::system::error_code code;
            server.shutdown(code);
            BOOST_CHECK_EQUAL(code, NO_ERROR);

            sp.set_value();
        });
    });

    client.lowest_layer().async_connect(se, [&](const boost::system::error_code& error)
    {
        BOOST_CHECK_EQUAL(error, NO_ERROR);
        BOOST_CHECK_NO_THROW(client.handshake(boost::asio::ssl::stream_base::client));

        size_t size = 0;
        BOOST_CHECK_NO_THROW(size = boost::asio::write(client, source.read_next()));
        BOOST_CHECK_EQUAL(stream_source::chunk_size, size);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(stream_source::chunk_size, boost::asio::write(client, source.read_next(), ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);

        boost::asio::async_write(client, source.read_next(), [&](const boost::system::error_code& error, size_t size)
        {
            BOOST_CHECK_EQUAL(error, NO_ERROR);
            BOOST_CHECK_EQUAL(stream_source::chunk_size, size);

            boost::system::error_code code;
            client.shutdown(code);
            BOOST_CHECK_EQUAL(code, NO_ERROR);

            cp.set_value();
        });
    });

    BOOST_CHECK_NO_THROW(sf.get());
    BOOST_CHECK_NO_THROW(cf.get());

    BOOST_CHECK_EQUAL(source.read(), sink.written());

    server.lowest_layer().shutdown(ec);
    BOOST_CHECK_EQUAL(ec, NO_ERROR);

    client.lowest_layer().shutdown(ec);
    BOOST_CHECK_EQUAL(ec, NO_ERROR);
}

BOOST_AUTO_TEST_CASE(acceptor)
{
    tubus::endpoint se(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    tubus::endpoint c1(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    tubus::endpoint c2(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus::acceptor server(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(server.bind(se));

    std::promise<void> ap1;
    std::future<void> af1 = ap1.get_future();

    tubus::socket peer1(g_reactor.io);
    server.async_accept(peer1, [&](const boost::system::error_code& code)
    {
        BOOST_CHECK_EQUAL(code, NO_ERROR);
        BOOST_CHECK_EQUAL(peer1.remote_endpoint(), c1);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(4096, boost::asio::read(peer1, tubus::mutable_buffer(4096), ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);
        BOOST_CHECK_EQUAL(peer1.available(), 0);

        ap1.set_value();
    });

    std::promise<void> ap2;
    std::future<void> af2 = ap2.get_future();

    tubus::socket peer2(g_reactor.io);
    server.async_accept(peer2, [&](const boost::system::error_code& code)
    {
        BOOST_CHECK_EQUAL(code, NO_ERROR);
        BOOST_CHECK_EQUAL(peer2.remote_endpoint(), c2);

        boost::system::error_code ec;
        BOOST_CHECK_EQUAL(1024, boost::asio::read(peer2, tubus::mutable_buffer(1024), ec));
        BOOST_CHECK_EQUAL(ec, NO_ERROR);
        BOOST_CHECK_EQUAL(peer2.available(), 0);

        ap2.set_value();
    });

    tubus::socket client1(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(client1.bind(c1));
    BOOST_REQUIRE_NO_THROW(client1.connect(se));

    tubus::socket client2(g_reactor.io);
    BOOST_REQUIRE_NO_THROW(client2.bind(c2));
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
