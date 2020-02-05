#include "main-window.hpp"
#include "ui_main-window.h"

#define WIDTH 352
#define HEIGHT 288
#define PIXEL_WIDTH 320
#define PIXEL_HEIGHT 180
#define BPP 12

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("P2P Live");
    show_dshow_device();

    SDL();
    std::cout << "程序正常运行" << std::endl;
}

void MainWindow::show_dshow_device(){
    AVFormatContext *pAVFC = avformat_alloc_context();
    AVDictionary* pAVD = NULL;
    av_dict_set(&pAVD,"list_devices","true",0);
    AVInputFormat *pAVIF = av_find_input_format("dshow");
    printf("======Device Info=======\n");
    avformat_open_input(&pAVFC,"video=dummy",pAVIF,&pAVD);
    printf("========================\n");
}

//BUGing:indata[0]=Mat.data  insize[0]=Mat.cols*Mat.elemSize();  摄像头输入数据:
int MainWindow::RGBToYUV(int width, int height)
{//在获取数据时循环调用
    SwsContext *vsc = NULL;
    AVFrame *yuv = NULL;
    int inWidth = width, inHeight = height;

    //初始化格式上下文
    vsc = sws_getCachedContext(vsc, inWidth, inHeight, AV_PIX_FMT_RGB24, //源
                               inWidth, inHeight, AV_PIX_FMT_YUV420P,    //目标
                               SWS_BICUBIC,                             //尺寸变化算法
                               0,0,0);
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
    int inSize[AV_NUM_DATA_POINTERS] = {0}; //每行(宽)数据的字节数

    inData[0]=0;//输入数据
    inSize[0]=0;
    int frame_row=0;//无数据源！
    int h = sws_scale(vsc, inData, inSize, 0, frame_row, //源数据
                      yuv->data, yuv->linesize);
    if (h <= 0)//continue; //其中一帧数据有问题 继续处理

    return 0;
}

//解码
int MainWindow::demux(char* pInputFile,char* pOutputVideoFile,char* pOutputAudioFile){
    av_register_all();
	avcodec_register_all();
    AVFormatContext *pAVFC=avformat_alloc_context();
    int re=avformat_open_input(&pAVFC,pInputFile,0,0);
        if(re<0){
            std::cout<<"file open error";
            return -1;
        }
    std::cout<<pAVFC->nb_streams;
    avformat_find_stream_info(pAVFC, NULL);



}


//播放一个yuv文件，尺寸和帧率需要设置
int MainWindow::SDL()
{
    if (SDL_Init(SDL_INIT_VIDEO)) //成功，非0
        std::cout << "SDL init error";
    SDL_Window *screen = SDL_CreateWindow("new sdl window", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE);
    if (screen == NULL)
        std::cout << "SDL window error";

    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);
    if (!renderer)
        std::cout << "SDL render error";

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
    if (!texture)
        std::cout << "SDL texture";

    SDL_Rect rect;
    cout << "0";
    FILE *f = fopen("352_288.yuv", "rb+");
    if (f == NULL)
    {
        cout << "file open error" << endl;
        return -1;
    }
    cout << "1";
    // unsigned char buffer[PIXEL_HEIGHT * PIXEL_WIDTH * BPP / 8];
    unsigned char *YPlane = (unsigned char *)malloc(WIDTH * HEIGHT);
    unsigned char *UPlane = (unsigned char *)malloc(WIDTH * HEIGHT / 4);
    unsigned char *VPlane = (unsigned char *)malloc(WIDTH * HEIGHT / 4);

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

MainWindow::~MainWindow() { delete ui; }
