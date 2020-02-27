FROM archlinux:latest
RUN pacman -Sy --noconfirm \
    && pacman -S --noconfirm make cmake gcc ffmpeg qt5-base google-glog gtest boost git gflags crypto++ sdl2
