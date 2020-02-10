#include "main-window.hpp"
#include "ui_main-window.h"

#define WIDTH 352
#define HEIGHT 288
#define PIXEL_WIDTH 320
#define PIXEL_HEIGHT 180
#define BPP 12

int codec_id_Video, codec_id_Audio;     // SDL播放辨别的Video格式
int index_Video = -1, index_Audio = -1; //解码时对应的视频音频标记（video是1，audio是0
int width_Video, height_Video;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // ui->setupUi(this);
    // this->setWindowTitle("P2P Live");
    show_dshow_device();
    demux("test.mp4", "outvideo", "outaudio");
    // SDL();
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
    if (!re)
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
int MainWindow::demux(char *pInputFile, char *pOutputVideoFile, char *pOutputAudioFile)
{
    AVFormatContext *pAVFC = avformat_alloc_context();
    AVCodecContext *pAVCC;
    AVPacket *pAVP=av_packet_alloc();
AVFrame *pAVF=av_frame_alloc();

    //av_register_all();
    //avcodec_register_all();
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

        if (pAVS->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            string format, codec_id; //音频采样格式,压缩编码格式

            /*if (AV_CODEC_ID_AAC == pAVS->codecpar->codec_id){
                codec_id_Audio=AV_CODEC_ID_AAC;
                codec_id = "AAC";
            }
            else if (AV_CODEC_ID_MP3 == pAVS->codecpar->codec_id){
                codec_id_Audio=AV_CODEC_ID_MP3;
                codec_id = "MP3";
            }*/

            if (pAVS->codecpar->format == AV_SAMPLE_FMT_FLTP)
                format = "AV_SAMPLE_FMT_FLTP";
            else if (pAVS->codecpar->format == AV_SAMPLE_FMT_S16P)
                format = "AV_SAMPLE_FMT_S16P";

            index_Audio = pAVS->index;
            int durationAudio = (pAVS->duration) * r2d(pAVS->time_base);
            int s = durationAudio % 60;
            int m = (durationAudio % 3600) / 60;
            int h = durationAudio / 3600;

            //信息打印
            cout << "音频信息:" << endl;
            cout << "音频采样率:" << pAVS->codecpar->sample_rate << "Hz" << endl;
            cout << "音频信道数目:" << pAVS->codecpar->channels << endl;
            cout << "音频采样格式:" << format << endl;
            // cout << "音频压缩编码格式:" << codec_id << endl;
            cout << "音频总时长：" << h << "时" << m << "分" << s << "秒" << endl;
        }
        else if (AVMEDIA_TYPE_VIDEO == pAVS->codecpar->codec_type) //如果是视频流，则打印视频的信息
        {
            /*string codec_id; //压缩编码格式
            if (pAVS->codecpar->codec_id==AV_CODEC_ID_MPEG4  ){
                codec_id_Video=AV_CODEC_ID_MPEG4;
                codec_id = "MPEG4";
            }*/

            index_Video = pAVS->index;
            int durationVideo = (pAVS->duration) * r2d(pAVS->time_base);
            int s = durationVideo % 60;
            int m = (durationVideo % 3600) / 60;
            int h = durationVideo / 3600;
            height_Video = pAVS->codecpar->height;
            width_Video = pAVS->codecpar->width;

            cout << "视频信息:" << endl;
            cout << "视频帧率:" << r2d(pAVS->avg_frame_rate) << "fps" << endl; //表示每秒出现多少帧
            cout << "帧高度:" << pAVS->codecpar->height << endl;
            cout << "帧宽度:" << pAVS->codecpar->width << endl;
            cout << "视频总时长:" << h << "时" << m << "分" << s << "秒" << endl;
        }

        AVCodec *pAVC = avcodec_find_decoder(pAVS->codecpar->codec_id);
        if (pAVC == NULL)
            ErrorExit("查找编码器失败");
        else
            std::cout << "压缩编码格式:" << pAVC->name << std::endl;

        //if(AVMEDIA_TYPE_VIDEO == pAVS->codecpar->codec_type){
            pAVCC = avcodec_alloc_context3(pAVC);
            re = avcodec_parameters_to_context(pAVCC,pAVS->codecpar);
            if (!pAVCC)
                ErrorExit("分配编码器失败");
            pAVCC->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //便于输出获取
            pAVCC->codec_id = pAVC->id;
            pAVCC->thread_count = 2; //线程数量

            //仅视频：
            pAVCC->bit_rate = 200 * 1024 * 8; //压缩后每秒视频的比特位大小：200kB
            pAVCC->width = WIDTH;
            pAVCC->height = HEIGHT;
            pAVCC->time_base = {1, 25}; // 1,fps
            pAVCC->framerate = {25, 1}; // fps,1

            //画面组大小，多少帧一个关键帧
            pAVCC->gop_size = 50;
            pAVCC->max_b_frames = 0; //最大b帧
            pAVCC->pix_fmt = AV_PIX_FMT_YUV420P;

            int re = avcodec_open2(pAVCC, pAVC, nullptr);
            if (re != 0)
            {
                avcodec_free_context(&pAVCC);
                ErrorExit("打开编码器失败");
            }
            else
                cout<<"打开编码器成功"<<endl;

    }

    av_init_packet(pAVP);
    //pAVP->data = NULL;
    //pAVP->size = 0;


    int i=0;
    while (av_read_frame(pAVFC, pAVP) >= 0)
    {
        if (pAVP->stream_index == index_Video){
            int frameFinished=0;
            int len=avcodec_decode_video2(pAVCC, pAVF, &frameFinished, pAVP);
            cout<<len<<" "<<frameFinished<<endl;
        }
        else if (pAVP->stream_index == index_Audio)
        {
        }
        else
            cout << "其他帧";

    }

    return 0;
}

//播放yuv文件时：尺寸和帧率需要设置
int MainWindow::SDL(FILE *f, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO)) //成功，非0
        ErrorExit("SDL init error");
    SDL_Window *screen = SDL_CreateWindow("new sdl window", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE);
    if (screen == NULL)
        ErrorExit("SDL window error");

    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);
    if (!renderer)
        ErrorExit("SDL render error");

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
    if (!texture)
        ErrorExit("SDL texture");

    SDL_Rect rect;

    // FILE *f = fopen("352_288.yuv", "rb+");
    // if (f == NULL)  ErrorExit( "file open error");
    // if(codec_id_Video)

    // unsigned char buffer[PIXEL_HEIGHT * PIXEL_WIDTH * BPP / 8];
    unsigned char *YPlane = (unsigned char *)malloc(WIDTH * HEIGHT);
    unsigned char *UPlane = (unsigned char *)malloc(WIDTH * HEIGHT / 4);
    unsigned char *VPlane = (unsigned char *)malloc(WIDTH * HEIGHT / 4);

    unsigned char *videoDstData[4];
    int videoLineSize[4];
    int videoBufferSize;

    while (1)
    {
        int sizeY = fread(YPlane, 1, WIDTH * HEIGHT, f);
        int sizeU = fread(UPlane, 1, WIDTH * HEIGHT / 4, f);
        int sizeV = fread(VPlane, 1, WIDTH * HEIGHT / 4, f);
        if (!sizeY || !sizeU || !sizeV)
            break;
        std::cout << sizeY << " " << sizeU << " " << sizeV << std::endl;

        int re = SDL_UpdateYUVTexture(texture, NULL, YPlane, WIDTH, UPlane, WIDTH / 2, VPlane, WIDTH / 2);
        if (re)
            std::cout << "SDL update error: " << SDL_GetError() << std::endl;
        rect.x = 0;
        rect.y = 0;
        rect.h = HEIGHT;
        rect.w = WIDTH;

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(25);
    }
    fclose(f);
    SDL_DestroyWindow(screen);
    SDL_Quit();
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
