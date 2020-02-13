#include "main-window.hpp"
#include "ui_main-window.h"

#define WIDTH 352
#define HEIGHT 288
#define PIXEL_WIDTH 320
#define PIXEL_HEIGHT 180
#define BPP 12

#define MAX_AUDIO_FRAME_SIZE 192000

int codec_id_Video, codec_id_Audio;     // SDL播放辨别的Video格式
int index_Video = -1, index_Audio = -1; //解码时对应的视频音频标记（video是1，audio是0
int width_Video, height_Video;
SDL_AudioSpec sdl_audios ;
SDL_Window *sdl_screen = NULL;
SDL_Renderer *sdl_renderer = NULL;
SDL_Texture *sdl_texture = NULL;
SDL_Rect sdl_rect;

Uint8 *MainWindow::audio_chunk=0;
Uint32 MainWindow::audio_len=0;
Uint8 *MainWindow::audio_pos=0;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // ui->setupUi(this);
    // this->setWindowTitle("P2P Live");
    show_dshow_device();
    demux("test2.mp4", "outvideo2.yuv", "outaudio2.pcm");
    // SDL();
    // RGBToYUV(320,568);
    std::cout << "程序正常运行" << std::endl;
}


void MainWindow::show_dshow_device()
{
    AVFormatContext *pAVFC = avformat_alloc_context();
    AVDictionary *pAVD = NULL;
    av_dict_set(&pAVD, "list_devices", "true", 0);
    AVInputFormat *pAVIF = av_find_input_format("dshow");
    printf("======Device Info=======\n");
    avformat_open_input(&pAVFC, "video=dummy", pAVIF, &pAVD);
    printf("========================\n");
}

// BUGing:indata[0]=Mat.data  insize[0]=Mat.cols*Mat.elemSize();  摄像头输入数据:
int MainWindow::RGBToYUV(int width, int height)
{ //在获取数据时循环调用
    SwsContext *vsc = NULL;
    AVFrame *yuv = NULL;
    int inWidth = width, inHeight = height;

    //初始化格式上下文
    vsc = sws_getCachedContext(vsc, inWidth, inHeight, AV_PIX_FMT_RGB24, //源
                               inWidth, inHeight, AV_PIX_FMT_YUV420P,    //目标
                               SWS_BICUBIC,                              //尺寸变化算法
                               0, 0, 0);
    if (!vsc)
    {
        std::cout << "SwsContext init error";
        return -1;
    }
    //输出的数据结构
    yuv = av_frame_alloc(); //分配对象空间
    yuv->format = AV_PIX_FMT_YUV420P;
    yuv->width = inWidth;
    yuv->height = inHeight;
    yuv->pts = 0;                          //显示时间戳
    int re = av_frame_get_buffer(yuv, 32); //分配yuv空间
    if (re != 0)
        ErrorExit(re);

    //输入的数据结构
    uint8_t *inData[AV_NUM_DATA_POINTERS] = {0}; //存放rgb数据
    int inSize[AV_NUM_DATA_POINTERS] = {0};      //每行(宽)数据的字节数

    inData[0] = 0; //输入数据
    inSize[0] = 0;
    int frame_row = 0;                                   //无数据源！
    int h = sws_scale(vsc, inData, inSize, 0, frame_row, //源数据
                      yuv->data, yuv->linesize);
    if (h <= 0) // continue; //其中一帧数据有问题 继续处理

        return 0;
}
inline double MainWindow::r2d(AVRational r) { return r.num == 0 || r.den == 0 ? 0.0 : (double)r.num / (double)r.den; }
//解码
int MainWindow::demux(char *pInputFile, char *pOutputFileName_Video, char *pOutputFileName_Audio)
{
    AVFormatContext *pAVFC = avformat_alloc_context();
    AVCodecContext *pAVCC_Video;
    AVCodecContext *pAVCC_Audio;
    AVPacket *pAVP = av_packet_alloc();
    AVFrame *pAVF_Audio = av_frame_alloc();
    AVFrame *pAVF_Video = av_frame_alloc();
    AVFrame *pAVF_Video_YUV = av_frame_alloc();
    int fps=0;

    // av_register_all();//旧版本函数已弃用
    // avcodec_register_all();//旧版本函数已弃用
    int re = avformat_open_input(&pAVFC, pInputFile, 0, 0);
    if (re < 0)
    {
        ErrorExit("input file open error");
        return -1;
    }
    std::cout << "文件流个数nb_streams:" << pAVFC->nb_streams << std::endl;
    std::cout << "平均比特率bit_rate:" << pAVFC->bit_rate << std::endl;

    avformat_find_stream_info(pAVFC, NULL); //获取更多信息

    for (int i = 0; i < pAVFC->nb_streams; i++)
    {
        AVStream *pAVS = pAVFC->streams[i];
        AVCodec *pAVC = avcodec_find_decoder(pAVS->codecpar->codec_id);//查找当前流的编码器
        if (pAVC == NULL)
            ErrorExit("查找编码器失败");
        else cout<<"success codec";

        if (pAVS->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            string format; //音频采样格式,压缩编码格式
            ///编码器
            pAVCC_Audio = avcodec_alloc_context3(pAVC);
            //pAVCC_Video = pAVFC->streams[i]->codec;
            re = avcodec_parameters_to_context(pAVCC_Audio, pAVS->codecpar);
            if (!pAVCC_Audio)
                ErrorExit("分配编码器失败");
            int re = avcodec_open2(pAVCC_Audio, pAVC, nullptr);
            if (re != 0)
            {
                avcodec_free_context(&pAVCC_Audio);
                ErrorExit("打开编码器失败");
            }
            else
                cout << "打开编码器成功" << endl;



            ///打印内容赋值
            if (pAVS->codecpar->format == AV_SAMPLE_FMT_S16)
                format = "AV_SAMPLE_FMT_S16";
            else if (pAVS->codecpar->format == AV_SAMPLE_FMT_S16P)
                format = "AV_SAMPLE_FMT_S16P";
            else if(pAVS->codecpar->format == AV_SAMPLE_FMT_FLT)
                format = "AV_SAMPLE_FMT_FLT";
            else if (pAVS->codecpar->format == AV_SAMPLE_FMT_FLTP)
                format = "AV_SAMPLE_FMT_FLTP";
            index_Audio = pAVS->index;
            int durationAudio = (pAVS->duration) * r2d(pAVS->time_base);
            int s = durationAudio % 60;
            int m = (durationAudio % 3600) / 60;
            int h = durationAudio / 3600;

            //信息打印
            cout << "========音频流信息========" << endl;
            cout << "音频采样率:" << pAVCC_Audio->sample_rate << "Hz" << endl;
            cout << "音频信道数目:" << pAVS->codecpar->channels << endl;
            cout << "音频采样格式:" << format << endl;
            cout << "音频压缩编码格式:" << pAVC->name << endl;
            cout << "音频总时长：" << h << "时" << m << "分" << s << "秒" << endl;
        }
        else if (pAVS->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) //如果是视频流，则打印视频的信息
        {
            ///分配视频编码器
            pAVCC_Video = avcodec_alloc_context3(pAVC);
            //pAVCC_Video = pAVFC->streams[i]->codec;
            re = avcodec_parameters_to_context(pAVCC_Video, pAVS->codecpar);
            if (!pAVCC_Video)
                ErrorExit("分配编码器失败");
            int re = avcodec_open2(pAVCC_Video, pAVC, nullptr);
            if (re != 0)
            {
                avcodec_free_context(&pAVCC_Video);
                ErrorExit("打开编码器失败");
            }
            else
                cout << "打开编码器成功" << endl;

            ///打印内容赋值
            index_Video = pAVS->index;
            fps= r2d(pAVS->avg_frame_rate);
            int durationVideo = (pAVS->duration) * r2d(pAVS->time_base);
            int s = durationVideo % 60;
            int m = (durationVideo % 3600) / 60;
            int h = durationVideo / 3600;
            height_Video = pAVS->codecpar->height;
            width_Video = pAVS->codecpar->width;

            cout << "========视频流信息========" << endl;
            cout << "视频帧率:" << fps << "fps" << endl; //表示每秒出现多少帧
            cout << "帧高度:" << pAVS->codecpar->height << endl;
            cout << "帧宽度:" << pAVS->codecpar->width << endl;
            cout << "视频压缩编码格式:" << pAVC->name << endl;
            cout << "视频总时长:" << h << "时" << m << "分" << s << "秒" << endl;

        }

            /**
             * 编码内容
            pAVCC_Video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //便于输出获取
            pAVCC_Video->codec_id = pAVC->id;
            pAVCC_Video->thread_count = 2; //线程数量

            //仅视频：
            pAVCC_Video->bit_rate = 200 * 1024 * 8; //压缩后每秒视频的比特位大小：200kB
            pAVCC_Video->width = WIDTH;
            pAVCC_Video->height = HEIGHT;
            pAVCC_Video->time_base = {1, 25}; // 1,fps
            pAVCC_Video->framerate = {25, 1}; // fps,1

            //画面组大小，多少帧一个关键帧
            pAVCC_Video->gop_size = 50;
            pAVCC_Video->max_b_frames = 0; //最大b帧
            pAVCC_Video->pix_fmt = AV_PIX_FMT_YUV420P;*/

    }

    av_init_packet(pAVP);
    /**
     * @brief pSC_Video
     */
    SwsContext *pSC_Video = NULL;
    pSC_Video = sws_getCachedContext(pSC_Video, pAVCC_Video->width, pAVCC_Video->height, pAVCC_Video->pix_fmt, //源
                               pAVCC_Video->width, pAVCC_Video->height, AV_PIX_FMT_YUV420P,  //目标
                               SWS_BICUBIC,                                      //尺寸变化算法
                               0, 0, 0);
    /**
     * @brief pSC_Audio
     */
    cout<<"---开始分配音频上下文---"<<endl;
    SwrContext *pSC_Audio=swr_alloc();
    //分配参数
    AVSampleFormat AVSF_out=AV_SAMPLE_FMT_S16;
    AVSampleFormat AVSF_in=pAVCC_Audio->sample_fmt;
    int inSampleRate=pAVCC_Audio->sample_rate;
    int outSampleRate=pAVCC_Audio->sample_rate;
    int outSamplesNumber=pAVCC_Audio->frame_size;//假定输出=输入一帧数据量
    int inChannelLayout=av_get_default_channel_layout(pAVCC_Audio->channels);

    uint64_t outChannelLayout=AV_CH_FRONT_LEFT;//输出通道数
    int outChannelLayoutNumber=av_get_channel_layout_nb_channels(outChannelLayout);
    cout<<outChannelLayout<<"---"<<inChannelLayout<<"---"<<outChannelLayout<<endl;
    pSC_Audio=swr_alloc_set_opts(pSC_Audio,
                                 1,AVSF_out,outSampleRate,
                                 1,AVSF_in,inSampleRate,
                                 0,NULL);

    swr_init(pSC_Audio);


    cout<<"---音频上下文over---"<<endl;
    /**
     * @brief outBuffer_Video
     * @brief outBuffer_Audio
     * 输入输出文件打开
     */


    cout<<"---打开输入输出文件---"<<endl;
    FILE *pOutputFile_Audio=fopen(pOutputFileName_Audio,"wb");
    FILE *pOutputFile_Video=fopen(pOutputFileName_Video,"wb");
    if(!pOutputFile_Audio||!pOutputFile_Video)ErrorExit("输入输出文件打开失败");

    int outBufferSize_Video=avpicture_get_size(AV_PIX_FMT_YUV420P, pAVCC_Video->width, pAVCC_Video->height);
    uint8_t *outBuffer_Video = (uint8_t *)av_malloc(outBufferSize_Video);
    avpicture_fill((AVPicture *)pAVF_Video_YUV, outBuffer_Video, AV_PIX_FMT_YUV420P, pAVCC_Video->width, pAVCC_Video->height);

    int outBufferSize_Audio=av_samples_get_buffer_size(NULL,outChannelLayoutNumber,outSamplesNumber,AVSF_out, 1);
    uint8_t *outBuffer_Audio=(uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    cout<<"---打开输入输出over---"<<endl;

    /**
     * SDL INIT
     * SDL_AudioSpec
     */
    cout<<"---sdl init---"<<endl;
    void (*pcallback)(void*,Uint8*,int) = &SDLAudio;

    sdl_audios.freq=outSampleRate;//采样率
    sdl_audios.format=AUDIO_S16SYS;
    sdl_audios.channels=outChannelLayout;
    sdl_audios.silence=0;//静音
    sdl_audios.samples=outSamplesNumber;
    sdl_audios.callback=pcallback;
    sdl_audios.userdata = pAVCC_Audio;

    if (SDL_OpenAudio(&sdl_audios, NULL)!=0)
        ErrorExit("can't open audio.\n");

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER)) //成功，非0
        ErrorExit("SDL init error");
    sdl_screen = SDL_CreateWindow("new sdl window", 0, 0, pAVCC_Video->width, pAVCC_Video->height, SDL_WINDOW_RESIZABLE);
    if (sdl_screen == NULL)
        ErrorExit("SDL window error");

    sdl_renderer = SDL_CreateRenderer(sdl_screen, -1, 0);
    if (!sdl_renderer)
        ErrorExit("SDL render error");

    sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_TARGET, pAVCC_Video->width, pAVCC_Video->height);
    if (!sdl_texture)
        ErrorExit("SDL sdl_texture");

    cout<<SDL_GetError();
    SDL_PauseAudio(0);

    while (av_read_frame(pAVFC, pAVP) >= 0)
    {

        if (pAVP->stream_index == index_Video)
        {
            // int frameFinished=0;
            // int len=avcodec_decode_video2(pAVCC_Video, pAVF_Video, &frameFinished, pAVP);弃用
            re = avcodec_send_packet(pAVCC_Video, pAVP);//re==0:success
            av_packet_unref(pAVP);
            re = avcodec_receive_frame(pAVCC_Video, pAVF_Video);//re==0:success
            if (re == 0)
            {

                sws_scale(pSC_Video, pAVF_Video->data, pAVF_Video->linesize, 0, pAVCC_Video->height, pAVF_Video_YUV->data, pAVF_Video_YUV->linesize);
                fwrite(pAVF_Video_YUV->data[0],1,pAVCC_Video->width*pAVCC_Video->height,pOutputFile_Video);//输出Y数据
                fwrite(pAVF_Video_YUV->data[1],1,pAVCC_Video->width*pAVCC_Video->height/4,pOutputFile_Video);//输出U数据
                fwrite(pAVF_Video_YUV->data[2],1,pAVCC_Video->width*pAVCC_Video->height/4,pOutputFile_Video);//输出V数据
                SDL(pAVF_Video_YUV->data[0], pAVF_Video_YUV->data[1], pAVF_Video_YUV->data[2], pAVCC_Video->width, pAVCC_Video->height,fps);

            }
        }
        else if (pAVP->stream_index == index_Audio)
        {
            re = avcodec_send_packet(pAVCC_Audio, pAVP);//re==0:success
            av_packet_unref(pAVP);
            re = avcodec_receive_frame(pAVCC_Audio, pAVF_Audio);//re==0:success

            re=swr_convert(pSC_Audio,&outBuffer_Audio, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pAVF_Audio->data , pAVF_Audio->nb_samples);
            outBufferSize_Audio = re * outChannelLayoutNumber * av_get_bytes_per_sample(AVSF_out);

            fwrite(outBuffer_Audio,1,outBufferSize_Audio,pOutputFile_Audio);
            while(audio_len>0)//Wait until finish
                     SDL_Delay(1);
            //Set audio buffer (PCM data)
            audio_chunk = (Uint8*) outBuffer_Audio;
            //Audio buffer length
            audio_len = outBufferSize_Audio;
            audio_pos = audio_chunk;
cout<<re;
        }
        else
            cout << "其他帧";
    }

    SDL_DestroyWindow(sdl_screen);
    SDL_Quit();
    return 0;
}

//播放yuv文件时：尺寸和帧率需要设置
int MainWindow::SDL(uint8_t *YPlane, uint8_t *UPlane, uint8_t *VPlane, int width, int height, int fps)
{
    //文件直接播放改为ffmpeg解码播放

    int re = SDL_UpdateYUVTexture(sdl_texture, NULL, YPlane, width, UPlane, width / 2, VPlane, width / 2);
    if (re)
        std::cout << "SDL update error: " << SDL_GetError() << std::endl;
    sdl_rect.x = 0;
    sdl_rect.y = 0;
    sdl_rect.h = height;
    sdl_rect.w = width;

    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, &sdl_rect);
    SDL_RenderPresent(sdl_renderer);
    SDL_Delay(fps);
    return 0;
}

int MainWindow::InInit(char *in_url, AVFormatContext *&in_avfc)
{
    int re = avformat_open_input(&in_avfc, in_url, 0, 0);
    if (re != 0)
        ErrorExit(re);
    cout << "open" << in_url << "success" << endl;

    re = avformat_find_stream_info(in_avfc, 0);
    if (re != 0)
        ErrorExit(re);

    cout << "in:" << endl;
    av_dump_format(in_avfc, 0, in_url, 0);
    return 0;
}


int MainWindow::OutInit(char *out_url, AVFormatContext *&out_avfc)
{
    int re = avformat_alloc_output_context2(&out_avfc, 0, "flv", out_url);
    if (out_avfc == NULL)
        return ErrorExit(re);
    cout << "out success" << endl;

    cout << "out:" << endl;
    av_dump_format(out_avfc, 0, out_url, 1); //打印
    return 0;
}

int MainWindow::ErrorExit(int errorNum)
{
    char *errorInfo;
    av_strerror(errorNum, errorInfo, sizeof(errorInfo)); //提取错误信息,打印退出
    cout << " Error: " << errorInfo << endl;
    exit(-1);
    // return -1;
}
int MainWindow::ErrorExit(string errorStr)
{
    cout << " Error: " << errorStr << endl;
    exit(-1);
}
MainWindow::~MainWindow() { delete ui; }
