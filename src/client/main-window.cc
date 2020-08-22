#include "main-window.hpp"
#include "ui_main-window.h"
#include "yuvwidget.hpp"
#include <functional>
#include <glog/logging.h>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

void MainWindow::video_main()
{

    if (avcodec_open2(video_codec, avcodec_find_decoder(video_codec->codec_id), NULL) < 0)
    {
        LOG(FATAL) << "Could not open video codec.\n";
        exit(-1);
    }

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
    AVPixelFormat pixFormat;
    switch (video_codec->pix_fmt)
    {
        case AV_PIX_FMT_YUVJ420P:
            pixFormat = AV_PIX_FMT_YUV420P;
            break;
        case AV_PIX_FMT_YUVJ422P:
            pixFormat = AV_PIX_FMT_YUV422P;
            break;
        case AV_PIX_FMT_YUVJ444P:
            pixFormat = AV_PIX_FMT_YUV444P;
            break;
        case AV_PIX_FMT_YUVJ440P:
            pixFormat = AV_PIX_FMT_YUV440P;
            break;
        default:
            pixFormat = video_codec->pix_fmt;
            break;
    }

    LOG(INFO) << "video: width: " << video_codec->width << ",height: " << video_codec->height
              << ",format: " << video_codec->pix_fmt << ",desc: " << video_codec->codec_descriptor->long_name;

    auto sws_ctx = sws_getContext(video_codec->width, video_codec->height, pixFormat, target_width, target_height,
                                  format, SWS_BILINEAR, NULL, NULL, NULL);
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    auto vsize = avpicture_get_size(format, video_codec->width, video_codec->height);

    char *out_buffer = new char[vsize];
    avpicture_fill((AVPicture *)frameYUV, (const uint8_t *)out_buffer, format, video_codec->width, video_codec->height);
    auto size_byte = video_codec->width * video_codec->height;
    while (run)
    {
        if (av_read_frame(video_format_ctx, packet) < 0)
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
}

void MainWindow::audio_main()
{
    if (avcodec_open2(audio_codec, avcodec_find_decoder(audio_codec->codec_id), NULL) < 0)
    {
        LOG(FATAL) << "Could not open audio codec.\n";
        exit(-1);
    }
    LOG(INFO) << "audio: "
              << ",desc: " << audio_codec->codec_descriptor->long_name;
}

#ifdef OS_WINDOWS
#include <dshow.h>
#include <windows.h>
#pragma comment(lib, "strmiids")

HRESULT EnumerateDevices(REFGUID category, IEnumMoniker **ppEnum)
{
    // Create the System Device Enumerator.
    ICreateDevEnum *pDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));

    if (SUCCEEDED(hr))
    {
        // Create an enumerator for the category.
        hr = pDevEnum->CreateClassEnumerator(category, ppEnum, 0);
        if (hr == S_FALSE)
        {
            hr = VFW_E_NOT_FOUND; // The category is empty. Treat as an error.
        }
        pDevEnum->Release();
    }
    return hr;
}

struct Dev
{
    std::string name;
    std::string dev_name;
};

#include <codecvt>
#include <string>

std::wstring utf8ToUtf16(const std::string& utf8Str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(utf8Str);
}

std::string utf16ToUtf8(const std::wstring& utf16Str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.to_bytes(utf16Str);
}

int FillDev(std::vector<Dev> &devs, IEnumMoniker *pEnum)
{
    IMoniker *pMoniker = NULL;
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
    {
        IPropertyBag *pPropBag;
        HRESULT hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
        if (FAILED(hr))
        {
            pMoniker->Release();
            continue;
        }
        devs.emplace_back();
        auto & dev = devs.back();

        VARIANT var;
        VariantInit(&var);

        // Get description or friendly name.
        hr = pPropBag->Read(L"Description", &var, 0);
        if (FAILED(hr))
        {
            hr = pPropBag->Read(L"FriendlyName", &var, 0);
        }
        if (SUCCEEDED(hr))
        {
            dev.name = utf16ToUtf8(var.bstrVal);
            VariantClear(&var);
        }

        hr = pPropBag->Write(L"FriendlyName", &var);
        // WaveInID applies only to audio capture devices.
        hr = pPropBag->Read(L"WaveInID", &var, 0);
        if (SUCCEEDED(hr))
        {
            printf("WaveIn ID: %d\n", var.lVal);
            VariantClear(&var);
        }

        hr = pPropBag->Read(L"DevicePath", &var, 0);
        if (SUCCEEDED(hr))
        {
            // The device path is not intended for display.
            dev.dev_name = utf16ToUtf8(var.bstrVal);
            VariantClear(&var);
        }

        pPropBag->Release();
        pMoniker->Release();
    }
    return 0;
}

int ListDevs(std::vector<Dev> &videos, std::vector<Dev> &audios)
{
    IEnumMoniker *pEnum;
    HRESULT hr;
    hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
    if (SUCCEEDED(hr))
    {
        FillDev(videos, pEnum);
        pEnum->Release();
    }
    hr = EnumerateDevices(CLSID_AudioInputDeviceCategory, &pEnum);
    if (SUCCEEDED(hr))
    {
        FillDev(audios, pEnum);
        pEnum->Release();
    }
    return 0;
}

#endif

void MainWindow::get_device()
{
    avdevice_register_all();
    avcodec_register_all();
    av_register_all();
    AVFormatContext *format_ctx = nullptr;

    std::unordered_map<std::string, std::vector<std::string>> devs = {
        {"video4linux2", {"/dev/video0", "/dev/video1"}},
        {"alsa", {"/dev/audio0", "/dev/audio1", "hw:0", "hw:1", "hw2"}},
        {"dshow",
         {"audio=Microphone", "video=Integrated Camera", "video=Integrated Webcam", "video=Camera"}}};

#ifdef OS_WINDOWS
    std::vector<Dev> videos, audios;
    ListDevs(videos, audios);
    for (auto &video : videos)
    {  
        LOG(INFO) << "find video " + video.name << " dev " << video.dev_name;
        devs["dshow"].push_back("video=" + video.name);
    }
    for (auto &audio : audios)
    {  
        LOG(INFO) << "find audio " + audio.name << " dev " << audio.dev_name;
        devs["dshow"].push_back("audio=" + audio.name);
    }
#endif


    for (auto &it : devs)
    {
        auto [name, dev_name] = it;
        AVInputFormat *ifmt = av_find_input_format(name.c_str());
        if (!ifmt)
            continue;
        for (auto dev : dev_name)
        {
            if (!format_ctx)
                format_ctx = avformat_alloc_context();
            if (avformat_open_input(&format_ctx, dev.c_str(), ifmt, NULL) != 0)
            {
                LOG(INFO) << "Couldn't open input stream. " << name << ":" << dev;
                continue;
            }

            if (avformat_find_stream_info(format_ctx, NULL) < 0)
            {
                LOG(INFO) << "Couldn't find stream information.\n";
            }
            bool used = false;
            for (int i = 0; i < format_ctx->nb_streams; i++)
            {
                if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoindex == -1)
                {
                    videoindex = i;
                    video_codec = format_ctx->streams[i]->codec;
                    video_format_ctx = format_ctx;
                    format_ctx = nullptr;
                    used = true;
                    break;
                }
                else if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioindex == -1)
                {
                    audioindex = i;
                    audio_codec = format_ctx->streams[i]->codec;
                    audio_format_ctx = format_ctx;
                    format_ctx = nullptr;
                    used = true;
                    break;
                }
            }
            if (videoindex >= 0 && audioindex >= 0)
                break;
        }
    }

    if (videoindex == -1)
    {
        LOG(FATAL) << "Couldn't find video stream.\n";
    }

    if (audioindex == -1)
    {
        LOG(ERROR) << "Couldn't find audio stream.\n";
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , target_width(0)
    , target_height(0)
    , run(true)
    , videoindex(-1)
    , audioindex(-1)
{
    qRegisterMetaType<std::shared_ptr<char[]>>("std::shared_ptr<char[]>");

    ui->setupUi(this);
    get_device();
    video_thread = std::thread(std::bind(&MainWindow::video_main, this));
    audio_thread = std::thread(std::bind(&MainWindow::audio_main, this));
}

MainWindow::~MainWindow()
{
    run = false;
    video_thread.join();
    audio_thread.join();
    avformat_close_input(&video_format_ctx);
    avformat_free_context(video_format_ctx);
    avformat_close_input(&audio_format_ctx);
    avformat_free_context(audio_format_ctx);
    delete ui;
}
