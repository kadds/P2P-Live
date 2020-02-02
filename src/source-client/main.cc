extern "C" {
#include<libavdevice/avdevice.h>
#include<libavformat/avformat.h>
#include<libavutil/avutil.h>
#include<libavutil/time.h>
#include<SDL2/SDL.h>
}
#include "main-window.hpp"
#include <QtWidgets>
#include <glog/logging.h>

#include <iostream>

/*
void OpenMp4(){
    av_register_all();
    avcodec_register_all();
    char* input="test.mp4";
    AVFormatContext *avfc=NULL;

    int re=avformat_open_input(&avfc,input,0,0);
    if(re==0)
        std::cout<<"success"<<std::endl;
    else{
        char *errorInfo;
        av_strerror(re, errorInfo, sizeof(errorInfo));
        std::cout << " Error: " << errorInfo ;
    }
}*/
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
