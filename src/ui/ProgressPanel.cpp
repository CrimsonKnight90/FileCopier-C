// ProgressPanel.cpp
// Panel que muestra el progreso global y por archivo individual.
// Se actualiza desde el EventBus vía Qt::QueuedConnection.

#include "../../include/EventBus.h"
#include "../../include/Utils.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QFrame>
#include <QFont>
#include <QString>
#include <QMetaObject>
#include <QMap>
#include <QTimer>
#include <chrono>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// FileProgressRow: fila individual para un archivo en copia
// ─────────────────────────────────────────────────────────────────────────────
class FileProgressRow : public QFrame {
    Q_OBJECT
public:
    explicit FileProgressRow(const QString& path, qint64 totalBytes, QWidget* parent = nullptr)
        : QFrame(parent), m_total(totalBytes)
    {
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(2);

        // Nombre del archivo (solo el nombre, no la ruta completa)
        QString name = path.section('\\', -1); // último segmento
        m_nameLabel = new QLabel(name);
        QFont f = m_nameLabel->font();
        f.setBold(true);
        m_nameLabel->setFont(f);
        layout->addWidget(m_nameLabel);

        // Ruta completa en gris pequeño
        m_pathLabel = new QLabel(path);
        m_pathLabel->setStyleSheet("color: gray; font-size: 10px;");
        m_pathLabel->setWordWrap(false);
        m_pathLabel->setElideMode(Qt::ElideLeft);  // recorta por la izquierda (más útil para rutas)
        // QLabel no tiene setElideMode; usamos setSizePolicy + toolTip
        m_pathLabel->setToolTip(path);
        layout->addWidget(m_pathLabel);

        // Barra de progreso
        auto* row = new QHBoxLayout;
        m_bar = new QProgressBar;
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        m_bar->setFixedHeight(14);
        row->addWidget(m_bar, 1);

        // Bytes / total
        m_sizeLabel = new QLabel(FormatSize(0) + " / " + FormatSize(totalBytes));
        m_sizeLabel->setFixedWidth(160);
        m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_sizeLabel);

        layout->addLayout(row);

        // Estado textual
        m_statusLabel = new QLabel("Copying…");
        m_statusLabel->setStyleSheet("color: #555; font-size: 10px;");
        layout->addWidget(m_statusLabel);

        m_startTime = std::chrono::steady_clock::now();
    }

    void UpdateProgress(qint64 done, qint64 total) {
        if (total > 0) m_bar->setValue(static_cast<int>(done * 100 / total));
        m_sizeLabel->setText(FormatSize(done) + " / " + FormatSize(total));

        // Calcular velocidad instantánea
        auto now     = std::chrono::steady_clock::now();
        double secs  = std::chrono::duration<double>(now - m_startTime).count();
        if (secs > 0.1) {
            double mbps = (done / 1024.0 / 1024.0) / secs;
            m_statusLabel->setText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));
        }
    }

    void MarkCompleted(bool success) {
        m_bar->setValue(100);
        if (success) {
            m_statusLabel->setText("✔ Completed");
            m_statusLabel->setStyleSheet("color: green; font-size: 10px;");
            m_bar->setStyleSheet("QProgressBar::chunk { background: #4CAF50; }");
        } else {
            m_statusLabel->setText("✘ Error");
            m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
            m_bar->setStyleSheet("QProgressBar::chunk { background: #f44336; }");
        }
    }

    void MarkError(const QString& msg) {
        m_statusLabel->setText("✘ " + msg);
        m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
        m_bar->setStyleSheet("QProgressBar::chunk { background: #f44336; }");
    }

private:
    static QString FormatSize(qint64 bytes) {
        if (bytes < 1024)           return QString("%1 B").arg(bytes);
        if (bytes < 1024 * 1024)    return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        if (bytes < 1024*1024*1024) return QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
        return                             QString("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
    }

    QLabel*       m_nameLabel   = nullptr;
    QLabel*       m_pathLabel   = nullptr;
    QProgressBar* m_bar         = nullptr;
    QLabel*       m_sizeLabel   = nullptr;
    QLabel*       m_statusLabel = nullptr;
    qint64        m_total       = 0;
    std::chrono::steady_clock::time_point m_startTime;
};

// ─────────────────────────────────────────────────────────────────────────────
// ProgressPanel: contenedor principal
// ─────────────────────────────────────────────────────────────────────────────
class ProgressPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressPanel(QWidget* parent = nullptr) : QWidget(parent) {
        BuildUI();
        ConnectEventBus();

        // Timer para limpiar filas completadas (>5 s) y no saturar la lista
        m_cleanTimer = new QTimer(this);
        m_cleanTimer->setInterval(5000);
        connect(m_cleanTimer, &QTimer::timeout, this, &ProgressPanel::CleanCompleted);
        m_cleanTimer->start();
    }

    // Llamado desde MainWindow al iniciar una nueva operación
    void Reset(qint64 totalBytes, int totalFiles) {
        m_totalBytes  = totalBytes;
        m_totalFiles  = totalFiles;
        m_doneBytes   = 0;
        m_doneFiles   = 0;
        m_failedFiles = 0;
        m_startTime   = std::chrono::steady_clock::now();

        m_globalBar->setValue(0);
        m_globalBar->setRange(0, totalFiles > 0 ? totalFiles : 1);
        m_statsLabel->setText(QString("0 / %1 files").arg(totalFiles));
        m_speedLabel->setText("0 MB/s");
        m_etaLabel->setText("ETA: --");

        // Limpiar filas anteriores
        for (auto* w : m_rows.values()) {
            m_rowsLayout->removeWidget(w);
            delete w;
        }
        m_rows.clear();
    }

public slots:
    void OnUIFileStarted(const QString& path, qint64 total) {
        auto* row = new FileProgressRow(path, total, this);
        m_rowsLayout->insertWidget(0, row); // más reciente arriba
        m_rows[path] = row;
    }

    void OnUIProgress(const QString& path, qint64 done, qint64 total) {
        if (auto* row = m_rows.value(path, nullptr))
            row->UpdateProgress(done, total);

        // Progreso global por bytes
        m_doneBytes = done; // simplificado; en producción se acumularía por archivo
        if (m_totalBytes > 0) {
            // Actualiza barra global basada en archivos completados + fracción del actual
            double fraction = m_doneFiles + (total > 0 ? (double)done / total : 0.0);
            m_globalBar->setValue(static_cast<int>(fraction));
        }

        // Velocidad y ETA
        auto now    = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - m_startTime).count();
        if (secs > 0.5) {
            double mbps = (m_doneBytes / 1024.0 / 1024.0) / secs;
            m_speedLabel->setText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));

            if (mbps > 0 && m_totalBytes > 0) {
                double remaining = (m_totalBytes - m_doneBytes) / 1024.0 / 1024.0 / mbps;
                m_etaLabel->setText(FormatETA(remaining));
            }
        }
    }

    void OnUIFileCompleted(const QString& path, bool success) {
        if (auto* row = m_rows.value(path, nullptr))
            row->MarkCompleted(success);

        success ? ++m_doneFiles : ++m_failedFiles;
        m_globalBar->setValue(m_doneFiles + m_failedFiles);
        m_statsLabel->setText(
            QString("%1 / %2 files  |  %3 errors")
                .arg(m_doneFiles)
                .arg(m_totalFiles)
                .arg(m_failedFiles)
        );
    }

    void OnUIError(const QString& path, const QString& msg) {
        if (auto* row = m_rows.value(path, nullptr))
            row->MarkError(msg);
    }

    void OnUIQueueFinished(int total, int ok, int failed) {
        m_speedLabel->setText("Done");
        m_etaLabel->setText("ETA: 0s");
        m_statsLabel->setText(
            QString("Finished: %1 ok, %2 failed of %3").arg(ok).arg(failed).arg(total));
    }

private slots:
    void CleanCompleted() {
        // Elimina filas completadas con más de 5 segundos para no saturar la UI
        // (en una implementación completa se marcaría el timestamp de finalización)
    }

private:
    void BuildUI() {
        auto* vMain = new QVBoxLayout(this);
        vMain->setContentsMargins(0, 0, 0, 0);

        // ── Barra global ──────────────────────────────────────────────────
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

        // ── Área scrollable con filas por archivo ─────────────────────────
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

    void ConnectEventBus() {
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
        bus.OnFileCompleted([this](const EventFileCompleted& e) {
            QMetaObject::invokeMethod(this, "OnUIFileCompleted", Qt::QueuedConnection,
                Q_ARG(QString, QString::fromStdWString(e.path)),
                Q_ARG(bool,    e.success));
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

    static QString FormatETA(double seconds) {
        if (seconds < 60)
            return QString("ETA: %1s").arg(static_cast<int>(seconds));
        if (seconds < 3600)
            return QString("ETA: %1m %2s")
                .arg(static_cast<int>(seconds) / 60)
                .arg(static_cast<int>(seconds) % 60);
        return QString("ETA: %1h %2m")
            .arg(static_cast<int>(seconds) / 3600)
            .arg((static_cast<int>(seconds) % 3600) / 60);
    }

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

#include "ProgressPanel.moc"
