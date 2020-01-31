# A P2P-based online live broadcast system

## Projects
* client  
* source-client  
* server  
* libnet  


### **client**
The peer to peer endpoint. Client can watch live video from peer to peer network.

### **source-client**
The live source client application. Based on QT5. Sending video-stream to server.

### **server**
The main functions of the server are **UDP hole punching**, instant messaging (IM), live streaming push from the source client, room number allocation, etc.

### **libnet**
libnet is an asynchronous network library that uses **IO multiplexing** ( *select/epoll* ) and no-blocked IO, while using **coroutines** for each connection to improve IO response.

## Building
```Bash
git clone /url/to/repo
mkdir P2P-Live/build && cd P2P-Live/build
cmake ..
make -j
```


## Third-party
* [Boost.Context](https://www.boost.org/doc/libs/1_72_0/libs/context/doc/html/index.html)  
* [QT5](https://www.qt.io/)

## Maintainers
[@Kadds](https://github.com/Kadds).  
[@YShaw99](https://github.com/YShaw99).  

## License
[GPL-3.0](./LICENSE) © Kadds

