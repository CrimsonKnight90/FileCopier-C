#pragma once
#include <QMainWindow>
#include "../AppController.h"

class QPushButton;
class QProgressBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void onStart();

private:
    AppController controller;

    QPushButton* startButton;
    QProgressBar* progressBar;
};