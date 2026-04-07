#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QMap>
#include <QTimer>
#include <chrono>

class FileProgressRow;

class ProgressPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressPanel(QWidget* parent = nullptr);
    void Reset(qint64 totalBytes, int totalFiles);

public slots:
    void OnUIFileStarted(const QString& path, qint64 total);
    void OnUIProgress(const QString& path, qint64 done, qint64 total);
    void OnUIFileCompleted(const QString& path, bool success);
    void OnUIError(const QString& path, const QString& msg);
    void OnUIQueueFinished(int total, int ok, int failed);

private slots:
    void CleanCompleted();

private:
    void BuildUI();
    void ConnectEventBus();
    static QString FormatETA(double seconds);

    QProgressBar* m_globalBar   = nullptr;
    QLabel*       m_speedLabel  = nullptr;
    QLabel*       m_etaLabel    = nullptr;
    QLabel*       m_statsLabel  = nullptr;
    QVBoxLayout*  m_rowsLayout  = nullptr;
    QTimer*       m_cleanTimer  = nullptr;

    QMap<QString, FileProgressRow*> m_rows;

    qint64 m_totalBytes  = 0;
    qint64 m_doneBytes   = 0;
    int    m_totalFiles  = 0;
    int    m_doneFiles   = 0;
    int    m_failedFiles = 0;
    std::chrono::steady_clock::time_point m_startTime;
};
