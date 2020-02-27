#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <thread>
extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_render.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#define FPS 25
#define AV_NOSYNC_THRESHOLD 0.01
#define VIDEO_PICTURE_QUEUE_SIZE 2
#define MAX_AUDIO_FRAME_SIZE 192000
#define MAX(a, b) (a) > (b) ? (a) : (b);
#define MIN(a, b) (a) > (b) ? (b) : (a);

struct SDL_Video
{
    int width = 800, height = 600, x, y;
    int resize;
    SDL_Window *sdl_window = NULL;
    SDL_Renderer *sdl_renderer = NULL;
    SDL_Texture *sdl_texture = NULL;
    SDL_Rect *sdl_rect;

    SDL_AudioSpec sdl_audiosSpec;

    SDL_mutex *play_mutex;
};
struct PacketQueue
{
    AVPacketList *front, *end;
    int size;
    SDL_mutex *queue_mutex;
    SDL_cond *queue_notempty;
    SDL_cond *queue_notfull;
};
struct VideoPicture
{
    AVFrame *pFrame;
    double pts;
};
struct VideoInfo
{
    // char *deviceName;
    //视频用buffer相关
    uint8_t *buffer;
    uint32_t buffer_size;
    //视频信息
    double video_clock; // 为单位
    double video_current_pts;
    //帧处理相关
    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;

    PacketQueue queue;
    VideoPicture group[VIDEO_PICTURE_QUEUE_SIZE];
    int group_size,           //视频帧数组的大小
        group_pull_index,     //取视频帧索引
        group_push_index;     //放视频帧索引
    SDL_mutex *group_mutex;   //保存视频帧用的锁
    SDL_cond *group_notempty; //保存视频帧用的信号量
    SDL_cond *group_notfull;

    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVStream *pStream;
    AVPacket *pPacket;
    AVFrame *pFrame;
    AVFrame *pFrame_play;
    AVPixelFormat pixel_format;

    // AVDictionary *pAVD;

    int quitFlag = 0;
};
struct AudioInfo
{
    char *deviceName;
    //时钟相关
    double audio_clock; //单位为s

    //音频用的buffer相关
    uint8_t *buffer;
    uint32_t buffer_size;
    uint8_t buffer_pos;

    //音频播放信息相关
    int nb_samples;     //单帧采样数
    int sample_rate;    //采样率
    int channel_layout; //声道布局
    int nb_channels;    //声道数量
    AVSampleFormat sample_format;
    // int out_nb_samples;
    // int out_channel_layout;
    // int out_samples_rate;
    // int out_nb_channels;

    //重采样相关
    SwrContext *pSwrCtx;

    //音频接收包队列
    PacketQueue queue;

    //音频格式上下文信息、流信息、解码器相关
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVStream *pStream;
    AVPacket *pPacket;
    AVFrame *pFrame;
    // AVDictionary *pAVD;
};
struct AVInfo
{
    AudioInfo *audio;
    VideoInfo *video;
};
int startFlag = 0;
int stopFlag = 0;
int quitFlag = 0;
int re = 0;
AVInfo *avinfo;
SDL_Video *sdl;

//获取当前音频播放的时钟
double get_audio_clock()
{
    double pts = 0;
    int hw_buffer_size;
    int bytes;

    // 上一步播放获取的PTS
    pts = avinfo->audio->audio_clock;
    // 音频缓冲区还没有播放的数据
    hw_buffer_size = avinfo->audio->buffer_size - avinfo->audio->buffer_pos;
    // 每秒钟音频播放的字节数
    bytes = avinfo->audio->sample_rate * avinfo->audio->nb_channels * avinfo->audio->sample_format;

    if (bytes != 0)
    {
        pts -= (double)hw_buffer_size / bytes;
    }
    return pts;
}

// 视频同步，获取正确的视频PTS，将结构体中的video_clock实时更新 ///-avinfo参数删除
double synchronize(AVFrame *pFrame, double pts)
{
    double frame_delay, other_delay;

    if (pts != 0)
    {
        avinfo->video->video_clock = pts;
    }
    else
    {
        pts = avinfo->video->video_clock;
    }

    //计算时间延迟
    frame_delay = av_q2d({1, FPS}); // avinfo->video->pCodecCtx->time_base
    //根据公式计算附加延迟
    other_delay = pFrame->repeat_pict * (frame_delay * 0.5);
    //计算总延迟
    frame_delay += other_delay;

    //将信息结构体中的时钟更新
    avinfo->video->video_clock += frame_delay;
    return pts;
}

int packet_queue_init(PacketQueue *queue)
{
    memset(queue, 0, sizeof(PacketQueue));
    queue->queue_mutex = SDL_CreateMutex();
    queue->queue_notempty = SDL_CreateCond();
    queue->queue_notfull = SDL_CreateCond();

    return 0;
}

//创建AVPacket节点 放入queue
int packet_push_queue(PacketQueue *queue, AVPacket *packet)
{

    if (av_packet_make_refcounted(packet) < 0)
    {
        return -1;
    } ///引用！

    SDL_CondWait(queue->queue_notempty, queue->queue_mutex);
    AVPacketList *temp_packet = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!temp_packet)
        return -1;

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
//取出AVPacket 释放AVPacketList节点
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

//将解码的视频帧和pts放入数组中
int frame_push_group(AVFrame *pFrame, double pts)
{
    AVFrame *temp_pFrame = av_frame_alloc();
    if (!temp_pFrame || !pFrame)
        return -1;

    while (avinfo->video->group_size >= VIDEO_PICTURE_QUEUE_SIZE && !quitFlag)
    {
        SDL_CondWait(avinfo->video->group_notfull, avinfo->video->group_mutex);
    }
    SDL_UnlockMutex(avinfo->video->group_mutex);

    if (quitFlag)
        return -1;

    //存储到队列中的Frame赋值
    *temp_pFrame = *pFrame;

    VideoPicture *vp = &avinfo->video->group[avinfo->video->group_push_index];
    vp->pts = pts;
    vp->pFrame = temp_pFrame;
    // vp->width = temp_pFrame->width;
    // vp->height = temp_pFrame->height;

    //索引溢出重新赋0
    if (++avinfo->video->group_push_index == VIDEO_PICTURE_QUEUE_SIZE)
    {
        avinfo->video->group_push_index = 0;
    }

    SDL_LockMutex(avinfo->video->group_mutex);
    avinfo->video->group_size++;
    SDL_CondSignal(avinfo->video->group_notempty);
    SDL_UnlockMutex(avinfo->video->group_mutex);

    return 0;
}

//使用两个出参 将数组中视频帧和pts传出
int frame_pop_group(AVFrame *pFrame, double &pts) //传出参数
{
    while (avinfo->video->group_size <= 0 && !quitFlag)
    {
        SDL_CondWait(avinfo->video->group_notempty, avinfo->video->group_mutex);
    }
    SDL_UnlockMutex(avinfo->video->group_mutex);

    VideoPicture *vp = &avinfo->video->group[avinfo->video->group_pull_index];

    if (quitFlag)
        return -1;

    //出参取值
    *pFrame = *vp->pFrame;
    pts = vp->pts;

    //索引溢出重新赋0
    if (++avinfo->video->group_pull_index == VIDEO_PICTURE_QUEUE_SIZE)
    {
        avinfo->video->group_pull_index = 0;
    }

    SDL_LockMutex(avinfo->video->group_mutex);
    avinfo->video->group_size--;
    SDL_CondSignal(avinfo->video->group_notfull);
    SDL_UnlockMutex(avinfo->video->group_mutex);
    return 0;
}

void temp()
{
    ///接收帧
}

//音频帧解码
//从解码queue中pop出pPacket
int audio_decode(double &pts) //出参pts
{
    int data_size;
    int nb_sample;

    SDL_LockMutex(avinfo->audio->queue.queue_mutex);
    while (avinfo->audio->queue.size <= 0)
    {
        SDL_CondWait(avinfo->audio->queue.queue_notempty, avinfo->audio->queue.queue_mutex);
    }
    packet_pop_queue(&avinfo->audio->queue, avinfo->audio->pPacket);
    avcodec_send_packet(avinfo->audio->pCodecCtx, avinfo->audio->pPacket);
    while (avcodec_receive_frame(avinfo->audio->pCodecCtx, avinfo->audio->pFrame) == 0)
    {
        int len = avinfo->audio->pFrame->pkt_size;
        if (len < 0)
        {
            break;
        }

        data_size = avinfo->audio->pFrame->nb_samples * 2 * 2;
        if (data_size <= avinfo->audio->buffer_size)
            return -1; //应得的解码的音频帧数据 比实际获取的buffer size小

        nb_sample = swr_convert(avinfo->audio->pSwrCtx, &avinfo->audio->buffer, MAX_AUDIO_FRAME_SIZE * 3 / 2,
                                (const uint8_t **)avinfo->audio->pFrame->data, avinfo->audio->pFrame->nb_samples);

        avinfo->audio->buffer = avinfo->audio->pFrame->data[0];
        avinfo->audio->buffer_size = nb_sample * 2 /*nb_channels*/ * 2 /*采样率*/;
        avinfo->audio->buffer_pos = 0;
    }

    //更新音频时钟
    pts = avinfo->audio->audio_clock;
    //追加一帧播放时间
    avinfo->audio->audio_clock += (double)avinfo->audio->pFrame->nb_samples / (double)avinfo->audio->sample_rate;

    if (avinfo->audio->pPacket)
        av_packet_unref(avinfo->audio->pPacket);

    return 0;
}

///-待更新函数
//音频回调函数,播放音频用
void audio_callback(void *udata, Uint8 *stream, int len)
{
    // std::cout << "audio_callback:" << pthread_self() << endl;
    //每次播放都需要清空dst缓冲区
    SDL_memset(stream, 0, len);
    double pts; //音频帧的pts,在audio_decode_frame中作为出参
    while (len > 0)
    {
        if (avinfo->audio->buffer_pos >= avinfo->audio->buffer_size)
        { //当前buffer内容已经播放完，重新获取新的buffer
            audio_decode(pts);
        }
        //如果解码错误，播放静音
        if (avinfo->audio->buffer_size < 0)
        {
            // AAC*2nb_channel*2bytes_per_sample
            avinfo->audio->buffer_size = 1024 * 2 * 2;
            memset(avinfo->audio->buffer, 0, avinfo->audio->buffer_size);
        }
        //每次实际播放的长度
        int actual_len = MIN(len, avinfo->audio->buffer_size);
        SDL_MixAudio(stream, avinfo->audio->buffer + avinfo->audio->buffer_pos, actual_len, SDL_MIX_MAXVOLUME);

        avinfo->audio->buffer_pos += actual_len;
        stream += actual_len;

        avinfo->audio->buffer_size -= actual_len;
        len -= actual_len;
    }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
    SDL_Event event;
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
    return 0; //非重复播放立即结束
}

// 设置定时器,延迟delay秒之后定时器开始发送信号回主线程，主线程开始刷新视频播放
inline static void set_timer(int delay)
{
    SDL_AddTimer(delay, sdl_refresh_timer_cb, NULL); // delay秒后push该event
}

//通过SDL窗口渲染YUV分量  ///-待处理 to resize
void sdl_display(uint8_t *YPlane, int Ylinesize, uint8_t *UPlane, int Ulinesize, uint8_t *VPlane, int Vlinesize)
{
    if (sdl->resize)
    {
    }
    re = SDL_UpdateYUVTexture(sdl->sdl_texture, NULL, YPlane, Ylinesize, UPlane, Ulinesize, VPlane, Vlinesize);

    if (re)
        std::cout << "SDL update error: " << SDL_GetError() << std::endl;
    sdl->sdl_rect->x = 0;
    sdl->sdl_rect->y = 0;
    sdl->sdl_rect->h = sdl->height;
    sdl->sdl_rect->w = sdl->width;

    SDL_LockMutex(sdl->play_mutex);
    SDL_RenderClear(sdl->sdl_renderer);
    SDL_RenderCopy(sdl->sdl_renderer, sdl->sdl_texture, NULL, sdl->sdl_rect);
    SDL_RenderPresent(sdl->sdl_renderer);
    SDL_Delay(FPS);
    SDL_UnlockMutex(sdl->play_mutex);
}

//刷新视频显示，从帧队列中取出视频帧，并渲染到sdl窗口显示
void video_display()
{
    double pts;          //当前帧的pts
    double delay;        //延迟时间
    double actual_delay; //每次用于播放的设置下次定时器的延迟时间
    double sync;         //同步参考
    double ref_clock;    //音频时间
    double diff;         //视频和音频时间差

    if (avinfo->video->group_size == 0)
    {
        //循环等待
        set_timer(1);
    }
    else
    {
        // 从数组中取出一帧视频帧
        frame_pop_group(avinfo->video->pFrame_play, pts);

        //更新当前播放pts
        avinfo->video->video_current_pts = pts;
        // avinfo->video->video_current_pts_time = av_gettime();

        // 当前Frame时间减去上一帧的时间，获取两帧间的时差
        delay = pts - avinfo->video->frame_last_pts;
        if (delay <= 0 || delay >= 1.0)
        {
            //容错处理：延时小于0或大于1秒（太长）都是错误的，将延时时间设置为上一次的延时时间
            delay = avinfo->video->frame_last_delay;
        }
        // 保存delay和pts，等待下次使用
        avinfo->video->frame_last_delay = delay;
        avinfo->video->frame_last_pts = pts;

        ///音视频同步:
        // 获取音频Audio_Clock
        ref_clock = get_audio_clock();
        // 视频帧比与音频帧的差值：正数视频快 负数音频快
        diff = pts - ref_clock;

        //根据延迟时间选择跳过或重复帧
        sync = MAX(delay, AV_NOSYNC_THRESHOLD);
        //延迟差的绝对值需要控制在一定范围内 超出一定范围则认定音视频不同步
        if (fabs(diff) < AV_NOSYNC_THRESHOLD)
        {
            //音频比视频块一个延迟帧 则跳过该视频帧
            if (diff <= -sync)
            {
                delay = 0;
            }
            //音频帧比视频帧慢一个延迟帧及以上 重复帧
            else if (diff >= sync)
            {
                delay = 2 * delay;
            }
        }
        //将参考时钟加上“预延迟”  并根据当前时间获取真正延迟时间
        avinfo->video->frame_timer += delay;
        // 最终真正要延时的时间
        actual_delay = avinfo->video->frame_timer - (av_gettime() / 1000000.0);
        if (actual_delay < 0.010)
        {
            // 延时时间过小就设置最小值
            actual_delay = 0.010;
        }
        // 根据延时时间重新设置定时器，刷新视频
        set_timer((int)(actual_delay * 1000 + 0.5));

        //显示当前帧
        sdl_display(avinfo->video->pFrame_play->data[0], avinfo->video->pFrame_play->linesize[0],  // Y
                    avinfo->video->pFrame_play->data[1], avinfo->video->pFrame_play->linesize[1],  // U
                    avinfo->video->pFrame_play->data[2], avinfo->video->pFrame_play->linesize[2]); // V
    }
}

//视频解码,从队列中取出packet后send&receive,再将pts、pFrame等信息放入帧数组中
int video_decode(void *)
{
    std::cout << "video_decode tid:";
    std::cout << pthread_self() << std::endl;
    double pts;

    while (startFlag == 0)
        ; // start为1开始

    while (1)
    {
        if (quitFlag)
        {
            break;
        }

        while (stopFlag)
        {
            SDL_Delay(10);
        };

        if (packet_pop_queue(&avinfo->video->queue, avinfo->video->pPacket))
        {
            std::cout << "video packet_pop_queue fail" << std::endl;
            av_packet_unref(avinfo->video->pPacket);
            continue;
        }

        re = avcodec_send_packet(avinfo->video->pCodecCtx, avinfo->video->pPacket);
        if (re < 0)
        {
            continue;
        }

        while (avcodec_receive_frame(avinfo->video->pCodecCtx, avinfo->video->pFrame) == 0)
        {
            //注意：如果从Frame中获取到了时间戳，（不是AV_NOPTS_VALUE宏：没有pts或dts数据），直接赋值，否则设为0；
            if (avinfo->video->pFrame->best_effort_timestamp == AV_NOPTS_VALUE)
            {
                pts = 0;
            }
            else
            {
                pts = avinfo->video->pFrame->best_effort_timestamp;
            }

            // pts乘当前时间基数，得到当前帧在视频中的位置
            pts *= av_q2d({1, FPS});
            pts = synchronize(avinfo->video->pFrame, pts); ///-待处理

            //帧放入刷新数组中
            if (frame_push_group(avinfo->video->pFrame, pts) < 0)
            {
                break;
            }
        }
        av_packet_unref(avinfo->video->pPacket);
    }
    SDL_DestroyWindow(sdl->sdl_window);
    SDL_Quit();
    return 0;
}

void init() {}

///-视频重采样需要？
//更新sdl以及输入输出音频信息
int sdl_init()
{
    re = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (re != 0)
        return -1;

    // audio state init
    void (*pcallback)(void *, Uint8 *, int) = &audio_callback;

    avinfo->audio->sample_format = AV_SAMPLE_FMT_S16;
    avinfo->audio->channel_layout = AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT;
    avinfo->audio->nb_channels = av_get_channel_layout_nb_channels(avinfo->audio->channel_layout);
    avinfo->audio->sample_rate = 44100;
    avinfo->audio->nb_samples = 1024;

    sdl->sdl_audiosSpec.freq = avinfo->audio->sample_rate; //采样率
    sdl->sdl_audiosSpec.format = AUDIO_S16SYS;             //播放格式
    sdl->sdl_audiosSpec.channels = avinfo->audio->nb_channels;
    sdl->sdl_audiosSpec.silence = 0; //静音
    sdl->sdl_audiosSpec.samples = avinfo->audio->nb_samples;
    // sdl_audiosSpec.size = 256;
    sdl->sdl_audiosSpec.callback = pcallback;
    sdl->sdl_audiosSpec.userdata = avinfo->audio->pCodecCtx;

    /*pSwrCtx = swr_alloc_set_opts(pSwrCtx,                      //
                                 1, audio_format, sample_rate, //目标
                                 1, audio_format, sample_rate, //源
                                 0, NULL);
    swr_init(pSwrCtx);*/

    // 音频
    if (SDL_OpenAudio(&sdl->sdl_audiosSpec, NULL) != 0)
        return -1;

    avinfo->audio->buffer_size = av_samples_get_buffer_size(NULL, avinfo->audio->channel_layout,
                                                            avinfo->audio->nb_samples, avinfo->audio->sample_format, 1);
    avinfo->audio->buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 3 / 2);

    SDL_PauseAudio(0);

    // 视频
    avinfo->video->pixel_format = avinfo->video->pCodecCtx->pix_fmt;

    // SDL
    sdl->sdl_window = SDL_CreateWindow("new sdl window", 0, 0, sdl->width, sdl->height, SDL_WINDOW_RESIZABLE);
    if (!sdl->sdl_window)
        return -1;

    sdl->sdl_renderer = SDL_CreateRenderer(sdl->sdl_window, -1, 0);
    if (!sdl->sdl_renderer)
        return -1;

    sdl->sdl_texture = SDL_CreateTexture(sdl->sdl_renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                                         sdl->width, sdl->height);
    if (!sdl->sdl_texture)
        return -1;
    sdl->play_mutex = SDL_CreateMutex();
    // video resample
    /*pSwsCtx = sws_getCachedContext(,                                                               //
                                   sdl->width, sdl->height, avinfo->video->pixel_format == -1 ? AV_PIX_FMT_YUV420P :
       avinfo->video->pixel_format, //源 sdl->width, sdl->height, AV_PIX_FMT_YUV420P, //目标 SWS_BICUBIC, //尺寸变化算法
                                   0, 0, 0);
*/
    // video buffer
    avinfo->video->buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, sdl->width, sdl->height, 1);
    avinfo->video->buffer = (uint8_t *)av_malloc(avinfo->video->buffer_size);
    memset(avinfo->video->buffer, 0, avinfo->video->buffer_size);
    av_image_fill_arrays(avinfo->video->pFrame_play->data, avinfo->video->pFrame_play->linesize, avinfo->video->buffer,
                         AV_PIX_FMT_YUV420P, sdl->width, sdl->height, 1);

    return 0;
}
/*
///-记得sdl信号量create
int main()
{
    // sdl = alloc()

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }
    set_timer(1);
    SDL_Event event;
    while (1)
    {
        // 阻塞等待SDL事件
        SDL_WaitEvent(&event);
        //判断SDL事件类型
        switch (event.type)
        {
            case SDL_QUIT: // 退出
                quitFlag = 1;
                goto end;
            case SDL_KEYDOWN:
                //键盘ESC退出
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    quitFlag = 1;
                    goto end;
                }
                break;
            case SDL_USEREVENT: // 定时器刷新事件
                set_timer(1);
                break;
            default:
                break;
        }
    }

end:
    SDL_Quit();
    return 0;
}
*/