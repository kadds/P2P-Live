#include "yuvwidget.hpp"

#define VERTEXIN 0
#define TEXTUREIN 1

yuvWidget::yuvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

yuvWidget::~yuvWidget()
{
    makeCurrent();
    vbo.destroy();
    texture_y->destroy();
    texture_u->destroy();
    texture_v->destroy();
    program->release();

    doneCurrent();
}

void yuvWidget::update_frame(std::shared_ptr<char[]> data, int width, int height)
{
    this->data = std::move(data);
    video_width = width;
    video_height = height;
    update();
}

void yuvWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);

    static const GLfloat vertices[]{
        -1.0f, -1.0f, -1.0f, +1.0f, +1.0f, +1.0f, +1.0f, -1.0f,

        0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
    };

    vbo.create();
    vbo.bind();
    vbo.allocate(vertices, sizeof(vertices));

    vs = std::make_unique<QOpenGLShader>(QOpenGLShader::Vertex, this);
    const char *vsrc = "attribute vec4 vertexIn; \
    attribute vec2 textureIn; \
    varying vec2 textureOut;  \
    void main(void)           \
    {                         \
        gl_Position = vertexIn; \
        textureOut = textureIn; \
    }";
    vs->compileSourceCode(vsrc);

    ps = std::make_unique<QOpenGLShader>(QOpenGLShader::Fragment, this);
    const char *fsrc = "varying vec2 textureOut; \
    uniform sampler2D tex_y; \
    uniform sampler2D tex_u; \
    uniform sampler2D tex_v; \
    void main(void) \
    { \
        vec3 yuv; \
        vec3 rgb; \
        yuv.x = texture2D(tex_y, textureOut).r; \
        yuv.y = texture2D(tex_u, textureOut).r - 0.5; \
        yuv.z = texture2D(tex_v, textureOut).r - 0.5; \
        rgb = mat3( 1,       1,         1, \
                    0,       -0.39465,  2.03211, \
                    1.13983, -0.58060,  0) * yuv; \
        gl_FragColor = vec4(rgb, 1); \
    }";
    ps->compileSourceCode(fsrc);

    program = std::make_unique<QOpenGLShaderProgram>(this);
    program->addShader(vs.get());
    program->addShader(ps.get());
    program->bindAttributeLocation("vertexIn", VERTEXIN);
    program->bindAttributeLocation("textureIn", TEXTUREIN);
    program->link();
    program->bind();
    program->enableAttributeArray(VERTEXIN);
    program->enableAttributeArray(TEXTUREIN);
    program->setAttributeBuffer(VERTEXIN, GL_FLOAT, 0, 2, 2 * sizeof(GLfloat));
    program->setAttributeBuffer(TEXTUREIN, GL_FLOAT, 8 * sizeof(GLfloat), 2, 2 * sizeof(GLfloat));

    texture_uniform_y = program->uniformLocation("tex_y");
    texture_uniform_u = program->uniformLocation("tex_u");
    texture_uniform_v = program->uniformLocation("tex_v");
    texture_y = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture_u = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture_v = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture_y->create();
    texture_u->create();
    texture_v->create();
    idy = texture_y->textureId();
    idu = texture_u->textureId();
    idv = texture_v->textureId();
    glClearColor(0.0, 0.0, 0.0, 0.0);
}

void yuvWidget::paintGL()
{
    auto data = this->data;
    char *ptr = data.get();
    if (ptr == nullptr)
        return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, idy);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, video_width, video_height, 0, GL_RED, GL_UNSIGNED_BYTE, ptr);
    // GL_NEAREST
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE1, idu);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, video_width >> 1, video_height >> 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                 ptr + video_width * video_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, idv);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, video_width >> 1, video_height >> 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                 ptr + video_width * video_height * 5 / 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(texture_uniform_y, 0);
    glUniform1i(texture_uniform_u, 1);
    glUniform1i(texture_uniform_v, 2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void yuvWidget::resizeGL(int width, int height) { glViewport(0, 0, width, height); }
