#include "../../include/ui/ProgressPanel.h"
#include "../../include/EventBus.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QString>
#include <QMetaObject>
#include <chrono>

using namespace FileCopier;

// ── FileProgressRow ───────────────────────────────────────────────────────────
class FileProgressRow : public QFrame {
    Q_OBJECT
public:
    explicit FileProgressRow(const QString& path, qint64 totalBytes, QWidget* parent = nullptr)
        : QFrame(parent), m_total(totalBytes)
    {
        setFrameShape(QFrame::StyledPanel);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(2);

        QString name = path.section('\\', -1);
        m_nameLabel = new QLabel(name);
        QFont f = m_nameLabel->font(); f.setBold(true);
        m_nameLabel->setFont(f);
        layout->addWidget(m_nameLabel);

        m_pathLabel = new QLabel(path);
        m_pathLabel->setStyleSheet("color: gray; font-size: 10px;");
        m_pathLabel->setToolTip(path);
        layout->addWidget(m_pathLabel);

        auto* row = new QHBoxLayout;
        m_bar = new QProgressBar;
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        m_bar->setFixedHeight(14);
        row->addWidget(m_bar, 1);

        m_sizeLabel = new QLabel(FormatSize(0) + " / " + FormatSize(totalBytes));
        m_sizeLabel->setFixedWidth(160);
        m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_sizeLabel);
        layout->addLayout(row);

        m_statusLabel = new QLabel("Copying...");
        m_statusLabel->setStyleSheet("color: #555; font-size: 10px;");
        layout->addWidget(m_statusLabel);

        m_startTime = std::chrono::steady_clock::now();
    }

    void UpdateProgress(qint64 done, qint64 total) {
        if (total > 0) m_bar->setValue(static_cast<int>(done * 100 / total));
        m_sizeLabel->setText(FormatSize(done) + " / " + FormatSize(total));
        auto now    = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - m_startTime).count();
        if (secs > 0.1) {
            double mbps = (done / 1024.0 / 1024.0) / secs;
            m_statusLabel->setText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));
        }
    }

    void MarkCompleted(bool success) {
        m_bar->setValue(100);
        if (success) {
            m_statusLabel->setText("Completed");
            m_statusLabel->setStyleSheet("color: green; font-size: 10px;");
            m_bar->setStyleSheet("QProgressBar::chunk { background: #4CAF50; }");
        } else {
            m_statusLabel->setText("Error");
            m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
            m_bar->setStyleSheet("QProgressBar::chunk { background: #f44336; }");
        }
    }

    void MarkError(const QString& msg) {
        m_statusLabel->setText("Error: " + msg);
        m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
        m_bar->setStyleSheet("QProgressBar::chunk { background: #f44336; }");
    }

private:
    static QString FormatSize(qint64 bytes) {
        if (bytes < 1024)            return QString("%1 B").arg(bytes);
        if (bytes < 1024*1024)       return QString("%1 KB").arg(bytes/1024.0, 0,'f',1);
        if (bytes < 1024*1024*1024)  return QString("%1 MB").arg(bytes/1024.0/1024.0, 0,'f',2);
        return                              QString("%1 GB").arg(bytes/1024.0/1024.0/1024.0, 0,'f',2);
    }

    QLabel*       m_nameLabel   = nullptr;
    QLabel*       m_pathLabel   = nullptr;
    QProgressBar* m_bar         = nullptr;
    QLabel*       m_sizeLabel   = nullptr;
    QLabel*       m_statusLabel = nullptr;
    qint64        m_total       = 0;
    std::chrono::steady_clock::time_point m_startTime;
};

#include "ProgressPanel.moc"

// ── ProgressPanel ─────────────────────────────────────────────────────────────
ProgressPanel::ProgressPanel(QWidget* parent) : QWidget(parent) {
    BuildUI();
    ConnectEventBus();
    m_cleanTimer = new QTimer(this);
    m_cleanTimer->setInterval(5000);
    connect(m_cleanTimer, &QTimer::timeout, this, &ProgressPanel::CleanCompleted);
    m_cleanTimer->start();
}

void ProgressPanel::Reset(qint64 totalBytes, int totalFiles) {
    m_totalBytes = totalBytes; m_totalFiles = totalFiles;
    m_doneBytes = 0; m_doneFiles = 0; m_failedFiles = 0;
    m_startTime = std::chrono::steady_clock::now();
    m_globalBar->setValue(0);
    m_globalBar->setRange(0, totalFiles > 0 ? totalFiles : 1);
    m_statsLabel->setText(QString("0 / %1 files").arg(totalFiles));
    m_speedLabel->setText("0 MB/s");
    m_etaLabel->setText("ETA: --");
    for (auto* w : m_rows.values()) { m_rowsLayout->removeWidget(w); delete w; }
    m_rows.clear();
}

void ProgressPanel::OnUIFileStarted(const QString& path, qint64 total) {
    auto* row = new FileProgressRow(path, total, this);
    m_rowsLayout->insertWidget(0, row);
    m_rows[path] = row;
}

void ProgressPanel::OnUIProgress(const QString& path, qint64 done, qint64 total) {
    if (auto* row = m_rows.value(path, nullptr)) row->UpdateProgress(done, total);
    m_doneBytes = done;
    auto now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now - m_startTime).count();
    if (secs > 0.5) {
        double mbps = (m_doneBytes / 1024.0 / 1024.0) / secs;
        m_speedLabel->setText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));
        if (mbps > 0 && m_totalBytes > 0) {
            double rem = (m_totalBytes - m_doneBytes) / 1024.0 / 1024.0 / mbps;
            m_etaLabel->setText(FormatETA(rem));
        }
    }
}

void ProgressPanel::OnUIFileCompleted(const QString& path, bool success) {
    if (auto* row = m_rows.value(path, nullptr)) row->MarkCompleted(success);
    success ? ++m_doneFiles : ++m_failedFiles;
    m_globalBar->setValue(m_doneFiles + m_failedFiles);
    m_statsLabel->setText(QString("%1 / %2 files  |  %3 errors")
        .arg(m_doneFiles).arg(m_totalFiles).arg(m_failedFiles));
}

void ProgressPanel::OnUIError(const QString& path, const QString& msg) {
    if (auto* row = m_rows.value(path, nullptr)) row->MarkError(msg);
}

void ProgressPanel::OnUIQueueFinished(int total, int ok, int failed) {
    m_speedLabel->setText("Done");
    m_etaLabel->setText("ETA: 0s");
    m_statsLabel->setText(QString("Finished: %1 ok, %2 failed of %3").arg(ok).arg(failed).arg(total));
}

void ProgressPanel::CleanCompleted() {}

void ProgressPanel::BuildUI() {
    auto* vMain = new QVBoxLayout(this);
    vMain->setContentsMargins(0, 0, 0, 0);

    auto* globalSection = new QFrame;
    globalSection->setFrameShape(QFrame::StyledPanel);
    auto* gLayout = new QVBoxLayout(globalSection);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel("<b>Overall progress</b>"));
    topRow->addStretch();
    m_speedLabel = new QLabel("0 MB/s");
    topRow->addWidget(m_speedLabel);
    m_etaLabel = new QLabel("ETA: --");
    topRow->addWidget(m_etaLabel);
    gLayout->addLayout(topRow);

    m_globalBar = new QProgressBar;
    m_globalBar->setRange(0, 100);
    m_globalBar->setFixedHeight(18);
    gLayout->addWidget(m_globalBar);

    m_statsLabel = new QLabel("0 / 0 files");
    gLayout->addWidget(m_statsLabel);
    vMain->addWidget(globalSection);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* rowsContainer = new QWidget;
    m_rowsLayout = new QVBoxLayout(rowsContainer);
    m_rowsLayout->setAlignment(Qt::AlignTop);
    m_rowsLayout->setSpacing(4);
    scroll->setWidget(rowsContainer);
    vMain->addWidget(scroll, 1);
}

void ProgressPanel::ConnectEventBus() {
    auto& bus = EventBus::Instance();
    bus.OnFileStarted([this](const EventFileStarted& e) {
        QMetaObject::invokeMethod(this, "OnUIFileStarted", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)), Q_ARG(qint64, e.totalBytes));
    });
    bus.OnFileProgress([this](const EventFileProgress& e) {
        QMetaObject::invokeMethod(this, "OnUIProgress", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(qint64, e.bytesCopied), Q_ARG(qint64, e.totalBytes));
    });
    bus.OnFileCompleted([this](const EventFileCompleted& e) {
        QMetaObject::invokeMethod(this, "OnUIFileCompleted", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)), Q_ARG(bool, e.success));
    });
    bus.OnError([this](const EventError& e) {
        QMetaObject::invokeMethod(this, "OnUIError", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(QString, QString::fromStdWString(e.message)));
    });
    bus.OnQueueFinished([this](const EventQueueFinished& e) {
        QMetaObject::invokeMethod(this, "OnUIQueueFinished", Qt::QueuedConnection,
            Q_ARG(int, static_cast<int>(e.total)),
            Q_ARG(int, static_cast<int>(e.succeeded)),
            Q_ARG(int, static_cast<int>(e.failed)));
    });
}

QString ProgressPanel::FormatETA(double seconds) {
    if (seconds < 60)   return QString("ETA: %1s").arg(static_cast<int>(seconds));
    if (seconds < 3600) return QString("ETA: %1m %2s").arg(static_cast<int>(seconds)/60).arg(static_cast<int>(seconds)%60);
    return QString("ETA: %1h %2m").arg(static_cast<int>(seconds)/3600).arg((static_cast<int>(seconds)%3600)/60);
}
