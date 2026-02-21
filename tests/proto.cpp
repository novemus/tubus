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
#include <tubus/udp/proto.h>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(utils)

BOOST_AUTO_TEST_CASE(inverter)
{
    uint64_t secret = 987654321UL;
    uint64_t salt = 192837465;
    uint64_t sign = 0x09090001;

    tubus::mutable_buffer mb(16);
    uint64_t* first = static_cast<uint64_t*>(mb.data());
    uint64_t* second = first + 1;

    uint64_t hide = tubus::make_inverter(secret, salt, true);
    uint64_t show = tubus::make_inverter(secret, salt, false);

    *first = htobe64(salt ^ secret);
    *second = htobe64(sign ^ hide);

    BOOST_CHECK_EQUAL(be64toh(*first) ^ secret, salt);
    BOOST_CHECK_EQUAL(be64toh(*second) ^ show, sign);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(udp_proto)

BOOST_AUTO_TEST_CASE(numeral)
{
    tubus::mutable_buffer mb(8);
    std::memset(mb.data(), 0, mb.size());

    auto curs = tubus::udp::proto::numeral(mb);

    BOOST_CHECK_EQUAL(curs.size(), 8);
    BOOST_CHECK_EQUAL(curs.value(), 0);
}

BOOST_AUTO_TEST_CASE(snippet)
{
    tubus::mutable_buffer mb(16);
    std::memset(mb.data(), 0, mb.size());

    tubus::udp::proto::snippet snip(mb);
    BOOST_CHECK_EQUAL(snip.size(), 16);
    BOOST_CHECK_EQUAL(snip.handle(), 0);
    BOOST_CHECK_EQUAL(snip.fragment().size(), 8);
    BOOST_CHECK_EQUAL(snip.fragment().data(), (uint8_t*)mb.data() + 8);
}

BOOST_AUTO_TEST_CASE(section)
{
    tubus::mutable_buffer mb(1024);
    tubus::udp::proto::section sect(mb);

    BOOST_CHECK_EQUAL(sect.size(), 1024);

    sect.numeral(tubus::udp::proto::section::move | tubus::udp::proto::section::echo, 12345);

    BOOST_CHECK_EQUAL(sect.type(), tubus::udp::proto::section::move | tubus::udp::proto::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), tubus::udp::proto::numeral::value_size);
    BOOST_CHECK_EQUAL(sect.value().size(), tubus::udp::proto::numeral::value_size);

    tubus::udp::proto::numeral curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), tubus::udp::proto::numeral::value_size);
    BOOST_CHECK_EQUAL(curs.value(), 12345);

    sect.advance();

    tubus::const_buffer cb("hello, tubus");
    sect.snippet(9, cb);

    BOOST_CHECK_EQUAL(sect.type(), tubus::udp::proto::section::move);
    BOOST_CHECK_EQUAL(sect.length(), tubus::udp::proto::snippet::handle_size + cb.size());
    
    tubus::udp::proto::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), tubus::udp::proto::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 9);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    sect.simple(tubus::udp::proto::section::link);

    BOOST_CHECK_EQUAL(sect.type(), tubus::udp::proto::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    BOOST_CHECK_EQUAL(sect.size(), 1024 - tubus::udp::proto::section::header_size * 3 - curs.size() - snip.size());
}

BOOST_AUTO_TEST_CASE(packet)
{
    tubus::mutable_buffer mb(1024);
    tubus::udp::proto::packet pack(mb);

    pack.set<uint64_t>(0, 0);
    pack.set<uint16_t>(sizeof(uint64_t), htons(tubus::udp::proto::packet::packet_sign));
    pack.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(tubus::udp::proto::packet::packet_version));
    pack.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(12345));
    pack.set<uint32_t>(tubus::udp::proto::packet::header_size, 0);

    BOOST_CHECK_EQUAL(pack.size(), mb.size());
    BOOST_CHECK_EQUAL(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), tubus::udp::proto::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), tubus::udp::proto::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 12345);

    auto sect = pack.body();

    tubus::const_buffer cb("hello, tubus");
    sect.snippet(12345, cb);

    sect.advance();
    sect.numeral(tubus::udp::proto::section::move | tubus::udp::proto::section::echo, 12345);

    sect.advance();
    sect.simple(tubus::udp::proto::section::link);

    sect.advance();
    sect.stub();

    pack.trim();

    BOOST_CHECK_EQUAL(pack.size(), tubus::udp::proto::packet::header_size + tubus::udp::proto::section::header_size * 3 + tubus::udp::proto::numeral::value_size + tubus::udp::proto::snippet::handle_size + cb.size());

    sect = pack.body();
    tubus::udp::proto::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), tubus::udp::proto::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 12345);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    tubus::udp::proto::numeral curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), tubus::udp::proto::numeral::value_size);
    BOOST_CHECK_EQUAL(curs.value(), 12345);

    sect.advance();

    BOOST_CHECK_EQUAL(sect.type(), tubus::udp::proto::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    auto stub = pack.stub();

    BOOST_CHECK_EQUAL(stub.data(), sect.data());

    tubus::mutable_buffer copy(pack.size());
    copy.fill(0, copy.size(), pack.data());

    copy = tubus::udp::proto::packet::invert(1234567890, copy);
    copy = tubus::udp::proto::packet::invert(1234567890, copy);

    BOOST_CHECK_EQUAL(pack.size(), copy.size());
    BOOST_CHECK_EQUAL(std::memcmp(pack.data(), copy.data(), copy.size()), 0);
}

BOOST_AUTO_TEST_SUITE_END()
