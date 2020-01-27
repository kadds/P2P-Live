#include "main-window.hpp"
#include "ui_main-window.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("P2P Live");
    
    
    //初始化部分
    av_register_all();                          //注册复用器

    avformat_network_init();                    //初始化网络库

    char *in_url="../test.flv";                 //打开的视频路径
    char *out_url="rtmp://192.168.0.255/live";
    AVFormatContext *in_avfc=NULL;              //解码用的结构体
    AVFormatContext *out_avfc=NULL;



    
    InInit(in_url,in_avfc);
    OutInit(out_url,out_avfc);

    //遍历输入的AVStream,创建新流
    for(int i=0;i<in_avfc->nb_streams;i++){
        AVStream *out_Stream=avformat_new_stream(out_avfc,in_avfc->streams[i]->codec->codec);//创建新流,arg2:输入流的第i个流的AVCodeContext的AVCode 即编码类型
        if(out_Stream==NULL)
            ErrorExit(0);
        //复制配置信息：输入编码器->输出流
        int re = avcodec_copy_context(out_Stream->codec,in_avfc->streams[i]->codec);
        if(re!=0)
            ErrorExit(re);
        out_Stream->codec->codec_tag=0;
    }

    cout<<"打印输出流:"<<endl;
    av_dump_format(out_avfc,0,out_url,1);//打印


    //cout<<"程序正常运行"<<endl;
}

int MainWindow::InInit(char* in_url,AVFormatContext *&in_avfc){//输入流
    //打开文件 根据文件格式自动解封协议头
    int re=avformat_open_input(&in_avfc,in_url,0,0);    //打开 返回0成功；注意这里第一个参数是**
    if(re!=0)   ErrorExit(re);  
    cout<<"打开"<<in_url<<"成功"<<endl;

    //获取音视频流信息
    re=avformat_find_stream_info(in_avfc,0);
    if(re!=0)   ErrorExit(re);  
    //打印流信息
    cout<<"打印输入流:"<<endl;
    av_dump_format(in_avfc,0,in_url,0);
    return 0;
}

int MainWindow::OutInit(char* out_url,AVFormatContext *&out_avfc){//输出流
    int re = avformat_alloc_output_context2(&out_avfc,0,"flv",out_url);//创建输出流上下文
    if(out_avfc==NULL)
        return ErrorExit(re);
    cout<<"输出流创建成功"<<endl; 
    return 0;

}





int MainWindow::ErrorExit(int errorNum){
    char errorInfo[1024];
    av_strerror(errorNum,errorInfo,sizeof(errorInfo));//提取错误信息,打印退出
    cout<<" Error: "<<errorInfo<<endl;
    exit(-1);
    //return -1;
}

MainWindow::~MainWindow() { delete ui; }
