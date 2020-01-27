extern "C" {
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
}
#include "main-window.hpp"
#include <QtWidgets>

#include <iostream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
