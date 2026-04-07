// MainWindow.cpp — UI completa de FileCopier
// Características: panel colapsable, tabs, multi-idioma, opciones, lista de copia

#include "../../include/ui/MainWindow.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include "../../include/Jobs.h"
#include "../../include/FileCopyEngine.h"
#include "../../include/DirectoryScanner.h"
#include "../../include/ThreadPool.h"
#include "../../include/Language.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <atomic>
#include <algorithm>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// CopyController: gestiona el hilo de copia y comunica progreso via EventBus
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow::CopyController : public QObject {
    Q_OBJECT
public:
    explicit CopyController(const QString& src, const QString& dst,
                            const AppConfig& cfg, QObject* parent = nullptr)
        : QObject(parent), m_src(src), m_dst(dst), m_cfg(cfg) {}

    void RequestPause()  { m_pause  = true;  }
    void RequestResume() { m_pause  = false; m_engine ? m_engine->Resume() : void(); }
    void RequestCancel() { m_cancel = true;  m_engine ? m_engine->Cancel() : void(); }

signals:
    void finished(int total, int ok, int failed);

public slots:
    void run() {
        DirectoryScanner scanner;
        std::vector<FileEntry> entries;

        EventBus::Instance().Emit(EventFileStarted{ L"__scanning__", 0 });
        scanner.Scan(m_src.toStdWString(), m_dst.toStdWString(), entries,
            [](size_t n) { /* scan progress */ });

        int totalFiles = 0;
        for (auto& e : entries) if (!e.isDirectory) ++totalFiles;

        ThreadPool pool(m_cfg.threadCount);
        std::atomic<int> ok{0}, failed{0};

        CopyOptions opts;
        opts.bufferSizeBytes = m_cfg.bufferSizeKB * 1024;
        opts.maxRetries      = m_cfg.maxRetries;
        opts.retryDelayMs    = m_cfg.retryDelayMs;
        opts.useNoBuffering  = m_cfg.useNoBuffering;
        opts.useOverlappedIO = m_cfg.useOverlapped;
        opts.verifyAfterCopy = m_cfg.verifyAfterCopy;
        opts.skipOnError     = m_cfg.skipOnError;

        for (auto& entry : entries) {
            if (m_cancel) break;
            if (entry.isDirectory) {
                ::CreateDirectoryW((L"\\\\?\\" + entry.dstPath).c_str(), nullptr);
                continue;
            }
            pool.Enqueue([this, entry, opts, &ok, &failed]() mutable {
                while (m_pause && !m_cancel) ::Sleep(50);
                if (m_cancel) { ++failed; return; }

                FileCopyEngine engine(opts);
                m_engine = &engine;

                auto result = engine.CopyFile(
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
                m_engine = nullptr;
                result.success ? ++ok : ++failed;
            });
        }

        pool.WaitAll();

        EventBus::Instance().Emit(EventQueueFinished{
            static_cast<size_t>(totalFiles),
            static_cast<size_t>(ok.load()),
            static_cast<size_t>(failed.load())
        });
        emit finished(totalFiles, ok.load(), failed.load());
    }

private:
    QString   m_src, m_dst;
    AppConfig m_cfg;
    std::atomic<bool> m_pause {false};
    std::atomic<bool> m_cancel{false};
    FileCopyEngine*   m_engine{nullptr};
};

#include "MainWindow.moc"

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow constructor
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(TR("app.title"));
    setMinimumSize(640, 260);
    BuildUI();
    ConnectEventBus();
    ApplyMinSize();
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_copying) {
        auto btn = QMessageBox::question(this,
            TR("app.title"),
            "¿Cancelar la copia en curso y salir?",
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) { event->ignore(); return; }
        emit requestCancel();
        QThread::msleep(300);
    }
    event->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::BuildUI() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* vRoot = new QVBoxLayout(central);
    vRoot->setSpacing(4);
    vRoot->setContentsMargins(6, 6, 6, 6);

    // ── Origen ────────────────────────────────────────────────────────────
    auto* rowSrc = new QHBoxLayout;
    rowSrc->addWidget(new QLabel(TR("lbl.source")));
    m_srcEdit = new QLineEdit;
    rowSrc->addWidget(m_srcEdit, 1);
    auto* btnSrc = new QPushButton(TR("btn.browse"));
    btnSrc->setFixedWidth(30);
    connect(btnSrc, &QPushButton::clicked, this, &MainWindow::OnBrowseSrc);
    rowSrc->addWidget(btnSrc);
    vRoot->addLayout(rowSrc);

    // ── Destino ───────────────────────────────────────────────────────────
    auto* rowDst = new QHBoxLayout;
    rowDst->addWidget(new QLabel(TR("lbl.dest")));
    m_dstEdit = new QLineEdit;
    rowDst->addWidget(m_dstEdit, 1);
    auto* btnDst = new QPushButton(TR("btn.browse"));
    btnDst->setFixedWidth(30);
    connect(btnDst, &QPushButton::clicked, this, &MainWindow::OnBrowseDst);
    rowDst->addWidget(btnDst);
    vRoot->addLayout(rowDst);

    // ── Barra de progreso global ───────────────────────────────────────────
    m_globalBar = new QProgressBar;
    m_globalBar->setRange(0, 1000);
    m_globalBar->setValue(0);
    m_globalBar->setFixedHeight(18);
    m_globalBar->setTextVisible(true);
    vRoot->addWidget(m_globalBar);

    // ── Fila stats: velocidad + ETA + toggle + botones ────────────────────
    auto* rowStats = new QHBoxLayout;

    m_speedLabel = new QLabel("0 MB/s");
    m_speedLabel->setMinimumWidth(80);
    rowStats->addWidget(new QLabel(TR("lbl.speed")));
    rowStats->addWidget(m_speedLabel);

    m_etaLabel = new QLabel("--");
    rowStats->addWidget(new QLabel("  ETA:"));
    rowStats->addWidget(m_etaLabel);

    m_statsLabel = new QLabel("");
    rowStats->addWidget(m_statsLabel);
    rowStats->addStretch();

    // Botón toggle panel
    m_toggleBtn = new QPushButton(TR("btn.toggle.show"));
    m_toggleBtn->setFixedSize(24, 24);
    m_toggleBtn->setToolTip("Mostrar / ocultar panel");
    connect(m_toggleBtn, &QPushButton::clicked, this, &MainWindow::OnTogglePanel);
    rowStats->addWidget(m_toggleBtn);

    rowStats->addSpacing(8);
    m_btnStart  = new QPushButton(TR("btn.start"));
    m_btnPause  = new QPushButton(TR("btn.pause"));
    m_btnCancel = new QPushButton(TR("btn.cancel"));
    m_btnPause->setEnabled(false);
    m_btnCancel->setEnabled(false);
    connect(m_btnStart,  &QPushButton::clicked, this, &MainWindow::OnStart);
    connect(m_btnPause,  &QPushButton::clicked, this, &MainWindow::OnPause);
    connect(m_btnCancel, &QPushButton::clicked, this, &MainWindow::OnCancel);
    rowStats->addWidget(m_btnStart);
    rowStats->addWidget(m_btnPause);
    rowStats->addWidget(m_btnCancel);
    vRoot->addLayout(rowStats);

    // ── Panel colapsable (tabs) ────────────────────────────────────────────
    m_panelWidget = new QWidget;
    m_panelWidget->setVisible(false);  // oculto por defecto
    auto* panelLayout = new QVBoxLayout(m_panelWidget);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    panelLayout->addWidget(m_tabs);

    // Contenido de cada pestaña
    auto* tabCopyList = new QWidget;
    BuildCopyListTab(tabCopyList);
    m_tabs->addTab(tabCopyList, TR("tab.copylist"));

    auto* tabErrors = new QWidget;
    BuildErrorTab(tabErrors);
    m_tabs->addTab(tabErrors, TR("tab.errors"));

    auto* tabOptions = new QWidget;
    BuildOptionsTab(tabOptions);
    m_tabs->addTab(tabOptions, TR("tab.options"));

    vRoot->addWidget(m_panelWidget, 1);

    statusBar()->showMessage(TR("status.ready"));
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::BuildCopyListTab(QWidget* parent) {
    auto* h = new QHBoxLayout(parent);
    h->setContentsMargins(4, 4, 4, 4);

    // Lista
    m_copyList = new QTreeWidget;
    m_copyList->setColumnCount(4);
    m_copyList->setHeaderLabels({
        TR("col.source"), TR("col.size"),
        TR("col.dest"),   TR("col.status")
    });
    m_copyList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_copyList->setAlternatingRowColors(true);
    m_copyList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_copyList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    h->addWidget(m_copyList, 1);

    // Botones laterales
    auto* vBtns = new QVBoxLayout;
    auto mkBtn = [&](QPushButton*& ptr, const char* key) {
        ptr = new QPushButton(TR(key));
        ptr->setMaximumWidth(130);
        vBtns->addWidget(ptr);
    };
    mkBtn(m_btnToTop,    "btn.toTop");
    mkBtn(m_btnUp,       "btn.up");
    mkBtn(m_btnDown,     "btn.down");
    mkBtn(m_btnToBottom, "btn.toBottom");
    vBtns->addSpacing(8);
    mkBtn(m_btnAddFiles, "btn.addFiles");
    mkBtn(m_btnRemove,   "btn.remove");
    vBtns->addSpacing(8);
    mkBtn(m_btnSaveList, "btn.save");
    mkBtn(m_btnLoadList, "btn.load");
    vBtns->addStretch();
    h->addLayout(vBtns);

    connect(m_btnToTop,    &QPushButton::clicked, this, &MainWindow::OnMoveToTop);
    connect(m_btnUp,       &QPushButton::clicked, this, &MainWindow::OnMoveUp);
    connect(m_btnDown,     &QPushButton::clicked, this, &MainWindow::OnMoveDown);
    connect(m_btnToBottom, &QPushButton::clicked, this, &MainWindow::OnMoveToBottom);
    connect(m_btnAddFiles, &QPushButton::clicked, this, &MainWindow::OnAddFiles);
    connect(m_btnRemove,   &QPushButton::clicked, this, &MainWindow::OnRemoveSelected);
    connect(m_btnSaveList, &QPushButton::clicked, this, &MainWindow::OnSaveList);
    connect(m_btnLoadList, &QPushButton::clicked, this, &MainWindow::OnLoadList);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::BuildErrorTab(QWidget* parent) {
    auto* h = new QHBoxLayout(parent);
    h->setContentsMargins(4, 4, 4, 4);

    m_errorList = new QTreeWidget;
    m_errorList->setColumnCount(4);
    m_errorList->setHeaderLabels({
        TR("col.time"), TR("col.action"),
        TR("col.target"), TR("col.errtext")
    });
    m_errorList->setAlternatingRowColors(true);
    m_errorList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_errorList->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    h->addWidget(m_errorList, 1);

    auto* vBtns = new QVBoxLayout;
    m_btnClearErr = new QPushButton(TR("btn.clearErrors"));
    m_btnSaveErr  = new QPushButton(TR("btn.saveErrors"));
    m_btnClearErr->setMaximumWidth(160);
    m_btnSaveErr->setMaximumWidth(160);
    vBtns->addWidget(m_btnClearErr);
    vBtns->addWidget(m_btnSaveErr);
    vBtns->addStretch();
    h->addLayout(vBtns);

    connect(m_btnClearErr, &QPushButton::clicked, this, &MainWindow::OnClearErrors);
    connect(m_btnSaveErr,  &QPushButton::clicked, this, &MainWindow::OnSaveErrors);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::BuildOptionsTab(QWidget* parent) {
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    auto* outer = new QVBoxLayout(parent);
    outer->setContentsMargins(0,0,0,0);
    outer->addWidget(scroll);

    auto* container = new QWidget;
    scroll->setWidget(container);
    auto* vMain = new QVBoxLayout(container);
    vMain->setAlignment(Qt::AlignTop);

    // ── Fin de copia ──────────────────────────────────────────────────────
    auto* grpEnd = new QGroupBox(TR("opt.endCopy"));
    auto* vEnd = new QVBoxLayout(grpEnd);
    m_rbNoCloseErr = new QRadioButton(TR("opt.noCloseErr")); m_rbNoCloseErr->setChecked(true);
    m_rbNoClose    = new QRadioButton(TR("opt.noClose"));
    m_rbClose      = new QRadioButton(TR("opt.close"));
    vEnd->addWidget(m_rbNoCloseErr);
    vEnd->addWidget(m_rbNoClose);
    vEnd->addWidget(m_rbClose);
    vMain->addWidget(grpEnd);

    // ── Colisiones ────────────────────────────────────────────────────────
    auto* grpCol = new QGroupBox(TR("opt.collision"));
    auto* hCol = new QHBoxLayout(grpCol);
    hCol->addWidget(new QLabel(TR("opt.collision") + QString(":")));
    m_cmbCollision = new QComboBox;
    m_cmbCollision->addItems({
        TR("opt.col.ask"), TR("opt.col.cancel"), TR("opt.col.skip"),
        TR("opt.col.overwrite"), TR("opt.col.overwriteD"),
        TR("opt.col.renameNew"), TR("opt.col.renameOld")
    });
    hCol->addWidget(m_cmbCollision);
    hCol->addStretch();
    vMain->addWidget(grpCol);

    // ── Errores de copia ──────────────────────────────────────────────────
    auto* grpErr = new QGroupBox(TR("opt.copyErr"));
    auto* hErr = new QHBoxLayout(grpErr);
    hErr->addWidget(new QLabel(TR("opt.copyErr") + QString(":")));
    m_cmbErrPolicy = new QComboBox;
    m_cmbErrPolicy->addItems({
        TR("opt.err.ask"), TR("opt.err.cancel"), TR("opt.err.skip"),
        TR("opt.err.retryLog"), TR("opt.err.moveEnd")
    });
    m_cmbErrPolicy->setCurrentIndex(3); // RetryOnceThenLog
    hErr->addWidget(m_cmbErrPolicy);
    hErr->addStretch();
    vMain->addWidget(grpErr);

    // ── Rendimiento ───────────────────────────────────────────────────────
    auto* grpPerf = new QGroupBox(TR("opt.perf"));
    auto* vPerf = new QVBoxLayout(grpPerf);

    auto mkSpinRow = [&](QSpinBox*& sp, const char* key, int mn, int mx, int val) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(TR(key)));
        sp = new QSpinBox;
        sp->setRange(mn, mx);
        sp->setValue(val);
        sp->setFixedWidth(80);
        row->addWidget(sp);
        row->addStretch();
        vPerf->addLayout(row);
    };

    auto& cfg = ConfigManager::Instance().Config();
    mkSpinRow(m_spThreads,  "opt.threads",  1, 64, (int)cfg.threadCount);
    mkSpinRow(m_spBufferKB, "opt.bufferKB", 64, 65536, (int)cfg.bufferSizeKB);

    m_chkNoBuffer = new QCheckBox(TR("opt.noBuffering"));
    m_chkOverlap  = new QCheckBox(TR("opt.overlapped"));
    m_chkVerify   = new QCheckBox(TR("opt.verify"));
    m_chkNoBuffer->setChecked(cfg.useNoBuffering);
    m_chkOverlap->setChecked(cfg.useOverlapped);
    m_chkVerify->setChecked(cfg.verifyAfterCopy);
    vPerf->addWidget(m_chkNoBuffer);
    vPerf->addWidget(m_chkOverlap);
    vPerf->addWidget(m_chkVerify);
    vMain->addWidget(grpPerf);

    // ── Idioma ────────────────────────────────────────────────────────────
    auto* grpLang = new QGroupBox(TR("opt.language"));
    auto* hLang = new QHBoxLayout(grpLang);
    m_cmbLanguage = new QComboBox;
    for (auto& [lang, name] : Language::Available())
        m_cmbLanguage->addItem(QString::fromStdWString(name),
                               static_cast<int>(lang));
    m_cmbLanguage->setCurrentIndex(
        static_cast<int>(Language::Instance().Current()));
    hLang->addWidget(m_cmbLanguage);
    hLang->addStretch();
    connect(m_cmbLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnLanguageChanged);
    vMain->addWidget(grpLang);

    // Botón aplicar
    m_btnApplyOpts = new QPushButton("✔ Aplicar y guardar opciones");
    m_btnApplyOpts->setFixedWidth(200);
    connect(m_btnApplyOpts, &QPushButton::clicked, this, &MainWindow::OnApplyOptions);
    vMain->addWidget(m_btnApplyOpts);
    vMain->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::ApplyMinSize() {
    setMinimumSize(680, 260);
}

// ─────────────────────────────────────────────────────────────────────────────
// Toggle panel colapsable
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnTogglePanel() {
    m_panelVisible = !m_panelVisible;
    m_panelWidget->setVisible(m_panelVisible);
    m_toggleBtn->setText(m_panelVisible ? TR("btn.toggle.hide")
                                        : TR("btn.toggle.show"));
    if (m_panelVisible)
        resize(width(), std::max(height(), 500));
    else
        resize(width(), minimumHeight());
}

// ─────────────────────────────────────────────────────────────────────────────
// Browse
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnBrowseSrc() {
    auto d = QFileDialog::getExistingDirectory(this, TR("lbl.source"), m_srcEdit->text());
    if (!d.isEmpty()) m_srcEdit->setText(d);
}
void MainWindow::OnBrowseDst() {
    auto d = QFileDialog::getExistingDirectory(this, TR("lbl.dest"), m_dstEdit->text());
    if (!d.isEmpty()) m_dstEdit->setText(d);
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Pause / Cancel
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnStart() {
    if (m_srcEdit->text().isEmpty() || m_dstEdit->text().isEmpty()) {
        QMessageBox::warning(this, TR("app.title"), TR("msg.selectFolders"));
        return;
    }
    if (m_copying) return;

    // Limpiar estado UI
    m_globalBar->setValue(0);
    m_doneFiles = m_failedFiles = m_totalFiles = 0;
    m_doneBytes = m_totalBytes = 0;
    m_statsLabel->clear();
    if (m_panelVisible) {
        m_copyList->clear();
        m_errorList->clear();
    }

    m_copying = true;
    m_btnStart->setEnabled(false);
    m_btnPause->setEnabled(true);
    m_btnCancel->setEnabled(true);

    auto& cfg = ConfigManager::Instance().Config();
    m_controller = new CopyController(m_srcEdit->text(), m_dstEdit->text(), cfg);
    auto* thread = new QThread(this);
    m_controller->moveToThread(thread);

    connect(thread, &QThread::started, m_controller, &CopyController::run);
    connect(m_controller, &CopyController::finished, this,
        [this, thread](int total, int ok, int failed) {
            m_copying = false;
            m_btnStart->setEnabled(true);
            m_btnPause->setEnabled(false);
            m_btnCancel->setEnabled(false);
            m_btnPause->setText(TR("btn.pause"));

            statusBar()->showMessage(
                QString(TR("msg.done"))
                    .arg(ok).arg(failed).arg(total));
            m_controller->deleteLater();
            m_controller = nullptr;
            thread->quit();
            thread->deleteLater();
        });
    connect(this, &MainWindow::requestCancel,
            m_controller, &CopyController::RequestCancel);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
    statusBar()->showMessage(TR("lbl.copying"));
}

void MainWindow::OnPause() {
    if (!m_controller) return;
    m_paused_ = !m_paused_;
    if (m_paused_) {
        m_controller->RequestPause();
        m_btnPause->setText(TR("btn.resume"));
    } else {
        m_controller->RequestResume();
        m_btnPause->setText(TR("btn.pause"));
    }
}

void MainWindow::OnCancel() {
    if (!m_controller) return;
    m_controller->RequestCancel();
    emit requestCancel();
    statusBar()->showMessage(TR("msg.cancelled"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lista de copia — botones de orden
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnMoveToTop() {
    auto selected = m_copyList->selectedItems();
    if (selected.isEmpty()) return;
    for (auto* item : selected) {
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row <= 0) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(0, item);
        item->setSelected(true);
    }
}

void MainWindow::OnMoveUp() {
    auto selected = m_copyList->selectedItems();
    for (auto* item : selected) {
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row <= 0) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(row - 1, item);
        item->setSelected(true);
    }
}

void MainWindow::OnMoveDown() {
    auto selected = m_copyList->selectedItems();
    int total = m_copyList->topLevelItemCount();
    for (int i = selected.size() - 1; i >= 0; --i) {
        auto* item = selected[i];
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row >= total - 1) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(row + 1, item);
        item->setSelected(true);
    }
}

void MainWindow::OnMoveToBottom() {
    auto selected = m_copyList->selectedItems();
    for (int i = selected.size() - 1; i >= 0; --i) {
        auto* item = selected[i];
        int row = m_copyList->indexOfTopLevelItem(item);
        m_copyList->takeTopLevelItem(row);
        m_copyList->addTopLevelItem(item);
        item->setSelected(true);
    }
}

void MainWindow::OnAddFiles() {
    auto files = QFileDialog::getOpenFileNames(this, TR("btn.addFiles"));
    auto dstBase = m_dstEdit->text();
    for (auto& f : files) {
        auto* item = new QTreeWidgetItem;
        QFileInfo fi(f);
        item->setText(0, f);
        item->setText(1, FormatSize(fi.size()));
        item->setText(2, dstBase + "/" + fi.fileName());
        item->setText(3, "Pendiente");
        m_copyList->addTopLevelItem(item);
    }
}

void MainWindow::OnRemoveSelected() {
    for (auto* item : m_copyList->selectedItems())
        delete item;
}

void MainWindow::OnSaveList() {
    auto path = QFileDialog::getSaveFileName(this,
        TR("btn.save"), "", "FileCopier List (*.fcl);;Text (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (int i = 0; i < m_copyList->topLevelItemCount(); ++i) {
        auto* item = m_copyList->topLevelItem(i);
        ts << item->text(0) << "|" << item->text(2) << "\n";
    }
}

void MainWindow::OnLoadList() {
    auto path = QFileDialog::getOpenFileName(this,
        TR("btn.load"), "", "FileCopier List (*.fcl);;Text (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        auto line = ts.readLine();
        auto parts = line.split("|");
        if (parts.size() < 2) continue;
        QFileInfo fi(parts[0]);
        auto* item = new QTreeWidgetItem;
        item->setText(0, parts[0]);
        item->setText(1, FormatSize(fi.size()));
        item->setText(2, parts[1]);
        item->setText(3, "Pendiente");
        m_copyList->addTopLevelItem(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Errores
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnClearErrors() { m_errorList->clear(); }

void MainWindow::OnSaveErrors() {
    auto path = QFileDialog::getSaveFileName(this,
        TR("btn.saveErrors"), "", "Text (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (int i = 0; i < m_errorList->topLevelItemCount(); ++i) {
        auto* it = m_errorList->topLevelItem(i);
        ts << it->text(0) << "\t" << it->text(1) << "\t"
           << it->text(2) << "\t" << it->text(3) << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Idioma / Opciones
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnLanguageChanged(int index) {
    auto lang = static_cast<Lang>(m_cmbLanguage->itemData(index).toInt());
    Language::Instance().SetLanguage(lang);
    RetranslateUI();
}

void MainWindow::OnApplyOptions() {
    auto& cfg = ConfigManager::Instance().Config();
    cfg.threadCount    = static_cast<size_t>(m_spThreads->value());
    cfg.bufferSizeKB   = static_cast<DWORD>(m_spBufferKB->value());
    cfg.useNoBuffering = m_chkNoBuffer->isChecked();
    cfg.useOverlapped  = m_chkOverlap->isChecked();
    cfg.verifyAfterCopy= m_chkVerify->isChecked();
    ConfigManager::Instance().Save(L"filecopier.ini");
    statusBar()->showMessage("Opciones guardadas.", 3000);
}

void MainWindow::RetranslateUI() {
    setWindowTitle(TR("app.title"));
    m_btnStart->setText(TR("btn.start"));
    m_btnPause->setText(TR("btn.pause"));
    m_btnCancel->setText(TR("btn.cancel"));
    m_tabs->setTabText(0, TR("tab.copylist"));
    m_tabs->setTabText(1, TR("tab.errors"));
    m_tabs->setTabText(2, TR("tab.options"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots de progreso (llamados desde hilo worker via QueuedConnection)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnUIFileStarted(const QString& path, qint64 total) {
    if (!m_panelVisible) return;
    auto* item = new QTreeWidgetItem;
    item->setText(0, path);
    item->setText(1, FormatSize(total));
    item->setText(2, m_dstEdit->text());
    item->setText(3, "Copiando...");
    m_copyList->insertTopLevelItem(0, item);
}

void MainWindow::OnUIProgress(const QString& /*path*/, qint64 done, qint64 total) {
    if (total > 0)
        m_globalBar->setValue(static_cast<int>(done * 1000 / total));
}

void MainWindow::OnUIFileCompleted(const QString& path, bool success) {
    success ? ++m_doneFiles : ++m_failedFiles;
    m_statsLabel->setText(
        QString("%1/%2  |  %3 err")
            .arg(m_doneFiles + m_failedFiles)
            .arg(m_totalFiles)
            .arg(m_failedFiles));

    if (!m_panelVisible) return;
    // Actualizar estado en la lista
    for (int i = 0; i < m_copyList->topLevelItemCount(); ++i) {
        auto* it = m_copyList->topLevelItem(i);
        if (it->text(0) == path) {
            it->setText(3, success ? "✔ OK" : "✘ Error");
            it->setForeground(3, success ? Qt::darkGreen : Qt::red);
            break;
        }
    }
}

void MainWindow::OnUIError(const QString& path, const QString& action,
                           const QString& target, const QString& errText) {
    // Mostrar la pestaña de errores automáticamente
    if (m_panelVisible) m_tabs->setCurrentIndex(1);

    auto* item = new QTreeWidgetItem;
    item->setText(0, QTime::currentTime().toString("HH:mm:ss"));
    item->setText(1, action);
    item->setText(2, path);
    item->setText(3, errText);
    item->setForeground(3, Qt::red);
    m_errorList->addTopLevelItem(item);
    m_errorList->scrollToItem(item);
}

void MainWindow::OnUISpeed(double mbps, qint64 done, qint64 total) {
    m_speedLabel->setText(FormatSpeed(mbps));
    if (total > 0 && mbps > 0) {
        double remMB = (total - done) / 1024.0 / 1024.0;
        m_etaLabel->setText(FormatETA(remMB / mbps));
    }
}

void MainWindow::OnUIQueueFinished(int total, int ok, int failed) {
    m_globalBar->setValue(1000);
    statusBar()->showMessage(
        QString(TR("msg.done")).arg(ok).arg(failed).arg(total));
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::ConnectEventBus() {
    auto& bus = EventBus::Instance();

    bus.OnFileStarted([this](const EventFileStarted& e) {
        if (e.path == L"__scanning__") {
            QMetaObject::invokeMethod(this, [this](){
                statusBar()->showMessage(TR("status.scanning"));
            }, Qt::QueuedConnection);
            return;
        }
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
            Q_ARG(QString, QString("Copy")),
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(QString, QString::fromStdWString(e.message)));
    });

    bus.OnSpeedUpdate([this](const EventSpeedUpdate& e) {
        QMetaObject::invokeMethod(this, "OnUISpeed", Qt::QueuedConnection,
            Q_ARG(double,  e.speedMBps),
            Q_ARG(qint64,  e.bytesDone),
            Q_ARG(qint64,  e.bytesTotal));
    });

    bus.OnQueueFinished([this](const EventQueueFinished& e) {
        QMetaObject::invokeMethod(this, "OnUIQueueFinished", Qt::QueuedConnection,
            Q_ARG(int, (int)e.total),
            Q_ARG(int, (int)e.succeeded),
            Q_ARG(int, (int)e.failed));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
QString MainWindow::FormatSize(qint64 b) const {
    if (b < 1024)           return QString("%1 B").arg(b);
    if (b < 1024*1024)      return QString("%1 KB").arg(b/1024.0, 0,'f',1);
    if (b < 1024*1024*1024) return QString("%1 MB").arg(b/1024.0/1024.0, 0,'f',2);
    return                         QString("%1 GB").arg(b/1024.0/1024.0/1024.0, 0,'f',2);
}

QString MainWindow::FormatETA(double secs) const {
    if (secs < 60)   return QString("%1s").arg((int)secs);
    if (secs < 3600) return QString("%1m%2s").arg((int)secs/60).arg((int)secs%60);
    return                  QString("%1h%2m").arg((int)secs/3600).arg(((int)secs%3600)/60);
}

QString MainWindow::FormatSpeed(double mbps) const {
    if (mbps < 1.0)  return QString("%1 KB/s").arg(mbps*1024, 0,'f',0);
    if (mbps < 1000) return QString("%1 MB/s").arg(mbps, 0,'f',1);
    return                  QString("%1 GB/s").arg(mbps/1024, 0,'f',2);
}
