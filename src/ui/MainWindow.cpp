#include "../../include/ui/MainWindow.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include "../../include/Jobs.h"
#include "../../include/FileCopyEngine.h"
#include "../../include/DirectoryScanner.h"
#include "../../include/ThreadPool.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>
#include <QMetaObject>
#include <QString>
#include <atomic>

using namespace FileCopier;

// ── CopyWorker ────────────────────────────────────────────────────────────────
class CopyWorker : public QObject {
    Q_OBJECT
public:
    CopyWorker(const QString& src, const QString& dst,
               const AppConfig& cfg, QObject* parent = nullptr)
        : QObject(parent), m_src(src), m_dst(dst), m_cfg(cfg) {}

signals:
    void finished(int total, int ok, int failed);

public slots:
    void run() {
        DirectoryScanner scanner;
        std::vector<FileEntry> entries;
        scanner.Scan(m_src.toStdWString(), m_dst.toStdWString(), entries);

        ThreadPool pool(m_cfg.threadCount);
        std::atomic<int> totalOk{0}, totalFail{0};

        CopyOptions opts;
        opts.bufferSize      = m_cfg.bufferSizeKB * 1024;
        opts.maxRetries      = m_cfg.maxRetries;
        opts.retryDelayMs    = m_cfg.retryDelayMs;
        opts.useNoBuffering  = m_cfg.useNoBuffering;
        opts.useOverlappedIO = m_cfg.useOverlapped;
        opts.verifyAfterCopy = m_cfg.verifyAfterCopy;
        opts.skipOnError     = m_cfg.skipOnError;

        for (auto& entry : entries) {
            if (entry.isDirectory) {
                ::CreateDirectoryW((L"\\\\?\\" + entry.dstPath).c_str(), nullptr);
                continue;
            }
            pool.Enqueue([entry, opts, &totalOk, &totalFail]() {
                FileCopyEngine engine(opts);
                bool ok = engine.CopyFile(
                    entry.srcPath, entry.dstPath,
                    [&](LONGLONG done, LONGLONG total) {
                        EventBus::Instance().Emit(
                            EventFileProgress{ entry.srcPath, done, total, 0.0 });
                    },
                    [&](const std::wstring& msg, DWORD code) {
                        EventBus::Instance().Emit(
                            EventError{ entry.srcPath, msg, code });
                    }
                );
                ok ? ++totalOk : ++totalFail;
            });
        }

        pool.WaitAll();
        EventBus::Instance().Emit(EventQueueFinished{
            entries.size(),
            static_cast<size_t>(totalOk.load()),
            static_cast<size_t>(totalFail.load())
        });
        emit finished(static_cast<int>(entries.size()), totalOk.load(), totalFail.load());
    }

private:
    QString   m_src, m_dst;
    AppConfig m_cfg;
};

#include "MainWindow.moc"

// ── MainWindow ────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("FileCopier");
    BuildUI();
    ConnectEventBus();
}

void MainWindow::OnBrowseSrc() {
    auto dir = QFileDialog::getExistingDirectory(this, "Source folder");
    if (!dir.isEmpty()) m_srcEdit->setText(dir);
}

void MainWindow::OnBrowseDst() {
    auto dir = QFileDialog::getExistingDirectory(this, "Destination folder");
    if (!dir.isEmpty()) m_dstEdit->setText(dir);
}

void MainWindow::OnStart() {
    if (m_srcEdit->text().isEmpty() || m_dstEdit->text().isEmpty()) {
        QMessageBox::warning(this, "FileCopier", "Select source and destination folders.");
        return;
    }
    m_logList->clear();
    m_globalBar->setValue(0);

    auto& cfg = ConfigManager::Instance().Config();
    auto* worker = new CopyWorker(m_srcEdit->text(), m_dstEdit->text(), cfg);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &CopyWorker::run);
    connect(worker, &CopyWorker::finished, this,
        [this, worker, thread](int total, int ok, int failed) {
            statusBar()->showMessage(
                QString("Done: %1 ok, %2 failed of %3").arg(ok).arg(failed).arg(total));
            m_globalBar->setValue(100);
            thread->quit();
            worker->deleteLater();
            thread->deleteLater();
        });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
    statusBar()->showMessage("Copying...");
}

void MainWindow::OnPause()  {}
void MainWindow::OnCancel() {}

void MainWindow::OnUIFileStarted(const QString& path, qint64 total) {
    m_logList->addItem(QString("[START] %1 (%2 MB)")
        .arg(path).arg(total / 1024.0 / 1024.0, 0, 'f', 2));
    m_logList->scrollToBottom();
}

void MainWindow::OnUIProgress(const QString& /*path*/, qint64 done, qint64 total) {
    if (total > 0) m_globalBar->setValue(static_cast<int>(done * 100 / total));
}

void MainWindow::OnUIError(const QString& path, const QString& msg) {
    m_logList->addItem(QString("[ERR] %1 - %2").arg(path).arg(msg));
    m_logList->scrollToBottom();
}

void MainWindow::OnUISpeed(double mbps, qint64 /*done*/, qint64 /*total*/) {
    m_speedLabel->setText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));
}

void MainWindow::BuildUI() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* vMain = new QVBoxLayout(central);

    auto addPathRow = [&](const QString& label, QLineEdit*& edit, auto slot) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        edit = new QLineEdit;
        row->addWidget(edit);
        auto* btn = new QPushButton("...");
        btn->setFixedWidth(30);
        connect(btn, &QPushButton::clicked, this, slot);
        row->addWidget(btn);
        vMain->addLayout(row);
    };

    addPathRow("Source:", m_srcEdit, &MainWindow::OnBrowseSrc);
    addPathRow("Dest:  ", m_dstEdit, &MainWindow::OnBrowseDst);

    m_globalBar = new QProgressBar;
    m_globalBar->setRange(0, 100);
    vMain->addWidget(m_globalBar);

    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel("Speed:"));
    m_speedLabel = new QLabel("0 MB/s");
    speedRow->addWidget(m_speedLabel);
    speedRow->addStretch();
    vMain->addLayout(speedRow);

    m_logList = new QListWidget;
    vMain->addWidget(m_logList, 1);

    auto* btnRow = new QHBoxLayout;
    auto addBtn = [&](const QString& txt, auto slot) {
        auto* b = new QPushButton(txt);
        connect(b, &QPushButton::clicked, this, slot);
        btnRow->addWidget(b);
    };
    addBtn("Start",  &MainWindow::OnStart);
    addBtn("Pause",  &MainWindow::OnPause);
    addBtn("Cancel", &MainWindow::OnCancel);
    vMain->addLayout(btnRow);

    statusBar()->showMessage("Ready");
}

void MainWindow::ConnectEventBus() {
    auto& bus = EventBus::Instance();

    bus.OnFileStarted([this](const EventFileStarted& e) {
        QMetaObject::invokeMethod(this, "OnUIFileStarted", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(qint64,  e.totalBytes));
    });
    bus.OnFileProgress([this](const EventFileProgress& e) {
        QMetaObject::invokeMethod(this, "OnUIProgress", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(qint64,  e.bytesCopied),
            Q_ARG(qint64,  e.totalBytes));
    });
    bus.OnError([this](const EventError& e) {
        QMetaObject::invokeMethod(this, "OnUIError", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(QString, QString::fromStdWString(e.message)));
    });
    bus.OnSpeedUpdate([this](const EventSpeedUpdate& e) {
        QMetaObject::invokeMethod(this, "OnUISpeed", Qt::QueuedConnection,
            Q_ARG(double,  e.speedMBps),
            Q_ARG(qint64,  e.bytesDone),
            Q_ARG(qint64,  e.bytesTotal));
    });
}
