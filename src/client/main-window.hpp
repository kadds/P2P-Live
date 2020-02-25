#pragma once
#include <QMainWindow>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QThread>
#include <thread>

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
    void device_main();

  private:
    Ui::MainWindow *ui;
    std::thread thread;
    int target_width;
    int target_height;
    bool run;
};
