# README

This repository contains the cross-platform C++ [tubus](https://github.com/novemus/tubus) library, which implements the streaming transport protocol based on *UDP*. The protocol was originally developed as part of the [wormhole](https://github.com/novemus/wormhole) utility. It can be used in cases where the use of TCP is difficult, for example, for applications running over NAT. Optionally, to increase connection security, `tubus` packets can be obfuscated by pre-shared key.

For the convenience of developing applications based on `boost::asio`, the asio-like primitives `tubus::socket` and `tubus::acceptor` are offered. The `tubus::socket` primitive implements *AsyncReadStream*, *AsyncWriteStream*, *Stream*, *SyncReadStream* and *SyncWriteStream* concepts, so it can be used as the lower layer of `boost::asio::ssl::stream`.

## Core

Tubus сhannel interface.

```cpp
namespace tubus {
...
struct channel
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
};
...
channel_ptr create_channel(boost::asio::io_context& io, uint64_t /*pre-shared key*/ secret = 0) noexcept(true);
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

Data consumer implemented using `tubus::channel`.

```cpp
#include <channel.h>
...
auto consumer = tubus::create_channel(io_service, key);
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

Data producer implemented using `tubus::socket`.

```cpp
#include <socket.h>
...
tubus::socket producer(io_service, key);
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

Server implemented using `tubus::acceptor` (*Linux* only).

```cpp
#include <acceptor.h>
...
tubus::acceptor server(io_service, key);
server.open(local_endpoint);

tubus::socket peer1(io_service);
server.accept(peer1);

peer1.read_some(...);
peer1.write_some(...);

tubus::socket peer2(io_service);
server.accept(peer2);

peer2.read_some(...);
peer2.write_some(...);

peer1.shutdown();
peer2.shutdown();

server.close();
```

Encrypted stream implemented using `boost::asio::ssl::stream` and `tubus::socket`.

```cpp
#include <boost/asio/ssl.hpp>
#include <socket.h>
...
boost::asio::ssl::stream<tubus::socket> client(tubus::socket(io_service, key), ssl_ctx);

client.lowest_layer().open(local_endpoint);
client.lowest_layer().connect(remote_endpoint);
client.handshake(boost::asio::ssl::stream_base::client);

boost::asio::read(client, ...);
boost::asio::write(client, ...);

client.shutdown();
```

## Build

The library depends on the `boost`. To build tests, the `openssl` is required.

```console
cd ~
git clone https://github.com/novemus/tubus.git
cd ~/tubus
cmake -B ./build [-DBOOST_ROOT=...] [-DBUILD_SHARED_LIBS=ON] [-DBUILD_TESTING=ON [-DOPENSSL_ROOT_DIR=...]]
cmake --build ./build --config Release --target tubus [tubus_ut]
cmake --build ./build --target install
```

`CMake` import variables.

* **tubus_USE_SHARED_LIB=ON** - import dynamic library to your CMake project, searches for a static library by default
* **tubus_INCLUDE_DIR** - tubus header files directory
* **tubus_LIBRARY_DIR** - tubus library directory
* **tubus_DLL** - the path to the tubus.dll library file
* **tubus_LIB** - the path to the tubus-static.lib, tubus.lib, libtubus.a or libtubus.so library file

## Collaboration

Report [bugs](https://github.com/novemus/tubus/issues) and suggest [improvements](https://github.com/novemus/tubus/issues).

## License

Tubus is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2023 Novemus Band. All Rights Reserved.
