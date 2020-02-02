extern "C" {
#include<libavdevice/avdevice.h>
#include<libavformat/avformat.h>
#include<libavutil/avutil.h>
#include<libavutil/time.h>
#include<SDL2/SDL.h>
}
#include "main-window.hpp"
#include <QtWidgets>

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
        av_strerror(re, errorInfo, sizeof(errorInfo)); //提取错误信息,打印退出
        std::cout << " Error: " << errorInfo ;
    }
}*/
int main(int argc, char *argv[])
{

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
