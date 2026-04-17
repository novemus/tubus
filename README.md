# README

This repository contains the cross-platform C++ [tubus](https://github.com/novemus/tubus) library, which implements a streaming transport based on UDP or TCP. The library was originally developed for the [wormhole](https://github.com/novemus/wormhole) utility to provide a NAT/DPI-tolerance transport protocol. Network stack over UDP/TCP can optionally be obfuscated by a pre-shared key.

For the convenience of developing applications based on `boost::asio`, the asio-like primitives `tubus::socket` is offered. It implements the *AsyncReadStream*, *AsyncWriteStream*, *Stream*, *SyncReadStream* and *SyncWriteStream* concepts, so it can be used as the lower layer for the `boost::asio::ssl::stream`.

## Core

Tubus сhannel interface.

```cpp
namespace tubus {
...
template<class proto> struct channel
{
    virtual ~channel() noexcept(true) {}
    virtual void close() noexcept(true) = 0;
    virtual void open(const endpoint& local) noexcept(false) = 0;
    virtual void connect(const endpoint& remote, const callback& handle) noexcept(true) = 0;
    virtual void accept(const endpoint& remote, const callback& handle) noexcept(true) = 0;
    virtual void read(const mutable_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void write(const const_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void shutdown(const callback& handle) noexcept(true) = 0;
    virtual size_t writable() const noexcept(true) = 0;
    virtual size_t readable() const noexcept(true) = 0;
    virtual endpoint host() const noexcept(false) = 0;
    virtual endpoint peer() const noexcept(false) = 0;

    static std::shared_ptr<channel<proto>> create(boost::asio::io_context& io, uint64_t secret = 0) noexcept(true);
};

typedef channel<boost::asio::ip::udp> udp_channel;
typedef channel<boost::asio::ip::tcp> tcp_channel;
...
}
```

* **open** - opens a channel at the specified endpoint
* **close** - closes the channel without notifying the remote side, interrupts all pended asynchronous operations
* **shutdown** - closes the channel with the notification of the remote side, interrupts all pended asynchronous operations
* **accept** - waits asynchronously for connection from the specified peer
* **connect** - starts asynchronously connecting to the specified peer
* **read** - appends asynchronous read operation, calls back when the passed buffer is full or an error has occurred
* **write** - appends asynchronous write operation, calls back when the passed buffer is transmitted or an error has occurred
* **writable** - returns the number of bytes that can be transmitted immediately
* **readable** - returns number of bytes available for reading
* **host** - returns local endpoint of the channel
* **peer** - returns remote endpoint of the channel

## Examples

Data consumer implemented using `tubus::udp_channel`.

```cpp
#include <tubus/channel.h>
...
auto consumer = tubus::udp_channel::create(io_service, key);
consumer->open(local_endpoint);
consumer->connect(remote_endpoint, [&](const boost::system::error_code& error)
{
    ...
    tubus::mutable_buffer buffer(consumer->readable()); 
    
    // if the buffer is empty, the callback will be called when all previously
    // pended read operations are completed and more data can be read
    
    consumer->read(buffer, [&](const boost::system::error_code& error, size_t size)
    {
        ...
        consumer->shutdown();
    };
});
```

Data producer implemented using `tubus::udp::socket`.

```cpp
#include <tubus/socket.h>
...
tubus::udp::socket producer(io_service, key);
producer.open(local_endpoint);
producer.async_accept(remote_endpoint, [&](const boost::system::error_code& error)
{
    ...
    tubus::const_buffer buffer("hello, world!");
    producer.async_write_some(buffer, [&](const boost::system::error_code& error, size_t size)
    {
        ...
        producer.shutdown();
    };
});
```

Encrypted stream implemented using `boost::asio::ssl::stream` and `tubus::udp::socket`.

```cpp
#include <boost/asio/ssl.hpp>
#include <tubus/socket.h>
...
boost::asio::ssl::stream<tubus::udp::socket> client(tubus::udp::socket(io_service, key), ssl_ctx);

client.lowest_layer().open(local_endpoint);
client.lowest_layer().connect(remote_endpoint);
client.handshake(boost::asio::ssl::stream_base::client);

boost::asio::read(client, ...);
boost::asio::write(client, ...);

client.shutdown();
```

The use of TCP primitives is the same.

## Build

You can download [prebuild packages](https://github.com/novemus/tubus/releases) for Debian and Windows platforms.

The library depends on the `boost`. To build tests, the `openssl` is required.

```console
$ cd ~
$ git clone https://github.com/novemus/tubus.git
$ cd ~/tubus
$ cmake -B ./build -DCMAKE_BUILD_TYPE=Release [-DBUILD_SHARED_LIBS=ON] [-DBOOST_ROOT=...] [-DTUBUS_SKIP_TEST_RULES=OFF [-DOPENSSL_ROOT_DIR=...]]
$ cmake --build ./build --config Release --target all
$ cmake --build ./build --target install
```

`CMake` variables.

* **TUBUS_SKIP_TEST_RULES** - whether to configure test target, ON by default
* **TUBUS_SKIP_INSTALL_RULES** - whether to configure install target, OFF by default
* **TUBUS_SKIP_PACKAGE_RULES** - whether to configure package target, ON by default
* **TUBUS_USE_SHARED_LIB** - force to import shared library

## Collaboration

Report [bugs](https://github.com/novemus/tubus/issues) and suggest [improvements](https://github.com/novemus/tubus/issues).

## License

Tubus is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2023 Novemus Band. All Rights Reserved.
