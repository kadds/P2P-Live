#include "main-window.hpp"
#include <QtWidgets>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <thread>

static void atexit_func() { google::ShutdownGoogleLogging(); }
int main(int argc, char *argv[])
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, false);
    google::SetLogDestination(google::GLOG_FATAL, "./client.fatal.log");
    google::SetLogDestination(google::GLOG_ERROR, "./client.error.log");
    google::SetLogDestination(google::GLOG_INFO, "./client.info.log");
    google::SetLogDestination(google::GLOG_WARNING, "./client.warning.log");
    google::SetStderrLogging(google::GLOG_INFO);

    atexit(atexit_func);
    QApplication app(argc, argv);
    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
