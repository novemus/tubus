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
#include <list>
#include <deque>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <numeric>
#include <iostream>
#include <type_traits>
#include <vector>
#include <boost/asio.hpp>
#include <boost/shared_array.hpp>

namespace tubus {

struct const_buffer : public boost::asio::const_buffer
{
    typedef const_buffer value_type;
    typedef const const_buffer* const_iterator;

    const_buffer() noexcept(true)
    {
    }

    const_buffer(const const_buffer& other) noexcept(true)
        : boost::asio::const_buffer(other)
        , m_array(other.m_array)
    {
    }

    const_buffer(const boost::asio::const_buffer& other) noexcept(true)
        : const_buffer(boost::shared_array<uint8_t>(static_cast<uint8_t*>(const_cast<void*>(other.data())), [](uint8_t*){}), other.size())
    {
    }

    const_buffer(const std::string& str) noexcept(true)
        : const_buffer(boost::shared_array<uint8_t>(new uint8_t[str.size()]), str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    const_buffer(boost::shared_array<uint8_t> array, size_t size) noexcept(true)
        : const_buffer(array, 0, size)
    {
    }

    const_buffer(boost::shared_array<uint8_t> array, size_t offset, size_t size) noexcept(true)
        : boost::asio::const_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    const_iterator begin() const { return this; }

    const_iterator end() const { return this + 1; }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    const_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("const_buffer::slice: out of range");

        size_t offset = (uint8_t*)data() - m_array.get();
        return const_buffer(m_array, offset + pos, len);
    }

    inline const_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::pop_front: out of range");

        size_t offset = (uint8_t*)data() - m_array.get();
        *this += len;

        return const_buffer(m_array, offset, len);
    }

    inline const_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::pop_back: out of range");

        *this = const_buffer(m_array, (uint8_t*)data() - m_array.get(), size() - len);

        return const_buffer(m_array, (uint8_t*)data() - m_array.get() + size(), len);
    }

    template<class type> type get(size_t pos) const noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("const_buffer::get: out of range");

        return *(const type*)((uint8_t*)data() + pos);
    }

    inline void copy(size_t pos, size_t len, void* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("const_buffer::copy: out of range");

        std::memcpy(dst, (uint8_t*)data() + pos, len);
    }

    inline void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::truncate: out of range");

        *this = const_buffer(m_array, (uint8_t*)data() - m_array.get(), len);
    }

    inline void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::crop: out of range");

        *this += len;
    }

private:

    boost::shared_array<uint8_t> m_array;
};

struct mutable_buffer : public boost::asio::mutable_buffer
{
    typedef mutable_buffer value_type;
    typedef const mutable_buffer* const_iterator;

    mutable_buffer() noexcept(true)
    {
    }

    mutable_buffer(size_t size) noexcept(true)
        : mutable_buffer(boost::shared_array<uint8_t>(new uint8_t[size]), size)
    {
    }

    mutable_buffer(const mutable_buffer& other) noexcept(true)
        : boost::asio::mutable_buffer(other)
        , m_array(other.m_array)
    {
    }

    mutable_buffer(const boost::asio::mutable_buffer& other) noexcept(true)
        : mutable_buffer(boost::shared_array<uint8_t>(static_cast<uint8_t*>(other.data()), [](uint8_t*){}), other.size())
    {
    }

    mutable_buffer(const std::vector<uint8_t>& data) noexcept(true)
        : mutable_buffer(boost::shared_array<uint8_t>(new uint8_t[data.size()]), data.size())
    {
        std::memcpy(m_array.get(), data.data(), data.size());
    }

    mutable_buffer(const std::string& str) noexcept(true)
        : mutable_buffer(boost::shared_array<uint8_t>(new uint8_t[str.size()]), str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    mutable_buffer(boost::shared_array<uint8_t> array, size_t size) noexcept(true)
        : mutable_buffer(array, 0, size)
    {
    }

    mutable_buffer(boost::shared_array<uint8_t> array, size_t offset, size_t size) noexcept(true)
        : boost::asio::mutable_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    const_iterator begin() const noexcept(true) { return this; }

    const_iterator end() const noexcept(true) { return this + 1; }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    mutable_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("mutable_buffer::slice: out of range");

        return mutable_buffer(m_array, (uint8_t*)data() - m_array.get() + pos, len);
    }

    inline mutable_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_front: out of range");

        size_t offset = (uint8_t*)data() - m_array.get();
        *this += len;

        return mutable_buffer(m_array, offset, len);
    }

    inline mutable_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_back: out of range");

        *this = mutable_buffer(m_array, (uint8_t*)data() - m_array.get(), size() - len);

        return mutable_buffer(m_array, (uint8_t*)data() - m_array.get() + size(), len);
    }

    template<class type> type get(size_t pos) const noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("mutable_buffer::get: out of range");

        return *(type*)((uint8_t*)data() + pos);
    }

    template<class type> void set(size_t pos, type val) noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("mutable_buffer::set: out of range");

        *(type*)((uint8_t*)data() + pos) = val;
    }

    inline void fill(size_t pos, size_t len, const void* src) noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::fill: out of range");
        
        std::memcpy((uint8_t*)data() + pos, src, len);
    }

    inline void copy(size_t pos, size_t len, void* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::copy: out of range");
        
        std::memcpy(dst, (uint8_t*)data() + pos, len);
    }

    inline void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::crop: out of range");
        
        *this += len;
    }

    inline void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::truncate: out of range");

        *this = mutable_buffer(m_array, (uint8_t*)data() - m_array.get(), len);
    }

    inline operator const const_buffer&() const noexcept(true)
    {
        return reinterpret_cast<const const_buffer&>(*this);
    }

private:

    boost::shared_array<uint8_t> m_array;

};

class buffer_factory : public std::enable_shared_from_this<buffer_factory>
{
    typedef std::weak_ptr<buffer_factory> weak_ptr;
    typedef std::shared_ptr<buffer_factory> self_ptr;

    static void destroy(weak_ptr weak, uint8_t* ptr) noexcept(true)
    {
        self_ptr self = weak.lock();
        if (self)
        {
            if (self->cache(ptr))
                return;
        }
        delete[] ptr;
    }

    bool cache(uint8_t* ptr) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        static const size_t s_max_cache_size = 16;

        if (m_cache.size() < s_max_cache_size)
        {
            m_cache.emplace_back(ptr,
                std::bind(&buffer_factory::destroy, shared_from_this(), std::placeholders::_1)
                );
            return true;
        }

        return false;
    }

public:

    buffer_factory(size_t size) noexcept(true) : m_size(size)
    {
    }

    mutable_buffer obtain() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        boost::shared_array<uint8_t> array;
        if (m_cache.empty())
        {
            array.reset(
                new uint8_t[m_size],
                std::bind(&buffer_factory::destroy, shared_from_this(), std::placeholders::_1)
                );
        }
        else
        {
            array = m_cache.front();
            m_cache.pop_front();
        }

        return mutable_buffer(array, m_size);
    }

private:

    size_t m_size;
    std::list<boost::shared_array<uint8_t>> m_cache;
    std::mutex m_mutex;
};

}
