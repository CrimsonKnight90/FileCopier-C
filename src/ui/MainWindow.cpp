#include "../../include/ui/MainWindow.h"
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout;

    startButton = new QPushButton("Select File & Copy");
    progressBar = new QProgressBar;

    layout->addWidget(startButton);
    layout->addWidget(progressBar);

    widget->setLayout(layout);
    setCentralWidget(widget);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::onStart);

    // Timer para actualizar progreso
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, [this]() {
        auto jobs = controller.GetJobManager().GetJobs();
        if (!jobs.empty()) {
            auto job = jobs.back();
            if (job->totalBytes > 0) {
                int progress = (int)((job->bytesCopied * 100) / job->totalBytes);
                progressBar->setValue(progress);
            }
        }
    });
    timer->start(100);
}

void MainWindow::onStart()
{
    QString src = QFileDialog::getOpenFileName(this, "Select Source File");
    QString dst = QFileDialog::getSaveFileName(this, "Select Destination");

    if (src.isEmpty() || dst.isEmpty())
        return;

    controller.CreateCopyJob(src.toStdWString(), dst.toStdWString());
    controller.Start();
}