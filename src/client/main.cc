#include "main-window.hpp"
#include <QtWidgets>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <thread>

/*
struct SDL
{
    int width, height, x, y;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Rect rect;

    SDL_AudioSpec audiosSpec;

    SDL_mutex *mutex;
};

struct PacketQueue
{
    AVPacketList *pHead, *pLast;
    int size;
    SDL_mutex *queue_mutex;
    SDL_cond *queue_cond;
};
struct VideoFrame
{
    AVFrame *pFrame;
    int width, height;
    double pts;
};
struct Video
{
    char *deviceName;
    //视频信息
    double video_clock; // ns
    double video_current_pts;
    //帧处理
    double frame_timer; // s
    double frame_last_pts;
    double frame_last_delay;

    PacketQueue queue;
    VideoFrame FrameGroup[VIDEO_FRAME_QUEUE_SIZE];
    int group_size,       //视频帧数组的大小
        group_pull_index, //取视频帧索引
        groyp_push_index; //放视频帧索引

    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVStream *pStream;
    AVPacket *pPacket;
    AVFrame *pFrame;
    AVDictionary *pAVD;

    SDL_mutex *group_mutex; //保存视频帧用的锁
    SDL_cond *group_cond;   //

    int quitFlag = 0;
};
struct Audio
{
    char *deviceName;
    double audio_clock;

    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2]; //为啥1.5倍
    unsigned int buffer_size;
    unsigned int buffer_index;
    int in_nb_samples; //单帧采样数
    int out_nb_samples;
    int in_samples_rate; //采样率
    int out_samples_rate;
    int in_channel_layout; //声道布局
    int out_channel_layout;
    int in_channels; //声道
    int out_channels;

    SwrContext *pSwrCtx;

    PacketQueue queue;

    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVStream *pStream;
    AVPacket *pPacket;
    AVFrame *pFrame;
    AVDictionary *pAVD;
};*/
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
