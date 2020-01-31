#include "main-window.hpp"
#include "ui_main-window.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("P2P Live");

    //初始化部分
    av_register_all(); //注册复用器

    avformat_network_init();      //初始化网络库
    int re = 0;                   // fun result
    char *in_url = "../test.flv"; //打开的视频路径
    char *out_url = "rtmp://192.168.0.255/live";
    AVFormatContext *in_avfc = NULL; //解码用的结构体
    AVFormatContext *out_avfc = NULL;

    InInit(in_url, in_avfc);
    OutInit(out_url, out_avfc);
    cout << "初始化成功" << endl;
    //遍历输入的AVStream,创建新流
    for (int i = 0; i < in_avfc->nb_streams; i++)
    {
        AVStream *out_Stream = avformat_new_stream(
            out_avfc,
            in_avfc->streams[i]->codec->codec); //创建新流,arg2:输入流的第i个流的AVCodeContext的AVCode 即编码类型
        if (out_Stream == NULL)
            ErrorExit(0);
        //复制配置信息：输入编码器->输出流
        // int re = avcodec_copy_context(out_Stream->codec,in_avfc->streams[i]->codec);//codec已经是过时的，但如果是mp4
        // (不用该方式)写入头信息,会提示现有格式不支持(ffmpeg库不支持mp4转flv)
        re = avcodec_parameters_copy(out_Stream->codecpar, in_avfc->streams[i]->codecpar);
        if (re != 0)
            ErrorExit(re);
        out_Stream->codec->codec_tag = 0;
    }

    //----此处有bug ：由于输出url有问题

    //打开IO
    re = avio_open(&out_avfc->pb, out_url, AVIO_FLAG_WRITE); // AVFormatContext结构体中的*pb作为IO上下文传出
    if (out_avfc->pb == NULL)
        ErrorExit(re);
    //写入头信息
    re = avformat_write_header(out_avfc, 0);
    if (re < 0)
        ErrorExit(re);

    //推流每一帧
    AVPacket avp;
    int64_t start = av_gettime(); //初始时间戳
    while (1)
    {
        re = av_read_frame(in_avfc, &avp); //每读一帧 存放在arg2-AVPacket中
        if (re != 0)
            break; //出错

        //计算pts dts
        AVRational in_timebase = in_avfc->streams[avp.stream_index]->time_base;
        AVRational out_timebase = out_avfc->streams[avp.stream_index]->time_base;
        avp.pts = av_rescale_q_rnd(
            avp.pts, in_timebase, out_timebase,
            (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)); // ffmpeg提供的输入输出时间戳转换函数
        avp.dts = av_rescale_q_rnd(avp.pts, in_timebase, out_timebase,
                                   (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        avp.duration = av_rescale_q_rnd(avp.pts, in_timebase, out_timebase,
                                        (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        avp.pos = -1;

        //判断视频帧还是音频帧
        if (in_avfc->streams[avp.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) //是视频类型
        {
            AVRational tb = in_avfc->streams[avp.stream_index]->time_base; //运算的时间基数
            int64_t now = av_gettime() - start;
            int64_t dts = 0;
            if (tb.den)                                          //防止除0异常
                dts = avp.dts * (1000 * 1000 * tb.num / tb.den); //单位：微秒

            if (dts > now)            //时间戳比当前现实时间快,处理流媒体服务器延时
                av_usleep(dts - now); //等待时间差
        }

        re = av_interleaved_write_frame(out_avfc, &avp); //自动排序发送，自动释放
        if (re < 0)
            ErrorExit(0);

        // av_packet_unref(&avp);//上面步骤已经包含
    }

    cout << "程序正常运行" << endl;
}

int MainWindow::InInit(char *in_url, AVFormatContext *&in_avfc)
{ //输入流
    //打开文件 根据文件格式自动解封协议头
    int re = avformat_open_input(&in_avfc, in_url, 0, 0); //打开 返回0成功；注意这里第一个参数是**
    if (re != 0)
        ErrorExit(re);
    cout << "打开" << in_url << "成功" << endl;

    //获取音视频流信息
    re = avformat_find_stream_info(in_avfc, 0);
    if (re != 0)
        ErrorExit(re);
    //打印流信息
    cout << "打印输入流:" << endl;
    av_dump_format(in_avfc, 0, in_url, 0);
    return 0;
}

int MainWindow::OutInit(char *out_url, AVFormatContext *&out_avfc)
{                                                                          //输出流
    int re = avformat_alloc_output_context2(&out_avfc, 0, "flv", out_url); //创建输出流上下文
    if (out_avfc == NULL)
        return ErrorExit(re);
    cout << "输出流创建成功" << endl;

    cout << "打印输出流:" << endl;
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
