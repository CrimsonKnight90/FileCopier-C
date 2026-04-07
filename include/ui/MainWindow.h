#pragma once
#include <QMainWindow>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OnBrowseSrc();
    void OnBrowseDst();
    void OnStart();
    void OnPause();
    void OnCancel();
    void OnUIFileStarted(const QString& path, qint64 total);
    void OnUIProgress(const QString& path, qint64 done, qint64 total);
    void OnUIError(const QString& path, const QString& msg);
    void OnUISpeed(double mbps, qint64 done, qint64 total);

private:
    void BuildUI();
    void ConnectEventBus();

    QLineEdit*    m_srcEdit    = nullptr;
    QLineEdit*    m_dstEdit    = nullptr;
    QProgressBar* m_globalBar  = nullptr;
    QLabel*       m_speedLabel = nullptr;
    QListWidget*  m_logList    = nullptr;
};
