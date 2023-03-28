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

#include "export.h"
#include "buffer.h"
#include <deque>
#include <random>
#include <numeric>

#ifdef _MSC_VER
#include <WinSock2.h>
#define htobe64 htonll
#define be64toh ntohll
#endif

namespace tubus {

struct section : public mutable_buffer
{
    static constexpr size_t header_size = sizeof(uint16_t) * 2;

    typedef const_buffer value_type;
    typedef const const_buffer* const_iterator;

    enum flag
    {
        echo = 1,
        link = 2,
        tear = 4,
        ping = 6,
        move = 8
    };

    explicit section()
    {
    }

    explicit section(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    void stub()
    {
        std::memset(data(), 0, std::min(header_size, size()));
    }

    void simple(uint16_t type)
    {
        set<uint16_t>(0, htons(type));
        set<uint16_t>(sizeof(uint16_t), 0);
    }

    void cursor(uint64_t handle)
    {
        set<uint16_t>(0, htons(flag::move | flag::echo));
        set<uint16_t>(sizeof(uint16_t), htons(sizeof(handle)));
        set<uint64_t>(header_size, htobe64(handle));
    }

    void snippet(uint64_t handle, const const_buffer& data)
    {
        set<uint16_t>(0, htons(flag::move));
        set<uint16_t>(sizeof(uint16_t), htons(static_cast<uint16_t>(sizeof(handle) + data.size())));
        set<uint64_t>(header_size, htobe64(handle));
        fill(header_size + sizeof(handle), data.size(), data.data());
    }

    uint16_t type() const
    {
        return size() >= header_size ? ntohs(get<uint16_t>(0)) : 0;
    }

    uint16_t length() const
    {
        return size() >= header_size ? ntohs(get<uint16_t>(sizeof(uint16_t))) : 0;
    }

    mutable_buffer value() const
    {
        return slice(std::min(header_size, size()), length());
    }

    void advance()
    {
        crop(std::min(header_size, size()) + length());
    }
};

struct cursor : public mutable_buffer
{
    static constexpr size_t handle_size = sizeof(uint64_t);
    
    explicit cursor(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t handle() const
    {
        return be64toh(get<uint64_t>(0));
    }
};

struct snippet : public mutable_buffer
{
    static constexpr uint16_t handle_size = sizeof(uint64_t);

    explicit snippet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t handle() const
    {
        return be64toh(get<uint64_t>(0));
    }

    mutable_buffer fragment() const
    {
        return slice(handle_size, size() - handle_size);
    }
};

struct packet : public mutable_buffer
{
    static constexpr size_t packet_sign = 0x0909;
    static constexpr size_t packet_version = 0x0100;
    static constexpr size_t header_size = 16;

    packet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t salt() const
    {
        return size() > packet::header_size ? be64toh(get<uint64_t>(0)) : 0;
    }

    uint16_t sign() const
    {
        return size() > packet::header_size ? ntohs(get<uint16_t>(sizeof(uint64_t))) : 0;
    }

    uint16_t version() const
    {
        return size() > packet::header_size ? ntohs(get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t))) : 0;
    }

    uint32_t pin() const
    {
        return size() > packet::header_size ? ntohl(get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2)) : 0;
    }

    section body() const
    {
        return section(slice(std::min(size(), header_size), size() - std::min(size(), header_size)));
    }

    section stub() const
    {
        section sect(slice(std::min(size(), header_size), size() - std::min(size(), header_size)));
        while (sect.type() != 0)
        {
            sect.crop(section::header_size + sect.length());
        }
        return sect;
    }

    void trim()
    {
        truncate((uint8_t*)stub().data() - (uint8_t*)data());
    }
};

struct dimmer
{
    static mutable_buffer invert(uint64_t secret, const mutable_buffer& buffer)
    {
        uint8_t* ptr = (uint8_t*)buffer.data();
        uint8_t* end = ptr + buffer.size();

        uint64_t salt = *(uint64_t*)ptr;
        bool dim = salt == 0;
        if (dim)
        {
            std::random_device dev;
            std::mt19937_64 gen(dev());
            salt = static_cast<uint64_t>(gen());
            *(uint64_t*)ptr = htobe64(salt ^ secret);
        }
        else
        {
            salt = be64toh(salt) ^ secret;
            *(uint64_t*)ptr = 0;
        }

        ptr += sizeof(uint64_t);

        uint64_t inverter = make_inverter(secret, salt, dim);
        while (ptr + sizeof(uint64_t) <= end)
        {
            *(uint64_t*)ptr ^= inverter;
            inverter = make_inverter(inverter, salt, dim);
            ptr += sizeof(uint64_t);
        }

        uint8_t* inv = (uint8_t*)&inverter;
        while (ptr < end)
        {
            *ptr ^= *inv;
            ++ptr;
            ++inv;
        }

        return buffer;
    }

private:

    static inline uint64_t make_inverter(uint64_t secret, uint64_t salt, bool dim)
    {
        uint64_t base = secret + salt;
        uint64_t shift = (base & 0x3F) | 0x01;
        return dim ? htobe64(((base >> shift) | (base << (64 - shift))) ^ salt) : be64toh(((base >> shift) | (base << (64 - shift))) ^ salt);
    }
};

}
