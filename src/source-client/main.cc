extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_mutex.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include "peer.hpp"
#include <QtWidgets>
#include <glog/logging.h>
#include <iostream>
#include <main-window.hpp>
#include <string>
#include <thread>
using namespace std; ///-

////函数

void atexit_func() { google::ShutdownGoogleLogging(); }
void set_logger(char *argv[])
{
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::GLOG_FATAL, "./source-client.fatal.log");
    google::SetLogDestination(google::GLOG_ERROR, "./source-client.error.log");
    google::SetLogDestination(google::GLOG_INFO, "./source-client.info.log");
    google::SetLogDestination(google::GLOG_WARNING, "./source-client.warning.log");
    google::SetStderrLogging(google::GLOG_INFO);
    atexit(atexit_func);
}

int main(int argc, char *argv[])
{
    set_logger(argv);
    // test(argc, argv);//进入mainwindow进行文件测试
    net::init_lib();
    on_connection_error([](net::connection_state state) {
        LOG(INFO) << "reconnecting...";
        return true;
    });
    on_edge_server_prepared([]() { LOG(INFO) << "connect to edge server ok"; });
    init_peer(1, net::socket_addr_t("127.0.0.1", 2769), net::make_timespan(2));

    QApplication app(argc, argv);
    MainWindow mainWindow;
    mainWindow.show();
    int v = app.exec();
    net::uninit_lib();
    return v;
}
