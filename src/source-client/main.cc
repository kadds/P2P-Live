extern "C" {
#include <SDL2/SDL.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}
#include "main-window.hpp"
#include <QtWidgets>
#include <glog/logging.h>

#include <iostream>

void atexit_func() { google::ShutdownGoogleLogging(); }

int main(int argc, char *argv[])
{
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::GLOG_FATAL, "./source-client.log");
    google::SetLogDestination(google::GLOG_ERROR, "./source-client.log");
    google::SetLogDestination(google::GLOG_INFO, "./source-client.log");
    google::SetLogDestination(google::GLOG_WARNING, "./source-client.log");
    google::SetStderrLogging(google::GLOG_INFO);
    atexit(atexit_func);

    QApplication app(argc, argv);
    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
