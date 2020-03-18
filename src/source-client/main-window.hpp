#pragma once
#include <QMainWindow>
#include <iostream>
#include <string>
#include <thread>
extern "C" {
#include <libavformat/avformat.h>
}

namespace Ui
{
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void video_main();
    void audio_main();
    void get_device();

  private:
    Ui::MainWindow *ui;
    std::thread video_thread, audio_thread;
    int target_width;
    int target_height;
    bool run;
    AVFormatContext *video_format_ctx, *audio_format_ctx;
    int videoindex;
    int audioindex;
    AVCodecContext *video_codec, *audio_codec;
};
