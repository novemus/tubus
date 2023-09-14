/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "packet.h"
#include "channel.h"
#include <map>
#include <set>
#include <list>
#include <atomic>
#include <iostream>
#include <random>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>

#ifndef TUBUS_MAX_PACKET_SIZE
#define TUBUS_MAX_PACKET_SIZE 1432u
#endif

#ifndef TUBUS_PING_TIMEOUT
#define TUBUS_PING_TIMEOUT 30l
#endif

#ifndef TUBUS_SHUTDOWN_TIMEOUT
#define TUBUS_SHUTDOWN_TIMEOUT 2000l
#endif

#ifndef TUBUS_CONNECT_TIMEOUT
#define TUBUS_CONNECT_TIMEOUT 30l
#endif

#ifndef TUBUS_ACCEPT_TIMEOUT
#define TUBUS_ACCEPT_TIMEOUT 30l
#endif

#ifndef TUBUS_SNIPPET_FLIGHT
#define TUBUS_SNIPPET_FLIGHT 48u
#endif

#ifndef TUBUS_MOVE_ATTEMPTS
#define TUBUS_MOVE_ATTEMPTS 32l
#endif

#ifndef TUBUS_RECEIVE_BUFFER_SIZE
#define TUBUS_RECEIVE_BUFFER_SIZE 5ull * 1024 * 1024
#endif

#ifndef TUBUS_SEND_BUFFER_SIZE
#define TUBUS_SEND_BUFFER_SIZE 5ull * 1024 * 1024
#endif

namespace tubus {

const boost::posix_time::ptime g_zero_time(boost::gregorian::date(1970, 1, 1));

mutable_buffer create_buffer(size_t size) noexcept(true)
{
    static std::mutex s_mutex;
    static std::map<size_t, std::shared_ptr<buffer_factory>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto res = s_pool.emplace(size, std::make_shared<buffer_factory>(size));
    return res.first->second->obtain();
}

template<class value_type> value_type getenv(const std::string& name, const value_type& def) noexcept(true)
{
    try
    {
        const char *env = std::getenv(name.c_str());
        return env ? boost::lexical_cast<value_type>(env) : def;
    }
    catch (const boost::bad_lexical_cast& ex)
    {
        std::cerr << ex.what() << std::endl;
    }

    return def;
}

class transport : public channel, public std::enable_shared_from_this<transport>
{
    enum state 
    { 
        neither,
        initial,
        accepting,
        connecting,
        linked,
        shutting,
        tearing,
        finished
    };

    inline static size_t max_packet_size() noexcept(true)
    {
        static size_t s_size(getenv("TUBUS_MAX_PACKET_SIZE", TUBUS_MAX_PACKET_SIZE));
        return s_size;
    }

    inline static boost::posix_time::time_duration ping_timeout() noexcept(true)
    {
        static boost::posix_time::seconds s_timeout(getenv("TUBUS_PING_TIMEOUT", TUBUS_PING_TIMEOUT));
        return s_timeout;
    }

    inline static boost::posix_time::time_duration shutdown_timeout() noexcept(true)
    {
        static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_SHUTDOWN_TIMEOUT", TUBUS_SHUTDOWN_TIMEOUT));
        return s_timeout;
    }

    inline static boost::posix_time::time_duration connect_timeout() noexcept(true)
    {
        static boost::posix_time::seconds s_timeout(getenv("TUBUS_CONNECT_TIMEOUT", TUBUS_CONNECT_TIMEOUT));
        return s_timeout;
    }

    inline static boost::posix_time::time_duration accept_timeout() noexcept(true)
    {
        static boost::posix_time::seconds s_timeout(getenv("TUBUS_ACCEPT_TIMEOUT", TUBUS_ACCEPT_TIMEOUT));
        return s_timeout;
    }

    inline static size_t snippet_flight() noexcept(true)
    {
        static size_t s_flight(getenv("TUBUS_SNIPPET_FLIGHT", TUBUS_SNIPPET_FLIGHT));
        return s_flight;
    }

    inline static size_t move_attempts() noexcept(true)
    {
        static size_t s_attempts(getenv("TUBUS_MOVE_ATTEMPTS", TUBUS_MOVE_ATTEMPTS));
        return s_attempts;
    }

    inline static size_t receive_buffer_size() noexcept(true)
    {
        static size_t s_size(getenv("TUBUS_RECEIVE_BUFFER_SIZE", TUBUS_RECEIVE_BUFFER_SIZE));
        return s_size;
    }

    inline static size_t send_buffer_size() noexcept(true)
    {
        static size_t s_size(getenv("TUBUS_SEND_BUFFER_SIZE", TUBUS_SEND_BUFFER_SIZE));
        return s_size;
    }

    struct connector
    {
        static uint16_t make_pin() noexcept(true)
        {
            static std::atomic<uint16_t> s_pin;
            uint16_t pin = ++s_pin;
            return pin > 0 ? pin : make_pin();
        };

        connector(boost::asio::io_context& io, boost::posix_time::time_duration& repeat) noexcept(true)
            : m_io(io)
            , m_repeat(repeat)
            , m_status(state::neither)
            , m_local(0)
            , m_remote(0)
            , m_seen(boost::posix_time::max_date_time)
            , m_dead(boost::posix_time::max_date_time)
        {
        }

        void init(const callback& fallback) noexcept(true)
        {
            on_error = fallback;
            m_status = state::initial;
        }

        void error(const boost::system::error_code& err) noexcept(true)
        {
            if (on_connect)
            {
                m_io.post(boost::bind(on_connect, err));
                on_connect = 0;
            }

            if (on_shutdown)
            {
                m_io.post(boost::bind(on_shutdown, err));
                on_shutdown = 0;
            }

            m_local = 0;
            m_remote = 0;
            m_seen = boost::posix_time::max_date_time;
            m_dead = boost::posix_time::max_date_time;
            m_jobs.clear();
            m_status = state::finished;
        }

        bool valid(const packet& pack) noexcept(true)
        {
            return pack.size() > packet::header_size 
                && pack.sign() == packet::packet_sign
                && pack.version() == packet::packet_version
                && m_local != 0 && (m_remote == 0 || m_remote == pack.pin());
        }

        void parse(const packet& pack) noexcept(true)
        {
            if (!valid(pack))
                return;

            m_seen = boost::posix_time::microsec_clock::universal_time();

            auto sect = pack.body();
            auto type = sect.type();

            while (type != 0)
            {
                switch (type)
                {
                    case section::link:
                    {
                        m_jobs.emplace(section::link | section::echo, g_zero_time);

                        if (m_status == state::accepting)
                        {
                            m_remote = pack.pin();
                        }
                        break;
                    }
                    case section::link | section::echo:
                    {
                        if (m_status == state::connecting)
                        {
                            m_status = state::linked;
                            m_dead = boost::posix_time::max_date_time;
                            m_remote = pack.pin();

                            m_jobs.erase(section::link);
                            m_jobs.emplace(section::ping, g_zero_time);

                            m_io.post(boost::bind(on_connect, boost::system::error_code()));
                            on_connect = 0;
                        }
                        break;
                    }
                    case section::tear:
                    {
                        m_jobs.emplace(section::tear | section::echo, g_zero_time);

                        if (m_status == state::linked)
                        {
                            m_status = state::tearing;
                            m_dead = m_seen + shutdown_timeout();
                        }
                        break;
                    }
                    case section::tear | section::echo:
                    {
                        if (m_status == state::shutting)
                        {
                            m_status = state::finished;
                            m_jobs.erase(section::tear);

                            m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                            on_shutdown = 0;
                        }
                        break;
                    } 
                    case section::ping:
                    {
                        numeral ping(sect.value());
                        m_jobs.emplace(section::ping | section::echo, g_zero_time + boost::posix_time::microseconds(ping.value()));
                        break;
                    }
                    case section::flag::ping | section::flag::echo:
                    {
                        numeral ping(sect.value());

                        size_t rtt = (m_seen - g_zero_time).total_microseconds() - ping.value();
                        m_repeat = boost::posix_time::microseconds(std::max(rtt * 2ul, 10000ul));

                        m_jobs.erase(section::ping);
                        m_jobs.emplace(section::ping, m_seen + ping_timeout());
                        break;
                    }
                    default: break;
                }

                sect.advance();
                type = sect.type();
            }
        }

        void imbue(packet& pack) noexcept(true)
        {
            pack.set<uint64_t>(0, 0);
            pack.set<uint16_t>(sizeof(uint64_t), htons(packet::packet_sign));
            pack.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(packet::packet_version));
            pack.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(m_local));
            pack.set<uint32_t>(packet::header_size, 0);

            auto now = boost::posix_time::microsec_clock::universal_time();
            if ((m_status == state::linked && m_seen + ping_timeout() < now - boost::posix_time::seconds(5)) || now > m_dead)
            {
                m_io.post(boost::bind(on_error, boost::asio::error::interrupted));
                return;
            }

            section sect = pack.stub();
            while (sect.size() >= section::header_size)
            {
                auto iter = std::find_if(m_jobs.begin(), m_jobs.end(), [now](const auto& item)
                {
                    return (item.first & section::echo) || item.second < now;
                });

                if (iter == m_jobs.end())
                    break;
 
                if (iter->first == section::ping)
                {
                    sect.numeral(section::ping, (now - g_zero_time).total_microseconds());
                }
                else if (iter->first == (section::ping | section::echo))
                {
                    sect.numeral(section::ping | section::echo, (iter->second - g_zero_time).total_microseconds());
                }
                else
                {
                    sect.simple(iter->first);
                }

                if (iter->first == (section::link | section::echo))
                {
                    m_jobs.erase(iter);

                    if (m_status == state::accepting)
                    {
                        m_status = state::linked;
                        m_dead = boost::posix_time::max_date_time;
                        m_io.post(boost::bind(on_connect, boost::system::error_code()));
                        on_connect = 0;
                    }
                }
                else if (iter->first == (section::ping | section::echo))
                {
                    m_jobs.erase(iter);
                }
                else if (iter->first == (section::tear | section::echo))
                {
                    m_jobs.erase(iter);
                    m_status = state::finished;

                    if (on_shutdown)
                    {
                        m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                        on_shutdown = 0;
                    }
                }
                else
                {
                    iter->second = now + m_repeat;
                }

                sect.advance();
            }

            sect.stub();
        }

        state status() const noexcept(true)
        {
            return m_status;
        }

        bool shutdown(const callback& handler) noexcept(true)
        {
            if (m_status == state::finished)
            {
                m_io.post(boost::bind(handler, boost::system::error_code()));
                return false;
            }

            if (m_status != state::linked && m_status != state::tearing)
            {
                boost::system::error_code error = m_status == state::shutting ? 
                    boost::asio::error::in_progress : boost::asio::error::not_connected;

                m_io.post(boost::bind(handler, error));
                return false;
            }

            on_shutdown = handler;

            m_dead = boost::posix_time::microsec_clock::universal_time() + shutdown_timeout();
            
            if (m_status != state::tearing)
            {
                m_status = state::shutting;
                m_jobs.emplace(section::tear, g_zero_time);
            }

            return true;
        }

        bool connect(const callback& handler) noexcept(true)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code error = m_status == state::connecting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::no_permission;

                m_io.post(boost::bind(handler, error));
                return false;
            }

            m_local = make_pin();
            on_connect = handler;

            m_dead = boost::posix_time::microsec_clock::universal_time() + connect_timeout();
            m_status = state::connecting;

            m_jobs.emplace(section::link, g_zero_time);

            return true;
        }

        bool accept(const callback& handler) noexcept(true)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code error = m_status == state::accepting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::no_permission;

                m_io.post(boost::bind(handler, error));
                return false;
            }

            m_local = make_pin();
            on_connect = handler;

            m_dead = boost::posix_time::microsec_clock::universal_time() + accept_timeout();
            m_status = state::accepting;

            return true;
        }

        bool deffered() const noexcept(true)
        {
            return !m_jobs.empty();
        }

    private:

        boost::asio::io_context& m_io;
        boost::posix_time::time_duration& m_repeat;
        state m_status;
        uint32_t m_local;
        uint32_t m_remote;
        std::map<uint16_t, boost::posix_time::ptime> m_jobs;
        boost::posix_time::ptime m_seen;
        boost::posix_time::ptime m_dead;
        callback on_error;
        callback on_connect;
        callback on_shutdown;
    };

    struct ostreamer
    {
        ostreamer(boost::asio::io_context& io, boost::posix_time::time_duration& repeat) noexcept(true)
            : m_io(io)
            , m_repeat(repeat)
        {
        }

        void init(const callback& fallback) noexcept(true)
        {
            on_error = fallback;
        }

        void error(const boost::system::error_code& err) noexcept(true)
        {
            auto cursor = m_moves.empty() ? m_buffer.head() : m_moves.begin()->first;

            auto iter = m_writers.begin();
            while (iter != m_writers.end())
            {
                size_t sent = 0;

                if (cursor >= iter->head + iter->size)
                    sent = iter->size;
                else if (cursor > iter->head) 
                    sent = cursor - iter->head;

                m_io.post(boost::bind(iter->callback, err, sent));
                ++iter;
            }

            m_writers.clear();
            m_moves.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack) noexcept(true)
        {
            auto sect = pack.body();
            auto type = sect.type();

            while (type != 0)
            {
                if (type == (section::move | section::echo))
                {
                    numeral curs(sect.value());
                    m_moves.erase(curs.value());

                    auto cursor = m_moves.empty() ? m_buffer.head() : m_moves.begin()->first;

                    auto iter = m_writers.begin();
                    while (iter != m_writers.end() && iter->head + iter->size <= cursor)
                    {
                        m_io.post(boost::bind(iter->callback, boost::system::error_code(), iter->size));
                        iter = m_writers.erase(iter);
                    }
                }
                else if (type == section::edge)
                {
                    numeral curs(sect.value());

                    m_range = std::max(m_range, curs.value());
                    m_acks.emplace(m_range);
                }

                sect.advance();
                type = sect.type();
            }
        }

        void imbue(packet& pack) noexcept(true)
        {
            auto now = boost::posix_time::microsec_clock::universal_time();

            section sect = pack.stub();
            while (sect.size() > section::header_size + snippet::handle_size)
            {
                auto max = sect.size() - section::header_size - snippet::handle_size;
                auto iter = std::find_if(m_moves.begin(), m_moves.end(), [max, &now](const auto& item)
                {
                    return item.second.time < now && item.second.data.size() <= max;
                });

                if (iter == m_moves.end())
                    break;

                if (iter->second.attempt++ == move_attempts())
                {
                    m_io.post(boost::bind(on_error, boost::asio::error::broken_pipe));
                    return;
                }

                iter->second.time = now + m_repeat;
                
                sect.snippet(iter->first, iter->second.data);
                sect.advance();
            }

            while (m_buffer.available() && m_moves.size() < snippet_flight())
            {
                uint64_t limit = m_range - m_buffer.head();

                if (limit == 0 || sect.size() <= section::header_size + snippet::handle_size)
                    break;

                auto handle = m_buffer.head();
                auto buffer = m_buffer.pull(
                    std::min<uint64_t>(limit, sect.size() - section::header_size - snippet::handle_size)
                    );

                sect.snippet(handle, buffer);
                sect.advance();

                m_moves.emplace(handle, flight(buffer, now + m_repeat));
            }

            auto iter = m_acks.begin();
            while (iter != m_acks.end() && sect.size() >= section::header_size + numeral::value_size)
            {
                sect.numeral(section::edge|section::echo, *iter);
                sect.advance();

                iter = m_acks.erase(iter);
            }

            sect.stub();
        }

        void append(const const_buffer& buffer, const io_callback& caller) noexcept(true)
        {
            auto tail = m_buffer.tail();
            if (m_buffer.add(buffer) == tail)
            {
                m_io.post(boost::bind(caller, boost::asio::error::no_buffer_space, 0));

                if (on_error)
                    m_io.post(boost::bind(on_error, boost::asio::error::no_buffer_space));

                return;
            }

            m_writers.emplace_back(tail, buffer.size(), caller);
        }

        bool deffered() const noexcept(true)
        {
            return !m_moves.empty() || !m_acks.empty() || (m_buffer.available() && m_buffer.head() < m_range);
        }

        uint64_t writable() const noexcept(true)
        {
            return m_range > m_buffer.tail() 
                ? std::min(m_range - m_buffer.tail(), send_buffer_size() - (m_buffer.tail() - m_buffer.head()))
                : 0;
        }

    private:

        struct streambuf
        {
            const_buffer pull(uint64_t max) noexcept(true)
            {
                static const const_buffer zero;

                if (m_data.empty())
                    return zero;

                auto top = m_data.front();
                if (top.size() <= max)
                {
                    m_head += top.size();
                    m_data.pop_front();
                    return top;
                }

                m_head += max;
                return m_data.front().pop_front(max);
            }

            uint64_t add(const const_buffer& buf) noexcept(true)
            {
                if (m_tail + buf.size() - m_head >= send_buffer_size())
                    return m_tail;

                m_tail += buf.size();
                m_data.push_back(buf);

                return m_tail;
            }

            void clear() noexcept(true)
            {
                m_data.clear();
                m_head = 0;
                m_tail = 0;
            }

            uint64_t head() const noexcept(true)
            {
                return m_head;
            }

            uint64_t tail() const noexcept(true)
            {
                return m_tail;
            }

            bool available() const noexcept(true)
            {
                return !m_data.empty();
            }

        private:

            uint64_t m_head = 0;
            uint64_t m_tail = 0;
            std::list<const_buffer> m_data;
        };

        struct writer
        {
            uint64_t head;
            uint64_t size;
            io_callback callback;

            writer(uint64_t h, uint64_t s, const io_callback& c) noexcept(true)
                : head(h)
                , size(s)
                , callback(c)
            {
            }
        };

        struct flight
        {
            const_buffer data;
            boost::posix_time::ptime time;
            uint8_t attempt;

            flight(const const_buffer& s, const boost::posix_time::ptime& t) noexcept(true)
                : data(s)
                , time(t)
                , attempt(1)
            {
            }
        };

        boost::asio::io_context& m_io;
        boost::posix_time::time_duration& m_repeat;
        streambuf m_buffer;
        std::map<uint64_t, flight> m_moves;
        std::list<writer> m_writers;
        std::set<uint64_t> m_acks;
        uint64_t m_range = 0;
        callback on_error;
    };

    struct istreamer
    {
        istreamer(boost::asio::io_context& io, boost::posix_time::time_duration& repeat) noexcept(true)
            : m_io(io)
            , m_repeat(repeat)
        {
        }

        void init(const callback& fallback) noexcept(true)
        {
            on_error = fallback;
        }

        void error(const boost::system::error_code& err) noexcept(true)
        {
            auto iter = m_readers.begin();
            while (iter != m_readers.end())
            {
                m_io.post(boost::bind(iter->callback, err, iter->read));
                ++iter;
            }

            m_readers.clear();
            m_acks.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack) noexcept(true)
        {
            auto sect = pack.body();
            auto type = sect.type();

            while (type != 0)
            {
                if (type == section::move)
                {
                    snippet snip(sect.value());
                    
                    if (m_buffer.map(snip.handle(), snip.fragment()))
                    {
                        m_acks.emplace(snip.handle());
                    }
                    else
                    {
                        if(on_error)
                            m_io.post(boost::bind(on_error, boost::asio::error::no_buffer_space));

                        break;
                    }
                }
                else if (type == (section::edge | section::echo))
                {
                    numeral curs(sect.value());
                    if (m_edge.range == curs.value())
                    {
                        m_edge.time = boost::posix_time::max_date_time;
                    }
                }

                sect.advance();
                type = sect.type();
            }

            transmit();
        }

        void imbue(packet& pack) noexcept(true)
        {
            section sect = pack.stub();

            auto iter = m_acks.begin();
            while (iter != m_acks.end() && sect.size() >= section::header_size + numeral::value_size)
            {
                sect.numeral(section::move|section::echo, *iter);
                sect.advance();

                iter = m_acks.erase(iter);
            }
            
            if (sect.size() >= section::header_size + numeral::value_size)
            {
                auto now = boost::posix_time::microsec_clock::universal_time();
                if (m_edge.time <= now)
                {
                    sect.numeral(section::edge, m_edge.range);
                    sect.advance();

                    m_edge.time = now + m_repeat;
                }
            }

            sect.stub();
        }

        void append(const mutable_buffer& buf, const io_callback& caller) noexcept(true)
        {
            m_readers.emplace_back(buf, caller);
            transmit();
        }

        bool deffered() const noexcept(true)
        {
            return !m_acks.empty() || m_edge.time != boost::posix_time::max_date_time;
        }

        uint64_t readable() const noexcept(true)
        {
            uint64_t bytes = m_buffer.readable_bytes();

            auto iter = m_readers.begin();
            while (bytes > 0 && iter != m_readers.end())
            {
                bytes -= std::min(iter->buffer.size() - iter->read, bytes);
                ++iter;
            }

            return bytes;
        }

    private:

        void transmit() noexcept(true)
        {
            while (m_buffer.available())
            {
                auto iter = m_readers.begin();
                if (iter == m_readers.end())
                    break;
                
                while (m_buffer.available() && iter->buffer.size() > iter->read)
                {
                    auto buffer = m_buffer.pull(iter->buffer.size() - iter->read);
                    iter->buffer.fill(iter->read, buffer.size(), buffer.data());
                    iter->read += buffer.size();
                }

                if (iter->buffer.size() == iter->read)
                {
                    if (iter->read > 0)
                    {
                        m_edge.range += iter->read;
                        m_edge.time = g_zero_time;
                    }

                    m_io.post(boost::bind(iter->callback, boost::system::error_code(), iter->read));
                    m_readers.erase(iter);
                }
            }
        }

        struct streambuf
        {
            const_buffer pull(uint64_t max) noexcept(true)
            {
                static const const_buffer zero;

                if (!available())
                    return zero;

                auto top = m_data.begin()->second;
                m_data.erase(m_data.begin());

                if (top.size() <= max)
                {
                    m_head += top.size();
                    return top;
                }

                m_head += max;

                auto ret = top.pop_front(max);
                m_data.emplace(m_head, top);

                return ret;
            }

            bool map(uint64_t handle, const const_buffer& buffer) noexcept(true)
            {
                if (handle >= m_tail && handle + buffer.size() - m_head > receive_buffer_size())
                    return false;

                if (handle >= m_head)
                {
                    auto res = m_data.emplace(handle, buffer);
                    if (res.second)
                    {
                        auto iter = m_data.rbegin();
                        m_tail = iter->first + iter->second.size();
                    }
                }

                return true;
            }

            uint64_t head() const noexcept(true)
            {
                return m_head;
            }

            uint64_t tail() const noexcept(true)
            {
                return m_tail;
            }

            void clear()
            {
                m_data.clear();
                m_head = 0;
            }

            bool available() const noexcept(true)
            {
                return m_data.size() > 0 && m_data.begin()->first == m_head;
            }

            uint64_t readable_bytes() const noexcept(true)
            {
                uint64_t cursor = m_head;

                auto iter = m_data.begin();
                while (iter != m_data.end() && iter->first == cursor)
                {
                    cursor += iter->second.size();
                    ++iter;
                }

                return cursor - m_head;
            }

        private:

            uint64_t m_head = 0;
            uint64_t m_tail = 0;
            std::map<uint64_t, const_buffer> m_data;
        };

        struct reader
        {
            uint64_t read = 0;
            mutable_buffer buffer;
            io_callback callback;

            reader(const mutable_buffer& b, const io_callback& c) noexcept(true)
                : buffer(b)
                , callback(c)
            {}
        };
        
        struct edge_job
        {
            uint64_t range;
            boost::posix_time::ptime time;

            edge_job() noexcept(true) 
                : range(receive_buffer_size())
                , time(g_zero_time)
            {}
        };

        boost::asio::io_context& m_io;
        boost::posix_time::time_duration& m_repeat;
        streambuf m_buffer;
        std::set<uint64_t> m_acks;
        std::list<reader> m_readers;
        edge_job m_edge;
        callback on_error;
    };

protected:

    void mistake(const boost::system::error_code& ec) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connector.error(ec);
        m_istreamer.error(ec);
        m_ostreamer.error(ec);

        boost::system::error_code err;
        m_socket.close(err);
        m_timer.cancel(err);
    }

    void feed(const mutable_buffer& buffer) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (buffer.size() < packet::header_size)
            return;

        packet pack(m_secret ? dimmer::invert(m_secret, buffer) : buffer);

        if (m_connector.valid(pack))
        {
            auto status = m_connector.status();
            if (status == state::linked || status == state::accepting || status == state::connecting)
            {
                m_istreamer.parse(pack);
                m_ostreamer.parse(pack);
            }

            m_connector.parse(pack);

            status = m_connector.status();
            if (status == state::tearing || status == state::shutting)
            {
                m_istreamer.error(boost::asio::error::connection_aborted);
                m_ostreamer.error(boost::asio::error::connection_aborted);
            }

            boost::system::error_code err;
            m_timer.cancel(err);
        }
    }

    void consume() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() == state::finished)
        {
            m_istreamer.error(boost::asio::error::connection_aborted);
            m_ostreamer.error(boost::asio::error::connection_aborted);
            return;
        }

        std::weak_ptr<transport> weak = shared_from_this();
        auto buffer = create_buffer(max_packet_size());
        m_socket.async_receive(buffer, [weak, buffer](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                        ptr->mistake(error);
                }
                else
                {
                    ptr->feed(buffer.slice(0, size));
                }
                ptr->consume();
            }
        });
    }

    void produce() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() == state::finished)
        {
            m_istreamer.error(boost::asio::error::connection_aborted);
            m_ostreamer.error(boost::asio::error::connection_aborted);

            boost::system::error_code err;
            m_socket.close(err);
            return;
        }

        std::weak_ptr<transport> weak = shared_from_this();

        packet pack(create_buffer(max_packet_size()));
        m_connector.imbue(pack);

        auto status = m_connector.status();
        if (status == state::linked || status == state::connecting)
        {
            m_istreamer.imbue(pack);
            m_ostreamer.imbue(pack);
        }

        pack.trim();

        if (pack.size() > packet::header_size)
        {
            auto buffer = m_secret == 0 ? pack : dimmer::invert(m_secret, pack);
            m_socket.async_send(buffer, [weak, buffer](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                        ptr->mistake(error);
                    else if (buffer.size() < size)
                        ptr->mistake(boost::asio::error::message_size);
                    else
                        ptr->produce();
                }
            });
        }
        else
        {
            auto timeout = status == state::accepting
                ? accept_timeout() : (m_connector.deffered() || m_istreamer.deffered() || m_ostreamer.deffered())
                ? m_repeat : ping_timeout();

            m_timer.expires_from_now(timeout);
            m_timer.async_wait([weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error && error != boost::asio::error::operation_aborted)
                        ptr->mistake(error);
                    else
                        ptr->produce();
                }
            });
        }
    }

    inline void run() noexcept(true)
    {
        std::weak_ptr<transport> weak = shared_from_this();
        m_io.post([weak]()
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->produce();
                ptr->consume();
            }
        });
    }

    inline void wake() noexcept(true)
    {
        boost::system::error_code err;
        m_timer.cancel(err);
    }

public:

    transport(boost::asio::io_context& io, uint64_t secret) noexcept(true)
        : m_io(io)
        , m_socket(io)
        , m_timer(io)
        , m_repeat(boost::posix_time::milliseconds(100))
        , m_connector(io, m_repeat)
        , m_istreamer(io, m_repeat)
        , m_ostreamer(io, m_repeat)
        , m_secret(secret)
    {
    }

    ~transport() noexcept(true) override
    {
        close();
    }

    void open(const endpoint& local) noexcept(false) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        static const size_t SOCKET_BUFFER_SIZE = 1048576;

        if (m_connector.status() != state::neither)
            boost::asio::detail::throw_error(boost::asio::error::no_permission, "bind");

        m_socket.open(local.protocol());

        m_socket.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::reuse_address(true));

        m_socket.bind(local);

        std::weak_ptr<transport> weak = shared_from_this();
        auto on_error = [weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->mistake(error);
            }
        };

        m_connector.init(on_error);
        m_istreamer.init(on_error);
        m_ostreamer.init(on_error);
    }

    void close() noexcept(true) override
    {
        mistake(boost::asio::error::interrupted);
    }

    void shutdown(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.shutdown(handler))
        {
            wake();
        }
    }

    void connect(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() != state::initial)
        {
            m_io.post(boost::bind(handler, boost::asio::error::no_permission));
            return;
        }

        boost::system::error_code ec;
        m_socket.connect(remote, ec);

        if (ec)
        {
            m_io.post(boost::bind(handler, ec));
            return;
        }

        if (m_connector.connect(handler))
        {
            run();
        }
    }

    void accept(const endpoint& remote, const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
                
        if (m_connector.status() != state::initial)
        {
            m_io.post(boost::bind(handler, boost::asio::error::no_permission));
            return;
        }

        boost::system::error_code ec;
        m_socket.connect(remote, ec);

        if (ec)
        {
            m_io.post(boost::bind(handler, ec));
            return;
        }

        if (m_connector.accept(handler))
        {
            run();
        }
    }

    void read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status <= state::initial ? boost::asio::error::not_connected : 
                status <= state::connecting ? boost::asio::error::try_again : boost::asio::error::bad_descriptor;

            m_io.post(boost::bind(handler, ec, 0));
            return;
        }

        m_istreamer.append(buffer, handler);
        wake();
    }

    void write(const const_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status <= state::initial ? boost::asio::error::not_connected : 
                status <= state::connecting ? boost::asio::error::try_again : boost::asio::error::bad_descriptor;

            m_io.post(boost::bind(handler, ec, 0));
            return;
        }

        m_ostreamer.append(buffer, handler);
        wake();
    }

    size_t writable() const noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_connector.status() == state::linked ? static_cast<size_t>(m_ostreamer.writable()) : 0ul;
    }

    size_t readable() const noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_connector.status() == state::linked ? static_cast<size_t>(m_istreamer.readable()) : 0ul;
    }

    endpoint host() const noexcept(false) override
    {
        return m_socket.local_endpoint();
    }

    endpoint peer() const noexcept(false) override
    {
        return m_socket.remote_endpoint();
    }

private:

    boost::asio::io_context& m_io;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::time_duration m_repeat;
    connector m_connector;
    istreamer m_istreamer;
    ostreamer m_ostreamer;
    uint64_t m_secret;
    mutable std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(boost::asio::io_context& io, uint64_t secret) noexcept(true)
{
    return std::make_shared<transport>(io, secret);
}

}
