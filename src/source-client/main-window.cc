#include "main-window.hpp"
#include "peer.hpp"
#include "ui_main-window.h"
#include <functional>
#include <glog/logging.h>
#include <string>
#include <vector>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
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
    this->setWindowTitle("P2P Live");

    video_thread = std::thread(std::bind(&MainWindow::video_main, this));
    audio_thread = std::thread(std::bind(&MainWindow::audio_main, this));
}

void MainWindow::video_main()
{
    get_device();
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
    auto vsize = av_image_get_buffer_size(format, video_codec->width, video_codec->height, 4);

    char *out_buffer = new char[vsize];
    av_image_fill_arrays(frameYUV->data, frameYUV->linesize, (const uint8_t *)out_buffer, format, video_codec->width,
                         video_codec->height, 4);
    auto size_byte = video_codec->width * video_codec->height;
    int dec_frame = 0;
    while (run)
    {
        if (av_read_frame(video_format_ctx, packet) != 0)
        {
            LOG(INFO) << "read from stream failed";
            continue;
        }
        if (avcodec_send_packet(video_codec, packet) != 0)
        {
            LOG(WARNING) << "decode from stream failed";
            continue;
        }
        send_data(packet->data, packet->size, 1, dec_frame++);

        while (avcodec_receive_frame(video_codec, frame) == 0)
        {
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
        }
        av_packet_unref(packet);
    }
    av_packet_unref(packet);
    av_frame_free(&frameYUV);
    av_frame_free(&frame);
    sws_freeContext(sws_ctx);
    delete[] out_buffer;

    avcodec_free_context(&video_codec);
}

void MainWindow::audio_main()
{
    std::unique_lock<std::mutex> lck(device_ready);
    cond_dev.wait(lck);

    if (avcodec_open2(audio_codec, avcodec_find_decoder(audio_codec->codec_id), NULL) < 0)
    {
        LOG(FATAL) << "Could not open audio codec.\n";
        exit(-1);
    }
    LOG(INFO) << "audio: "
              << "desc: " << audio_codec->codec_descriptor->long_name;
    auto packet = av_packet_alloc();
    auto frame = av_frame_alloc();
    int dec_frame = 0;
    while (run)
    {
        if (av_read_frame(audio_format_ctx, packet) != 0)
        {
            LOG(INFO) << "read from stream failed";
            continue;
        }

        if (avcodec_send_packet(audio_codec, packet) != 0)
        {
            LOG(WARNING) << "decode audio from stream failed";
            continue;
        }
        send_data(packet->data, packet->size, 2, dec_frame++);

        while (avcodec_receive_frame(audio_codec, frame) == 0)
        {

            /// TODO: play audio and send audio
        }
        av_packet_unref(packet);
    }
    av_packet_unref(packet);
    av_frame_free(&frame);
    avcodec_free_context(&audio_codec);
}

void ffmpeg_logger(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level >= AV_LOG_INFO)
    {
        return;
    }
    char log_buffer[128];
    log_buffer[vsnprintf(log_buffer, 127, fmt, vl)] = 0;

    if (level >= AV_LOG_WARNING)
    {
        LOG(WARNING) << log_buffer;
    }
    else if (level >= AV_LOG_ERROR)
    {
        // LOG(ERROR) << log_buffer;
    }
    else
    {
        LOG(FATAL) << log_buffer;
    }
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

std::wstring utf8ToUtf16(const std::string &utf8Str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(utf8Str);
}

std::string utf16ToUtf8(const std::wstring &utf16Str)
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
        auto &dev = devs.back();

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
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
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
    av_log_set_callback(ffmpeg_logger);
    av_log_set_level(AV_LOG_ERROR);
    avdevice_register_all();

    AVFormatContext *format_ctx = nullptr;

    std::unordered_map<std::string, std::vector<std::string>> devs = {
        {"video4linux2", {"/dev/video0", "/dev/video1"}},
        {"alsa", {"/dev/audio0", "/dev/audio1", "hw:0", "hw:1", "hw2"}},
        {"dshow", {"audio=Microphone", "video=Integrated Camera", "video=Integrated Webcam", "video=Camera"}}};

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
                if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoindex == -1)
                {
                    videoindex = i;
                    AVCodec *codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
                    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
                    avcodec_parameters_to_context(codec_ctx, format_ctx->streams[i]->codecpar);
                    av_codec_set_pkt_timebase(codec_ctx, format_ctx->streams[i]->time_base);
                    video_codec = codec_ctx;
                    video_format_ctx = format_ctx;
                    format_ctx = nullptr;
                    used = true;
                    break;
                }
                else if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioindex == -1)
                {
                    audioindex = i;
                    AVCodec *codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
                    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
                    avcodec_parameters_to_context(codec_ctx, format_ctx->streams[i]->codecpar);
                    av_codec_set_pkt_timebase(codec_ctx, format_ctx->streams[i]->time_base);
                    audio_codec = codec_ctx;
                    audio_format_ctx = format_ctx;
                    format_ctx = nullptr;
                    used = true;
                    cond_dev.notify_one();
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
