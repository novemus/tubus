/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(tubus_cursor)
{
    tubus::mutable_buffer mb(8);
    std::memset(mb.data(), 0, mb.size());

    auto curs = tubus::cursor(mb);

    BOOST_CHECK_EQUAL(curs.size(), 8);
    BOOST_CHECK_EQUAL(curs.handle(), 0);
}

BOOST_AUTO_TEST_CASE(snippet)
{
    tubus::mutable_buffer mb(16);
    std::memset(mb.data(), 0, mb.size());

    tubus::snippet snip(mb);
    BOOST_CHECK_EQUAL(snip.size(), 16);
    BOOST_CHECK_EQUAL(snip.handle(), 0);
    BOOST_CHECK_EQUAL(snip.fragment().size(), 8);
    BOOST_CHECK_EQUAL(snip.fragment().data(), (uint8_t*)mb.data() + 8);
}

BOOST_AUTO_TEST_CASE(tubus_section)
{
    tubus::mutable_buffer mb(1024);
    tubus::section sect(mb);

    BOOST_CHECK_EQUAL(sect.size(), 1024);

    sect.cursor(12345);

    BOOST_CHECK_EQUAL(sect.type(), tubus::section::flag::move | tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(sect.value().size(), tubus::cursor::handle_size);

    tubus::cursor curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(curs.handle(), 12345);

    sect.advance();

    tubus::const_buffer cb("hello, tubus");
    sect.snippet(9, cb);

    BOOST_CHECK_EQUAL(sect.type(), tubus::section::move);
    BOOST_CHECK_EQUAL(sect.length(), tubus::snippet::handle_size + cb.size());
    
    tubus::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), tubus::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 9);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    sect.simple(tubus::section::link);

    BOOST_CHECK_EQUAL(sect.type(), tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    BOOST_CHECK_EQUAL(sect.size(), 1024 - tubus::section::header_size * 3 - curs.size() - snip.size());
}

BOOST_AUTO_TEST_CASE(tubus_packet)
{
    tubus::mutable_buffer mb(1024);
    tubus::packet pack(mb);

    pack.set<uint64_t>(0, 0);
    pack.set<uint16_t>(sizeof(uint64_t), htons(tubus::packet::packet_sign));
    pack.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(tubus::packet::packet_version));
    pack.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(12345));
    pack.set<uint32_t>(tubus::packet::header_size, 0);

    BOOST_CHECK_EQUAL(pack.size(), mb.size());
    BOOST_CHECK_EQUAL(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 12345);

    auto sect = pack.body();

    tubus::const_buffer cb("hello, tubus");
    sect.snippet(12345, cb);

    sect.advance();
    sect.cursor(12345);

    sect.advance();
    sect.simple(tubus::section::link);

    sect.advance();
    sect.stub();

    pack.trim();

    BOOST_CHECK_EQUAL(pack.size(), tubus::packet::header_size + tubus::section::header_size * 3 + tubus::cursor::handle_size + tubus::snippet::handle_size + cb.size());

    sect = pack.body();
    tubus::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), tubus::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 12345);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    tubus::cursor curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(curs.handle(), 12345);

    sect.advance();

    BOOST_CHECK_EQUAL(sect.type(), tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    auto stub = pack.stub();

    BOOST_CHECK_EQUAL(stub.data(), sect.data());

    tubus::mutable_buffer copy(pack.size());
    copy.fill(0, copy.size(), pack.data());

    copy = tubus::dimmer::invert(1234567890, copy);
    copy = tubus::dimmer::invert(1234567890, copy);

    BOOST_CHECK_EQUAL(pack.size(), copy.size());
    BOOST_CHECK_EQUAL(std::memcmp(pack.data(), copy.data(), copy.size()), 0);
}
