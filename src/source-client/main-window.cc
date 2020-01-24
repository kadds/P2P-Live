#include "main-window.hpp"
#include "ui_main-window.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  this->setWindowTitle("P2P Live");
}

MainWindow::~MainWindow() { delete ui; }
