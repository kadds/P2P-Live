# A P2P-based online live broadcast system
![GPL-3.0](https://img.shields.io/badge/License-GPL-green)  
![Build:master](https://github.com/kadds/P2P-Live/workflows/Build/badge.svg)
![Test:master](https://github.com/kadds/P2P-Live/workflows/Test/badge.svg)
![Lint:master](https://github.com/kadds/P2P-Live/workflows/Lint/badge.svg)

## Projects
* **client**             *src/client*  
    The peer to peer endpoint. Client can pull video-stream from peer to peer network with multi-channels.
* **source-client**      *src/source-client*  
    The live source client application. Based on QT5. Sending video-stream to server.
* **server**             *src/server*  
    The main functions of the server are [**UDP hole punching**](#UDP-hole-punching), instant messaging (IM), live streaming pull from the source client, room number allocation, peer to peer network control, etc.
* **libnet**             *lib/net*  
    libnet is an **asynchronous** network library that uses **IO multiplexing** ( *select/epoll/IOCP* ) and no-blocked IO, while using [**coroutines**](#Coroutines) for each connection to improve IO response. Includes coroutines, [thread pool](#Thread-pool), timer, tcp/udp encapsulation, peer to peer network sending/receiving, tracker nodes exchange, reliable udp make by KCP, hole punching. etc...

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
> NAT types  
> * NAT1  
>   Full Cone NAT  
> * NAT2  
>   Address-Restricted Cone NAT  
> * NAT3  
>   Port-Restricted Cone NAT  
> * NAT4  
>   Symmetric NAT  

If PeerA wants to connect to PeerB: 

| Name    | Address     |                        Mark                         |
| :------ | :---------- | :-------------------------------------------------: |
| ServerT | ipS:portS   | RUDP listens connection request and detect NAT port |
| PeerA   | ipA:portA   |                 PeerA local address                 |
| PeerB   | ipB:portB   |                 PeerB local address                 |
| NATA    | ipNA:portNA |                                                     |
| NATB    | ipNB:portNB |                                                     |


> 1. PeerA sends a connection request to ServerT(ipS:portS) through RUDP.  
> 2. ServerT obtains NAT address (ipNA:portNA) of PeerA and forwards the request to PeerB through TCP long connection.   
> 3. PeerB attempts to connect to PeerA(ipNA:portNA) directly. If failed. PeerB sends a connection request to ServerT through RUDP.   
> 4. ServerT get PeerB's NAT address (ipNB:portNB) and transport the request to PeerA.   
> 5. PeerA attempts to connect to PeerB (ipNB:portNB).   
> 6. Once PeerA and PeerB send to each other, The hole punching is completed.   

This method works only NATA and NATB is not Symmetric NAT.  

But When NATA or NATB is Symmetric NAT?  
> Using port guessing may succeed but isn't stable. We don't process this condition.

If NTAA and NATB both symmetric NAT, We don't process it and just connect to edge server to get data.

### Coroutines
It is a stackful coroutine switch by Boost.Context.
Coroutine uses:
1. TCP
    When TCP acceptor accepts a new TCP client, a coroutine is built to process it. Coroutines are randomly assigned to the event loop for load balancing.
2. UDP
   Not supports multi-coroutines, only one coroutine is used for sending and writing.
3. RUDP
   Reliable UDP is built by KCP(ARQ) from UDP. A coroutine is built when tell RUDP to establish a new connection. each connection has a send/recv queue and does not block each other.  

Coroutine dispatcher:
Our scheduler has a dispatch queue and dispatches via FIFO. No need for cross-thread scheduling.

### Thread pool
TODO...

## Third-party
Ensure that the following packages are installed:  
* [Boost.Context](https://www.boost.org/doc/libs/1_72_0/libs/context/doc/html/index.html)  
* [QT5](https://www.qt.io/)  
* [GTest](https://github.com/google/googletest)  
* [GLog](https://github.com/google/glog)  
* [FFmpeg](https://ffmpeg.org)
* [gflags](https://github.com/gflags/gflags)

## Maintainers
[@Kadds](https://github.com/Kadds).  
[@YShaw99](https://github.com/YShaw99).  

## License
[GPL-3.0](./LICENSE) © Kadds

