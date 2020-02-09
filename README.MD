# A P2P-based online live broadcast system
![GPL-3.0](https://img.shields.io/badge/License-GPL-green)  
![Build:master](https://github.com/kadds/P2P-Live/workflows/Build/badge.svg)
![Test:master](https://github.com/kadds/P2P-Live/workflows/Test/badge.svg)
![Lint:master](https://github.com/kadds/P2P-Live/workflows/Lint/badge.svg)

## Projects
* **client**             *src/client*  
    The peer to peer endpoint. Client can pull video-stream from peer to peer network.
* **source-client**      *src/source-client*  
    The live source client application. Based on QT5. Sending video-stream to server.
* **server**             *src/server*  
    The main functions of the server are [**UDP hole punching**](#UDP-hole-punching), instant messaging (IM), live streaming pull from the source client, room number allocation, peer to peer network control, etc.
* **libnet**             *lib/net*  
    libnet is an **asynchronous** network library that uses **IO multiplexing** ( *select/epoll* ) and no-blocked IO, while using [**coroutines**](#Coroutines) for each connection to improve IO response. Includes coroutines, [thread pool](#Thread-pool), timer, tcp/udp encapsulation, peer to peer network sending/receiving.

## Building

Setting up a development environment with docker (optional):
```Bash
# build image
docker build -t p2p-live .
# run docker container
docker run -i -t p2p-live /usr/bin/bash
cd root
git clone /url/to/repo
```

Building:
```Bash
mkdir P2P-Live/build && cd P2P-Live/build
# set library path like -DCMAKE_PREFIX_PATH=/usr/local/lib
cmake ..
make -j
```

## Testing
```Bash
./build/bin/test-net
```

## Detailed Design
### UDP hole punching
TODO...
### Coroutines
stackfull coroutine  
TODO...
### Thread pool
TODO...

## Third-party
Ensure that the following packages are installed:  
* [Boost.Context](https://www.boost.org/doc/libs/1_72_0/libs/context/doc/html/index.html)  
* [QT5](https://www.qt.io/)  
* [GTest](https://github.com/google/googletest)  
* [GLog](https://github.com/google/glog)  
* [FFmpeg](https://ffmpeg.org)

## Maintainers
[@Kadds](https://github.com/Kadds).  
[@YShaw99](https://github.com/YShaw99).  

## License
[GPL-3.0](./LICENSE) © Kadds

