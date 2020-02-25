#include "main-window.hpp"
#include "ui_main-window.h"
#include "yuvwidget.hpp"
#include <functional>
#include <glog/logging.h>
#include <thread>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

void MainWindow::device_main()
{
    av_register_all();
    avdevice_register_all();
    AVFormatContext *format_ctx = avformat_alloc_context();
    // AVInputFormat *ifmt = av_find_input_format("vfwcap");
    // AVInputFormat *ifmt = av_find_input_format("dshow");
    // avformat_open_input(&format_ctx,"video=Integrated Camera",ifmt,NULL) ;

    // avformat_open_input(&format_ctx, 0, ifmt, NULL);

    AVInputFormat *ifmt = av_find_input_format("video4linux2");
    if (avformat_open_input(&format_ctx, "/dev/video0", ifmt, NULL) != 0)
    {
        LOG(FATAL) << "couldn't open input stream.\n";
        exit(-1);
    }

    // input video initialize
    if (avformat_find_stream_info(format_ctx, NULL) < 0)
    {
        LOG(FATAL) << "Couldn't find video stream information.\n";
        exit(-1);
    }

    int videoindex = -1;
    int audioindex = -1;
    for (int i = 0; i < format_ctx->nb_streams; i++)
    {
        if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoindex == -1)
        {
            videoindex = i;
        }
        else if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioindex == -1)
        {
            audioindex = i;
        }
    }

    if (videoindex == -1)
    {
        LOG(FATAL) << "couldn't find video stream.\n";
        exit(-1);
    }

    if (audioindex == -1)
    {
        LOG(ERROR) << "couldn't find audio stream.\n";
    }
    auto &video_codec = format_ctx->streams[videoindex]->codec;

    if (avcodec_open2(video_codec, avcodec_find_decoder(video_codec->codec_id), NULL) < 0)
    {
        LOG(FATAL) << "Could not open video codec.\n";
        exit(-1);
    }

    LOG(INFO) << "width: " << video_codec->width << ",height: " << video_codec->height
              << ",format: " << video_codec->pix_fmt;
    AVPacket *packet;
    AVFrame *frame, *frameYUV;
    if (target_width == 0)
    {
        target_width = video_codec->width;
    }
    if (target_height == 0)
    {
        target_height = video_codec->height;
    }

    AVPixelFormat format = AV_PIX_FMT_YUV420P;

    auto sws_ctx = sws_getContext(video_codec->width, video_codec->height, video_codec->pix_fmt, target_width,
                                  target_height, format, SWS_BILINEAR, NULL, NULL, NULL);
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    auto vsize = avpicture_get_size(format, video_codec->width, video_codec->height);

    char *out_buffer = new char[vsize];
    avpicture_fill((AVPicture *)frameYUV, (const uint8_t *)out_buffer, format, video_codec->width, video_codec->height);
    auto size_byte = video_codec->width * video_codec->height;
    while (run)
    {
        if (av_read_frame(format_ctx, packet) < 0)
        {
            LOG(INFO) << "read from stream failed";
            continue;
        }
        int dec_frame = 0;
        auto ret = avcodec_decode_video2(video_codec, frame, &dec_frame, packet);

        if (ret < 0)
        {
            LOG(INFO) << "decode from stream failed";
            continue;
        }
        sws_scale(sws_ctx, (const unsigned char *const *)frame->data, frame->linesize, 0, video_codec->height,
                  frameYUV->data, frameYUV->linesize);
        std::shared_ptr<char[]> dataframe(new char[vsize]);
        int offset = 0;
        memcpy(dataframe.get(), frameYUV->data[0], size_byte);
        offset += size_byte;
        memcpy(dataframe.get() + offset, frameYUV->data[1], size_byte >> 2);
        offset += size_byte >> 2;
        memcpy(dataframe.get() + offset, frameYUV->data[2], size_byte >> 2);

        QMetaObject::invokeMethod(ui->display, "update_frame", Qt::QueuedConnection,
                                  Q_ARG(std::shared_ptr<char[]>, std::move(dataframe)), Q_ARG(int, frame->width),
                                  Q_ARG(int, frame->height));
        av_free_packet(packet);
    }
    av_free_packet(packet);
    av_frame_free(&frameYUV);
    av_frame_free(&frame);
    sws_freeContext(sws_ctx);
    delete[] out_buffer;

    avcodec_close(video_codec);
    avformat_close_input(&format_ctx);
    avformat_free_context(format_ctx);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , target_width(0)
    , target_height(0)
    , run(true)
{
    qRegisterMetaType<std::shared_ptr<char[]>>("std::shared_ptr<char[]>");

    ui->setupUi(this);
    thread = std::thread(std::bind(&MainWindow::device_main, this));
}

MainWindow::~MainWindow()
{
    run = false;
    thread.join();
    delete ui;
}
