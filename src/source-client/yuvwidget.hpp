#pragma once
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>

QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)
QT_FORWARD_DECLARE_CLASS(QOpenGLTexture)

class yuvWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
  public:
    yuvWidget(QWidget *parent = 0);
    ~yuvWidget();

  public:
    Q_INVOKABLE void update_frame(std::shared_ptr<char[]> data, int width, int height); //显示一帧Yuv图像

  protected:
    void initializeGL() Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;
    void resizeGL(int width, int height) Q_DECL_OVERRIDE;

  private:
    QOpenGLBuffer vbo;
    GLuint texture_uniform_y, texture_uniform_u, texture_uniform_v;
    std::unique_ptr<QOpenGLTexture> texture_y, texture_u, texture_v;
    GLuint idy, idu, idv;
    std::shared_ptr<char[]> data;
    int video_width, video_height;

    std::unique_ptr<QOpenGLShader> vs, ps;
    std::unique_ptr<QOpenGLShaderProgram> program;
};
