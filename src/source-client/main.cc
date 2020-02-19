extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_mutex.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <QtWidgets>
#include <glog/logging.h>
#include <iostream>
#include <main-window.hpp>
#include <string>
#include <thread>
#include <unistd.h>
#define SDL_PLAY_PIX_FMT AV_PIX_FMT_YUV420P // sdl播放像素格式
#define MAX_AUDIO_FRAME_SIZE 192000
#define MAX_AUDIO_QUEUE_SIZE 10
#define MAX_VIDEO_QUEUE_SIZE 10
using namespace std; ///-
struct PacketQueue
{
    AVPacketList *front, *end;
    int size;
    SDL_mutex *queue_mutex;
    SDL_cond *queue_notfull;
    SDL_cond *queue_notempty;
};

// AV Arg
AVFormatContext *pFormatCtx;
AVInputFormat *pInputFormat_Audio;
AVInputFormat *pInputFormat_Video;
AVCodecContext *pCodecCtx_Audio;
AVCodecContext *pCodecCtx_Video;
AVCodec *pCodec_Audio;
AVCodec *pCodec_Video;
AVStream *pStream_Audio;
AVStream *pStream_Video;
AVFrame *pFrame_Audio;
AVFrame *pFrame_Video_in;
AVFrame *pFrame_Video_YUV;
AVFrame *pFrame_Video_out;
AVPacket *pPacket_Audio;
AVPacket *pPacket_Video;
AVPacket *pPacket;
SwrContext *pSwrCtx;
SwsContext *pSwsCtx;

AVDictionary *pDict = NULL;

// Audio
SDL_AudioSpec sdl_audios;

int audio_stream_index;
int sample_rate; //输入输出相同，暂不作处理:swr_alloc_set_opts
int audio_fotmat;
int nb_sample;
uint64_t in_channel_layout; //出入相同，暂不作处理
uint64_t in_nb_channel_layout;
uint64_t out_channel_layout;
uint64_t out_nb_channel_layout;
AVSampleFormat audio_format;
int audio_buffer_size;
uint8_t *audio_buffer;
// Audio rollback
Uint8 *audio_chunk = 0;
Uint32 audio_len = 0;
Uint8 *audio_pos = 0;

// Video
SDL_Window *sdl_screen = NULL;
SDL_Renderer *sdl_renderer = NULL;
SDL_Texture *sdl_texture = NULL;
SDL_Rect sdl_rect;

int video_stream_index;
int width;
int height;
int fps;
AVPixelFormat video_format;
int video_buffer_size;
uint8_t *video_buffer;
// other
SDL_mutex *play_mutex;

PacketQueue *video_queue;
PacketQueue *audio_queue;

int quitFlag = 0;
int stopFlag = 0;
int re = 0;

////函数
int ErrorExit(int errorNum);
int ErrorExit(std::string errorStr);
int packet_pop_queue(PacketQueue *queue, AVPacket *packet);
int packet_push_queue(PacketQueue *queue, AVPacket *packet);

void atexit_func() { google::ShutdownGoogleLogging(); }
void Log(char *argv[])
{
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::GLOG_FATAL, "./source-client.log");
    google::SetLogDestination(google::GLOG_ERROR, "./source-client.log");
    google::SetLogDestination(google::GLOG_INFO, "./source-client.log");
    google::SetLogDestination(google::GLOG_WARNING, "./source-client.log");
    google::SetStderrLogging(google::GLOG_INFO);
    atexit(atexit_func);
}
inline double r2d(AVRational r) { return r.num == 0 || r.den == 0 ? 0.0 : (double)r.num / (double)r.den; }

int audio_decode(void *)
{
    /// std::cout << "audio_decode tid:";
    /// std::cout << pthread_self();
    while (audio_queue->size > 0)
    {
        re = packet_pop_queue(audio_queue, pPacket_Audio);
        re = avcodec_send_packet(pCodecCtx_Audio, pPacket_Audio);
        while (avcodec_receive_frame(pCodecCtx_Audio, pFrame_Audio) == 0)
        {
            /*len1 = is->audio_frame.pkt_size;

            if (len1 < 0)
            {
                /* if error, skip frame *
                is->audio_pkt_size = 0;
                break;
            }

            data_size = 2 * is->audio_frame.nb_samples * 2;
            assert(data_size <= buf_size);

            swr_convert(is->audio_swr_ctx, &audio_buf, MAX_AUDIO_FRAME_SIZE * 3 / 2,*
                        (const uint8_t **)is->audio_frame.data, is->audio_frame.nb_samples);*/
        }
    }
}
//音频回调函数,调用解码
void audio_callback(void *udata, Uint8 *stream, int len)
{

    /// std::cout << "audio_callback:" << pthread_self() << "\n";
    audio_decode(NULL);
    SDL_memset(stream, 0, len);

    /*while (len > 0)
        {
            if (audio_len == 0)
                continue;
            int temp = (len > audio_len ? audio_len : len);
            SDL_MixAudio(stream, audio_pos, temp, SDL_MIX_MAXVOLUME);
            audio_pos += temp;
            audio_len -= temp;
            stream += temp;
            len -= temp;
        }*/
    if (audio_len == 0)
        return;

    len = (len > audio_len ? audio_len : len); /*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}
//空间分配init
int init()
{
    pFormatCtx = avformat_alloc_context();
    if (pFormatCtx == nullptr)
        ErrorExit("pFormatCtx alloc fail");

    pFrame_Audio = av_frame_alloc();
    pFrame_Video_in = av_frame_alloc();
    pFrame_Video_out = av_frame_alloc();
    pFrame_Video_YUV = av_frame_alloc();
    if (!pFrame_Audio || !pFrame_Video_in || !pFrame_Video_out || !pFrame_Video_YUV)
        ErrorExit("pFrame alloc fail");

    pPacket_Video = av_packet_alloc();
    pPacket_Audio = av_packet_alloc();
    pPacket = av_packet_alloc();
    if (!pPacket_Video || !pPacket_Audio || !pPacket)
        ErrorExit("pPacket alloc fail");

    pSwrCtx = swr_alloc();
    if (!pSwrCtx)
        ErrorExit("pSwrCtx alloc fail");

    audio_queue = (PacketQueue *)av_malloc(sizeof(PacketQueue));
    video_queue = (PacketQueue *)av_malloc(sizeof(PacketQueue));
    if (!audio_queue || !video_queue)
        ErrorExit("queue alloc fail");

    return 0;
}
//更新sdl以及输入输出音频信息
int sdl_init(int isVideo)
{
    re = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (re != 0)
        ErrorExit("SDL Init Error");
    void (*pcallback)(void *, Uint8 *, int) = &audio_callback;

    switch (isVideo)
    {
        case 0:
            // audio state init
            audio_format = pCodecCtx_Audio->sample_fmt; //重采样用
            in_channel_layout = pCodecCtx_Audio->channels;
            in_nb_channel_layout = av_get_channel_layout_nb_channels(in_channel_layout);
            out_channel_layout = in_channel_layout;
            out_nb_channel_layout = in_nb_channel_layout;
            sample_rate = pCodecCtx_Audio->sample_rate;

            sdl_audios.freq = sample_rate;    //采样率
            sdl_audios.format = AUDIO_S16SYS; /// sdl播放格式AUDIO_S16SYS与输入格式的区别AV_SAMPLE_FMT_S16
            sdl_audios.channels = in_channel_layout;
            sdl_audios.silence = 0; //静音
            sdl_audios.samples = nb_sample;
            sdl_audios.callback = pcallback;
            sdl_audios.userdata = pCodecCtx_Audio;

            pSwrCtx = swr_alloc_set_opts(pSwrCtx,                      //
                                         1, audio_format, sample_rate, //目标
                                         1, audio_format, sample_rate, //源
                                         0, NULL);
            swr_init(pSwrCtx);

            // audio sdl init  &  audio resample
            if (SDL_OpenAudio(&sdl_audios, NULL) != 0)
                ErrorExit("SDL_OpenAudio Fail");

            SDL_PauseAudio(0);
            // audio buffer
            audio_buffer_size = av_samples_get_buffer_size(NULL, out_nb_channel_layout, nb_sample, audio_format, 1);
            audio_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

            break;
        case 1:
            // video state init
            width = pCodecCtx_Video->width;
            height = pCodecCtx_Video->height;
            video_format = pCodecCtx_Video->pix_fmt;

            // video sdl init
            sdl_screen = SDL_CreateWindow("new sdl window", 0, 0, width, height, SDL_WINDOW_RESIZABLE);
            if (!sdl_screen)
                ErrorExit("window init fail");

            sdl_renderer = SDL_CreateRenderer(sdl_screen, -1, 0);
            if (!sdl_renderer)
                ErrorExit("render init fail");

            sdl_texture =
                SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_TARGET, width, height);
            if (!sdl_texture)
                ErrorExit("texture");

            play_mutex = SDL_CreateMutex();
            // video resample
            pSwsCtx = sws_getCachedContext(pSwsCtx, width, height, video_format, //源
                                           width, height, SDL_PLAY_PIX_FMT,      //目标
                                           SWS_BICUBIC,                          //尺寸变化算法
                                           0, 0, 0);

            // video buffer
            video_buffer_size = avpicture_get_size(SDL_PLAY_PIX_FMT, width, height);
            video_buffer = (uint8_t *)av_malloc(video_buffer_size);
            avpicture_fill((AVPicture *)pFrame_Video_YUV, video_buffer, SDL_PLAY_PIX_FMT, width, height);

            break;
        default:
            break;
    }

    return 0;
}

int packet_queue_init(PacketQueue *queue)
{
    memset(queue, 0, sizeof(PacketQueue));
    queue->queue_mutex = SDL_CreateMutex();
    queue->queue_notempty = SDL_CreateCond();
    queue->queue_notfull = SDL_CreateCond();

    return 0;
}

//设备初始化
int device_init()
{
    avdevice_register_all();
    //音频
    pInputFormat_Audio = av_find_input_format("alsa");
    if (pInputFormat_Audio == nullptr)
        ErrorExit("Cant find Audio input ");
    re = avformat_open_input(&pFormatCtx, "default", pInputFormat_Audio, NULL);
    if (re)
        ErrorExit("Init Audio Input");
    //视频
    pInputFormat_Video = av_find_input_format("video4linux2");
    if (pInputFormat_Audio == nullptr)
        ErrorExit("Cant find Audio input ");
    re = avformat_open_input(&pFormatCtx, "/dev/video0", pInputFormat_Video, NULL);
    if (re)
        ErrorExit("Init Video Input");

    return 0;
}
void destroy()
{
    if (pFormatCtx)
        avformat_free_context(pFormatCtx);

    if (pCodecCtx_Audio)
        avcodec_free_context(&pCodecCtx_Audio);

    if (pCodecCtx_Video)
        avcodec_free_context(&pCodecCtx_Video);

    if (pPacket)
        av_packet_free(&pPacket);
    if (pPacket_Audio)
        av_packet_free(&pPacket_Audio);
    if (pPacket_Video)
        av_packet_free(&pPacket_Video);

    SDL_DestroyWindow(sdl_screen);
    SDL_Quit();

    if (audio_buffer)
        av_free(audio_buffer);
    if (video_buffer)
        av_free(video_buffer);

    if (audio_queue)
        av_free(audio_queue);
    if (video_queue)
        av_free(video_queue);
}
void dumpArg(int isAudio) { std::string format; }

int demux(void *)
{

    std::cout << "demux tid:";
    std::cout << pthread_self() << std::endl;

    av_dump_format(pFormatCtx, 0, 0, 0);
    /// delete!  re = avformat_find_stream_info(pFormatCtx, NULL);
    //流初始化
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
            pStream_Audio = pFormatCtx->streams[i];
            //查找解码器
            pCodec_Audio = avcodec_find_decoder(pStream_Audio->codecpar->codec_id);
            if (pCodec_Audio == nullptr)
                ErrorExit("Cant find Audio decoder");
            //分配、复制、打开解码器
            pCodecCtx_Audio = avcodec_alloc_context3(pCodec_Audio);
            if (pCodecCtx_Audio == nullptr)
                ErrorExit("Alloc Audio CodecCtx Fail");
            re = avcodec_parameters_to_context(pCodecCtx_Audio, pStream_Audio->codecpar);
            if (re < 0)
                ErrorExit("Audio Stream parameters Copy to CodecCtx Fail ");
            re = avcodec_open2(pCodecCtx_Audio, pCodec_Audio, nullptr);
            if (re != 0)
                ErrorExit("Open Codec Fail");
            //
            sdl_init(0);
            // dumpArg(0);
        }
        else if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            pStream_Video = pFormatCtx->streams[i];
            //查找解码器
            pCodec_Video = avcodec_find_decoder(pStream_Video->codecpar->codec_id);
            if (pCodec_Video == nullptr)
                ErrorExit("Cant find Video decoder");
            //分配、复制、打开解码器
            pCodecCtx_Video = avcodec_alloc_context3(pCodec_Video);
            if (pCodecCtx_Video == nullptr)
                ErrorExit("Alloc Video CodecCtx Fail");
            re = avcodec_parameters_to_context(pCodecCtx_Video, pStream_Video->codecpar);
            if (re < 0)
                ErrorExit("Video Stream parameters Copy to CodecCtx Fail ");
            re = avcodec_open2(pCodecCtx_Video, pCodec_Video, nullptr);
            if (re != 0)
                ErrorExit("Open Codec Fail");
            //
            fps = r2d(pCodecCtx_Video->framerate) == 0 ? 25 : r2d(pCodecCtx_Video->framerate);

            sdl_init(1);
            // dumpArg(1);
        }
    }
    //读包

    while (1)
    {
        if (quitFlag || stopFlag)
            break;
        if (audio_queue->size >= MAX_AUDIO_QUEUE_SIZE || video_queue->size >= MAX_AUDIO_QUEUE_SIZE)
        {
            SDL_Delay(10);
            if (audio_queue->size >= MAX_AUDIO_QUEUE_SIZE)
                SDL_CondSignal(audio_queue->queue_notempty);

            if (video_queue->size >= MAX_VIDEO_QUEUE_SIZE)
                SDL_CondSignal(video_queue->queue_notempty);

            continue;
        }
        re = av_read_frame(pFormatCtx, pPacket);
        std::cout << re;
        if (re < 0)
        {
            if (pFormatCtx->pb->error == 0)
            {
                SDL_Delay(100);
                continue;
            }
            else
            {
                break;
            }
        }
        string str=pPacket->stream_index == video_stream_index ? "视频"
                                                               : "音频";
        std::cout << "--------demux放包进--------" << str << std::endl;
        //把包放入链表中
        if (pPacket->stream_index == video_stream_index)
        {
            packet_push_queue(video_queue, pPacket);
        }
        else if (pPacket->stream_index == audio_stream_index)
        {
            packet_push_queue(audio_queue, pPacket);
        }
        else
        {
            av_packet_unref(pPacket);
        }
    }
}

int packet_push_queue(PacketQueue *queue, AVPacket *packet) //创建节点 放入queue
{
    if (av_packet_make_refcounted(packet) < 0)
    {
        return -1;
    } ///引用！

    SDL_CondWait(queue->queue_notempty, queue->queue_mutex);
    AVPacketList *temp_packet = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!temp_packet)
        ErrorExit("push_queue temp_packet alloc fail");

    temp_packet->pkt = *packet;
    temp_packet->next = NULL;
    SDL_LockMutex(queue->queue_mutex);

    if (queue->front == NULL)
    {
        queue->front = temp_packet;
        queue->front->next = NULL;
    }
    else
        queue->end->next = temp_packet;

    queue->end = temp_packet;
    queue->size++;

    SDL_CondSignal(queue->queue_notempty);
    SDL_UnlockMutex(queue->queue_mutex);

    return 0;
}
int packet_pop_queue(PacketQueue *queue, AVPacket *packet)
{

    SDL_LockMutex(queue->queue_mutex);
    while (queue->size <= 0)
        SDL_CondWait(queue->queue_notempty, queue->queue_mutex);

    *packet = queue->front->pkt; //取出去

    AVPacketList *temp_packet = queue->front;
    if (queue->size == 1)
    {
        queue->front = NULL;
        queue->end = NULL;
    }
    else
        queue->front = queue->front->next;

    queue->size--;

    av_free(temp_packet); //放入队列中的包由malloc来

    SDL_CondSignal(queue->queue_notfull);
    SDL_UnlockMutex(queue->queue_mutex);

    return 0;
}

void sdl_play_video(uint8_t *YPlane, uint8_t *UPlane, uint8_t *VPlane)
{
    int re = SDL_UpdateYUVTexture(sdl_texture, NULL, YPlane, width, UPlane, width / 2, VPlane, width / 2);
    if (re)
        std::cout << "SDL update error: " << SDL_GetError() << std::endl;
    sdl_rect.x = 0;
    sdl_rect.y = 0;
    sdl_rect.h = pCodecCtx_Video->height;
    sdl_rect.w = pCodecCtx_Video->width;

    SDL_LockMutex(play_mutex);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, &sdl_rect);
    SDL_RenderPresent(sdl_renderer);
    SDL_Delay(fps);
    SDL_UnlockMutex(play_mutex);
}
int video_decode(void *)
{
    std::cout << "video_decode tid:";
    std::cout << pthread_self();
    while (1)
    {
        std::cout << "video_decode: size=" << video_queue->size << std::endl;

        if (packet_pop_queue(video_queue, pPacket_Video) == 0)
            std::cout << "video packet_pop_queue success" << std::endl;
        else
            continue;

        re = avcodec_send_packet(pCodecCtx_Video, pPacket_Video);
        std::cout << "decode video re:" << re << std::endl;
        /*if (re < 0) // fail
            ErrorExit(re);*/
        while (avcodec_receive_frame(pCodecCtx_Video, pFrame_Video_in) == 0)
        {
            sws_scale(pSwsCtx,                                                                      //
                      pFrame_Video_in->data, pFrame_Video_in->linesize, 0, pCodecCtx_Video->height, //源数据
                      pFrame_Video_YUV->data, pFrame_Video_YUV->linesize); //重采样到YUV进行播放

            sdl_play_video(pFrame_Video_YUV->data[0], pFrame_Video_YUV->data[0], pFrame_Video_YUV->data[0]);
            /// encode
        }

        av_packet_unref(pPacket_Video);
    }
}

int ErrorExit(int errorNum)
{
    char *errorInfo;
    av_strerror(errorNum, errorInfo, sizeof(errorInfo)); //提取错误信息,打印退出
    std::cout << " Error: " << errorInfo << std::endl;
    destroy();
    exit(-1);
}
int ErrorExit(std::string errorStr)
{
    std::cout << " Error: " << errorStr << std::endl;
    destroy();
    exit(-1);
}

void test()
{ // std::cout  << fps << std::endl << r2d(pCodecCtx_Video->framerate);
    re = av_read_frame(pFormatCtx, pPacket);
    std::cout << "test:" << re << std::endl;
}
int main(int argc, char *argv[])
{
    Log(argv);

    std::cout << "main:start\n";
    if (init() || device_init() || packet_queue_init(audio_queue) || packet_queue_init(video_queue))
        std::cout << "error!!!!!!";

    std::cout << "main:init end\n";
    SDL_CreateThread(demux, "demux thread", NULL);
    std::cout << "main:demux end\n";

    std::cout << "main:test end\n";

    std::cout << "main tid:" << pthread_self() << std::endl;

    pFormatCtx->flags |= AVFMT_FLAG_NONBLOCK;

    SDL_CreateThread(video_decode, "video_decode thread", NULL);
    test();
    /// SDL_PauseAudio(1);
    while (1)
    {
        std::cout << std::flush;
        sleep(1);
    } /*

     while (1)
         std::cout << " " << std::flush;

  QApplication app(argc, argv);
  MainWindow mainWindow;
  mainWindow.show();
  return app.exec();
*/
    return 0;
}
