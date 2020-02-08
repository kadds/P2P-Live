FROM archlinux:latest
RUN pacman -Sy
    && pacman -S --noconfirm make cmake gcc ffmpeg qt5-base google-glog gtest boost git 
RUN cd root && git clone https://github.com/kadds/P2P-Live.git 
    && cd P2P-Live && mkdir build && cd build