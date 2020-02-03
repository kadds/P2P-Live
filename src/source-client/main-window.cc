#include "main-window.hpp"
#include "ui_main-window.h"

#define WIDTH 352
#define HEIGHT 288
#define PIXEL_WIDTH 320
#define PIXEL_HEIGHT 180
#define BPP 12

// const int WIDTH = 800, HEIGHT = 600, PIXEL_WIDTH = 320, PIXEL_HEIGHT = 180, BPP = 12;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("P2P Live");
    SDL();
    cout << "程序正常运行" << endl;
}
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
        cout << sizeY << " " << sizeU << " " << sizeV << endl;

        int re = SDL_UpdateYUVTexture(texture, NULL, YPlane, WIDTH, UPlane, WIDTH / 2, VPlane, WIDTH / 2);
        if (re)
            cout << "SDL update error: " << SDL_GetError() << endl;
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
