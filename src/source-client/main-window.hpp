#pragma once
#include <QMainWindow>
#include <iostream>
#include <string>
#include <thread>
extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
using namespace std;

namespace Ui
{
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = 0);
    //输入流 输出流 错误退出
    int InInit(char *in_url, AVFormatContext *&in_avfc);
    int OutInit(char *out_url, AVFormatContext *&out_avfc);
    int ErrorExit(int errorNum);
    int ErrorExit(string errorStr);

    void show_dshow_device();
    int RGBToYUV(int width, int height);

    inline double r2d(AVRational r);
    int demux(char *pInputFile, char *pOutputVideoFile, char *pOutputAudioFile);
    int SDL(uint8_t *YPlane, uint8_t *UPlane, uint8_t *VPlane, int width, int height, int fps);

    static Uint8 *audio_chunk;
    static Uint32 audio_len;
    static Uint8 *audio_pos;
    static void SDLAudio(void *udata, Uint8 *stream, int len)
    {
        // cout << "回调函数" << pthread_self() << endl;
        SDL_memset(stream, 0, len);
        while (len > 0)
        {
            if (audio_len == 0)
                continue;
            int temp = (len > audio_len ? audio_len : len);
            SDL_MixAudio(stream, audio_pos, temp, SDL_MIX_MAXVOLUME);
            cout << "len temp audio_len:" << len << " " << temp << " " << audio_len << endl;
            audio_pos += temp;
            stream += temp;

            audio_len -= temp;
            len -= temp;
        }
        // cout << "播放完了" << endl;
        /*
    if (audio_len == 0)
        return;

    len = (len > audio_len ? audio_len : len); /*  Mix  as  much  data  as  possible  *

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;*/
    }

    ~MainWindow();

  private:
    Ui::MainWindow *ui;
};
