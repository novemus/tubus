/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#define BOOST_TEST_MODULE tubus_tests

#include "../buffer.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(buffer);

BOOST_AUTO_TEST_CASE(mutable_buffer)
{
    BOOST_REQUIRE_NO_THROW(tubus::mutable_buffer());
    BOOST_CHECK_EQUAL(tubus::mutable_buffer().size(), 0);

    const char* greet = "hello, tubus";
    const char* hello = "hello";
    const char* tubus = "tubus";

    tubus::mutable_buffer mb(greet);

    BOOST_CHECK(mb.unique());
    BOOST_CHECK_EQUAL(mb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet, mb.size()), 0);

    // fill, copy, get, set
    tubus::mutable_buffer sb = mb.slice(7, 5);

    BOOST_CHECK(!mb.unique());
    BOOST_CHECK(!sb.unique());
    BOOST_CHECK_EQUAL(sb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(sb.data(), greet + 7, sb.size()), 0);

    mb.fill(0, strlen(tubus), (const uint8_t*)tubus);
    mb.fill(7, strlen(hello), (const uint8_t*)hello);

    BOOST_CHECK_EQUAL(mb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp((uint8_t*)mb.data() + 7, hello, strlen(hello)), 0);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), tubus, strlen(tubus)), 0);

    mb.copy(0, 5, sb.data());

    BOOST_CHECK_EQUAL(std::memcmp(sb.data(), mb.data(), 5), 0);

    mb.set<uint8_t>(0, 9);
    mb.set<uint16_t>(1, 99);
    mb.set<uint32_t>(3, 999);

    BOOST_CHECK_EQUAL(mb.get<uint8_t>(0), 9);
    BOOST_CHECK_EQUAL(mb.get<uint16_t>(1), 99);
    BOOST_CHECK_EQUAL(mb.get<uint32_t>(3), 999);
    
    mb.set<uint64_t>(0, 9999);
    BOOST_CHECK_EQUAL(mb.get<uint64_t>(0), 9999);

    BOOST_REQUIRE_THROW(mb.get<uint8_t>(12), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.get<uint32_t>(10), std::runtime_error);

    BOOST_REQUIRE_THROW(mb.set<uint8_t>(12, 9), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.set<uint32_t>(10, 999), std::runtime_error);

    sb = tubus::mutable_buffer(0);

    // slice
    mb = mb.slice(0, 5);

    BOOST_CHECK_EQUAL(mb.size(), 5);

    mb = mb.slice(1, 4);

    BOOST_CHECK_EQUAL(mb.size(), 4);
    BOOST_REQUIRE_THROW(mb.slice(0, 5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 1), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 5), std::runtime_error);

    mb = mb.slice(0, 0);

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK(mb.unique());

    // pop
    mb = tubus::mutable_buffer(greet);
    auto pb = mb.pop_front(5);

    BOOST_CHECK_EQUAL(mb.size(), strlen(greet) - 5);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet + 5, mb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    pb = mb.pop_back(5);

    BOOST_CHECK_EQUAL(mb.size(), strlen(greet) - 10);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet + 5, mb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 7, pb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_back(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_REQUIRE_THROW(pb.pop_front(pb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(pb.pop_back(pb.size() + 1), std::runtime_error);

    // truncate, crop
    mb = tubus::mutable_buffer(greet);
    
    pb = mb.pop_back(mb.size());
    pb.truncate(5);

    BOOST_CHECK_EQUAL(pb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);

    pb.crop(3);

    BOOST_CHECK_EQUAL(pb.size(), 2);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 3, pb.size()), 0);

    BOOST_REQUIRE_THROW(mb.truncate(mb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.crop(mb.size() + 1), std::runtime_error);

    mb.truncate(mb.size());
    pb.crop(pb.size());

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 0);

    BOOST_CHECK(!mb.unique());
    BOOST_CHECK(!pb.unique());
}

BOOST_AUTO_TEST_CASE(const_buffer)
{
    BOOST_REQUIRE_NO_THROW(tubus::const_buffer());
    BOOST_CHECK_EQUAL(tubus::const_buffer().size(), 0);

    const char* greet = "hello, tubus";

    tubus::const_buffer cb(greet);

    BOOST_CHECK(cb.unique());
    BOOST_CHECK_EQUAL(cb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet, cb.size()), 0);

    // copy, get
    tubus::const_buffer sb = cb.slice(4, 8);

    BOOST_CHECK(!cb.unique());
    BOOST_CHECK(!sb.unique());
    BOOST_CHECK_EQUAL(sb.size(), 8);

    BOOST_CHECK_EQUAL(cb.get<uint8_t>(4), sb.get<uint8_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint16_t>(4), sb.get<uint16_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint32_t>(4), sb.get<uint32_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint64_t>(4), sb.get<uint64_t>(0));

    uint8_t copy[5];
    cb.copy(0, 5, copy);

    BOOST_CHECK_EQUAL(std::memcmp(copy, cb.data(), 5), 0);

    BOOST_REQUIRE_THROW(cb.get<uint8_t>(12), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.get<uint32_t>(10), std::runtime_error);

    sb = tubus::const_buffer();

    // slice
    cb = cb.slice(0, 5);

    BOOST_CHECK_EQUAL(cb.size(), 5);

    cb = cb.slice(1, 4);

    BOOST_CHECK_EQUAL(cb.size(), 4);
    BOOST_REQUIRE_THROW(cb.slice(0, 5), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.slice(5, 1), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.slice(5, 5), std::runtime_error);

    cb = cb.slice(0, 0);

    BOOST_CHECK_EQUAL(cb.size(), 0);
    BOOST_CHECK(cb.unique());

    // pop
    cb = tubus::const_buffer(greet);
    auto pb = cb.pop_front(5);

    BOOST_CHECK_EQUAL(cb.size(), strlen(greet) - 5);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet + 5, cb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    pb = cb.pop_back(5);

    BOOST_CHECK_EQUAL(cb.size(), strlen(greet) - 10);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet + 5, cb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 7, pb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_back(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_REQUIRE_THROW(pb.pop_front(pb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(pb.pop_back(pb.size() + 1), std::runtime_error);

    // truncate, crop
    cb = tubus::const_buffer(greet);
    
    pb = cb.pop_back(cb.size());
    pb.truncate(5);

    BOOST_CHECK_EQUAL(pb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);

    pb.crop(3);

    BOOST_CHECK_EQUAL(pb.size(), 2);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 3, pb.size()), 0);

    BOOST_REQUIRE_THROW(cb.truncate(cb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.crop(cb.size() + 1), std::runtime_error);

    cb.truncate(cb.size());
    pb.crop(pb.size());

    BOOST_CHECK_EQUAL(cb.size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 0);

    BOOST_CHECK(!cb.unique());
    BOOST_CHECK(!pb.unique());
}

BOOST_AUTO_TEST_CASE(buffer_factory)
{
    auto factory = std::make_shared<tubus::buffer_factory>(16384);

    void* ptr1 = 0;
    void* ptr2 = 0;
    {
        auto buf = factory->obtain();
        ptr1 = buf.data();

        BOOST_CHECK(ptr1);
    }

    auto buf = factory->obtain();
    ptr2 = buf.data();

    BOOST_CHECK_EQUAL(ptr1, ptr2);

    auto buf1 = factory->obtain();
    auto buf2 = factory->obtain();
    auto buf3 = factory->obtain();

    BOOST_CHECK(buf1.data() != buf2.data() && buf1.data() != buf3.data() && buf2.data() != buf3.data());
}

BOOST_AUTO_TEST_SUITE_END();
