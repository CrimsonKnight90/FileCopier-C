// MainWindow.cpp

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
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QSplitter>
#include <atomic>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// CopyController
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow::CopyController : public QObject {
    Q_OBJECT
public:
    CopyController(const QString& src, const QString& dst,
                   const AppConfig& cfg, QObject* parent = nullptr)
        : QObject(parent), m_src(src), m_dst(dst), m_cfg(cfg) {}

    void RequestPause()  { m_pause  = true; }
    void RequestResume() { m_pause  = false; if (m_engine) m_engine->Resume(); }
    void RequestCancel() { m_cancel = true;  if (m_engine) m_engine->Cancel(); }

signals:
    void finished(int total, int ok, int failed);

public slots:
    void run() {
        DirectoryScanner scanner;
        std::vector<FileEntry> entries;
        EventBus::Instance().Emit(EventFileStarted{L"__scanning__", 0});
        scanner.Scan(m_src.toStdWString(), m_dst.toStdWString(), entries,
                     [](size_t /*n*/) {});

        int totalFiles = 0;
        for (auto& e : entries) if (!e.isDirectory) ++totalFiles;

        ThreadPool pool(m_cfg.threadCount);
        std::atomic<int> ok{0}, failed{0};

        CopyOptions opts;
        opts.bufferSizeBytes = m_cfg.bufferSizeKB * 1024;
        opts.maxRetries      = m_cfg.maxRetries;
        opts.retryDelayMs    = m_cfg.retryDelayMs;
        opts.useNoBuffering  = m_cfg.useNoBuffering;   // false por defecto
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
                            EventFileProgress{entry.srcPath, done, total, 0.0});
                    },
                    [&](const std::wstring& msg, DWORD code) {
                        EventBus::Instance().Emit(
                            EventError{entry.srcPath, msg, code});
                    }
                );
                m_engine = nullptr;

                if (!result.success) {
                    // Emitir error con detalle si no se emitió ya
                    if (result.lastError != 0) {
                        std::wstring errMsg = ErrorHandler::FormatWinError(result.lastError);
                        EventBus::Instance().Emit(
                            EventError{entry.srcPath, errMsg, result.lastError});
                    }
                    ++failed;
                } else {
                    ++ok;
                }
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
    FileCopyEngine* m_engine{nullptr};
};

#include "MainWindow.moc"

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(TR("app.title"));
    setMinimumWidth(680);
    setMinimumHeight(200);
    BuildUI();
    ConnectEventBus();
    LoadSettings();
}

MainWindow::~MainWindow() { SaveSettings(); }

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_copying) {
        auto btn = QMessageBox::question(this,
            TR("app.title"), TR("msg.confirmCancel"),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) { event->ignore(); return; }
        emit requestCancel();
        QThread::msleep(400);
    }
    SaveSettings();
    event->accept();
}

void MainWindow::SaveSettings() {
    QSettings s("FileCopierDev", "FileCopier");
    s.setValue("geometry", saveGeometry());
    s.setValue("panelVisible", m_panelVisible);
    // Opciones
    auto& cfg = ConfigManager::Instance().Config();
    s.setValue("threads",      (int)cfg.threadCount);
    s.setValue("bufferKB",     (int)cfg.bufferSizeKB);
    s.setValue("noBuffering",  cfg.useNoBuffering);
    s.setValue("overlapped",   cfg.useOverlapped);
    s.setValue("verify",       cfg.verifyAfterCopy);
    s.setValue("skipOnError",  cfg.skipOnError);
    s.setValue("endCopyMode",
        m_rbClose->isChecked() ? 2 : m_rbNoClose->isChecked() ? 1 : 0);
    s.setValue("collision",    m_cmbCollision->currentIndex());
    s.setValue("errPolicy",    m_cmbErrPolicy->currentIndex());
    s.setValue("language",     (int)Language::Instance().Current());
}

void MainWindow::LoadSettings() {
    QSettings s("FileCopierDev", "FileCopier");
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());

    auto& cfg = ConfigManager::Instance().Config();
    if (s.contains("threads"))     { cfg.threadCount    = s.value("threads").toInt();   m_spThreads->setValue((int)cfg.threadCount); }
    if (s.contains("bufferKB"))    { cfg.bufferSizeKB   = s.value("bufferKB").toInt();  m_spBufferKB->setValue((int)cfg.bufferSizeKB); }
    if (s.contains("noBuffering")) { cfg.useNoBuffering  = s.value("noBuffering").toBool(); m_chkNoBuffer->setChecked(cfg.useNoBuffering); }
    if (s.contains("overlapped"))  { cfg.useOverlapped   = s.value("overlapped").toBool();  m_chkOverlap->setChecked(cfg.useOverlapped); }
    if (s.contains("verify"))      { cfg.verifyAfterCopy = s.value("verify").toBool();       m_chkVerify->setChecked(cfg.verifyAfterCopy); }
    if (s.contains("skipOnError")) { cfg.skipOnError      = s.value("skipOnError").toBool(); }
    if (s.contains("endCopyMode")) {
        int m = s.value("endCopyMode").toInt();
        if (m == 2) m_rbClose->setChecked(true);
        else if (m == 1) m_rbNoClose->setChecked(true);
        else m_rbNoCloseErr->setChecked(true);
    }
    if (s.contains("collision"))  m_cmbCollision->setCurrentIndex(s.value("collision").toInt());
    if (s.contains("errPolicy"))  m_cmbErrPolicy->setCurrentIndex(s.value("errPolicy").toInt());
    if (s.contains("language")) {
        Lang lang = static_cast<Lang>(s.value("language").toInt());
        Language::Instance().SetLanguage(lang);
        m_cmbLanguage->setCurrentIndex(static_cast<int>(lang));
        RetranslateUI();
    }
    // Restaurar visibilidad del panel
    if (s.value("panelVisible", false).toBool()) {
        m_panelVisible = false;
        OnTogglePanel();
    }
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
    auto* lblSrc = new QLabel(TR("lbl.source"));
    lblSrc->setFixedWidth(60);
    rowSrc->addWidget(lblSrc);
    m_srcEdit = new QLineEdit;
    rowSrc->addWidget(m_srcEdit, 1);
    auto* btnSrc = new QPushButton(TR("btn.browse"));
    btnSrc->setFixedWidth(30);
    connect(btnSrc, &QPushButton::clicked, this, &MainWindow::OnBrowseSrc);
    rowSrc->addWidget(btnSrc);
    vRoot->addLayout(rowSrc);

    // ── Destino ───────────────────────────────────────────────────────────
    auto* rowDst = new QHBoxLayout;
    auto* lblDst = new QLabel(TR("lbl.dest"));
    lblDst->setFixedWidth(60);
    rowDst->addWidget(lblDst);
    m_dstEdit = new QLineEdit;
    rowDst->addWidget(m_dstEdit, 1);
    auto* btnDst = new QPushButton(TR("btn.browse"));
    btnDst->setFixedWidth(30);
    connect(btnDst, &QPushButton::clicked, this, &MainWindow::OnBrowseDst);
    rowDst->addWidget(btnDst);
    vRoot->addLayout(rowDst);

    // ── Barra de progreso ─────────────────────────────────────────────────
    m_globalBar = new QProgressBar;
    m_globalBar->setRange(0, 1000);
    m_globalBar->setValue(0);
    m_globalBar->setFixedHeight(18);
    m_globalBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    vRoot->addWidget(m_globalBar);

    // ── Fila stats + toggle + botones ─────────────────────────────────────
    auto* rowStats = new QHBoxLayout;
    rowStats->addWidget(new QLabel(TR("lbl.speed")));
    m_speedLabel = new QLabel("0 MB/s");
    m_speedLabel->setMinimumWidth(75);
    rowStats->addWidget(m_speedLabel);
    rowStats->addWidget(new QLabel("  ETA:"));
    m_etaLabel = new QLabel("--");
    m_etaLabel->setMinimumWidth(60);
    rowStats->addWidget(m_etaLabel);
    m_statsLabel = new QLabel("");
    rowStats->addWidget(m_statsLabel);
    rowStats->addStretch();

    m_toggleBtn = new QPushButton(TR("btn.toggle.show"));
    m_toggleBtn->setFixedSize(26, 22);
    m_toggleBtn->setToolTip("Mostrar/ocultar panel");
    connect(m_toggleBtn, &QPushButton::clicked, this, &MainWindow::OnTogglePanel);
    rowStats->addWidget(m_toggleBtn);
    rowStats->addSpacing(6);

    m_btnStart  = new QPushButton(TR("btn.start"));
    m_btnPause  = new QPushButton(TR("btn.pause"));
    m_btnCancel = new QPushButton(TR("btn.cancel"));
    m_btnPause->setEnabled(false);
    m_btnCancel->setEnabled(false);
    m_btnStart->setMinimumWidth(70);
    m_btnPause->setMinimumWidth(70);
    m_btnCancel->setMinimumWidth(70);
    connect(m_btnStart,  &QPushButton::clicked, this, &MainWindow::OnStart);
    connect(m_btnPause,  &QPushButton::clicked, this, &MainWindow::OnPause);
    connect(m_btnCancel, &QPushButton::clicked, this, &MainWindow::OnCancel);
    rowStats->addWidget(m_btnStart);
    rowStats->addWidget(m_btnPause);
    rowStats->addWidget(m_btnCancel);
    vRoot->addLayout(rowStats);

    // ── Panel colapsable ──────────────────────────────────────────────────
    // Altura mínima fija para que el panel no distorsione el área superior
    m_panelWidget = new QWidget;
    m_panelWidget->setVisible(false);
    m_panelWidget->setMinimumHeight(300);
    m_panelWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);

    auto* panelLayout = new QVBoxLayout(m_panelWidget);
    panelLayout->setContentsMargins(0, 2, 0, 0);

    m_tabs = new QTabWidget;
    panelLayout->addWidget(m_tabs);

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

    // El área superior NO debe estirarse cuando el panel está oculto
    // setSizeConstraint evita que QWidget intente redistribuir el espacio vacío
    vRoot->setSizeConstraint(QLayout::SetMinAndMaxSize);

    statusBar()->showMessage(TR("status.ready"));
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::BuildCopyListTab(QWidget* parent) {
    auto* h = new QHBoxLayout(parent);
    h->setContentsMargins(4, 4, 4, 4);

    m_copyList = new QTreeWidget;
    m_copyList->setColumnCount(4);
    m_copyList->setHeaderLabels(QStringList{
        TR("col.source"), TR("col.size"), TR("col.dest"), TR("col.status")});
    m_copyList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_copyList->setAlternatingRowColors(true);
    m_copyList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_copyList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_copyList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_copyList, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::OnCopyListContextMenu);
    h->addWidget(m_copyList, 1);

    auto* vBtns = new QVBoxLayout;
    vBtns->setSpacing(3);
    auto mkBtn = [&](QPushButton*& ptr, const QString& label) {
        ptr = new QPushButton(label);
        ptr->setMaximumWidth(140);
        ptr->setMinimumHeight(26);
        vBtns->addWidget(ptr);
    };
    mkBtn(m_btnToTop,    TR("btn.toTop"));
    mkBtn(m_btnUp,       TR("btn.up"));
    mkBtn(m_btnDown,     TR("btn.down"));
    mkBtn(m_btnToBottom, TR("btn.toBottom"));
    vBtns->addSpacing(6);
    mkBtn(m_btnAddFiles, TR("btn.addFiles"));
    mkBtn(m_btnRemove,   TR("btn.remove"));
    vBtns->addSpacing(6);
    mkBtn(m_btnSaveList, TR("btn.save"));
    mkBtn(m_btnLoadList, TR("btn.load"));
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
    m_errorList->setHeaderLabels(QStringList{
        TR("col.time"), TR("col.action"), TR("col.target"), TR("col.errtext")});
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

    // Fin de copia
    auto* grpEnd = new QGroupBox(TR("opt.endCopy"));
    auto* vEnd = new QVBoxLayout(grpEnd);
    m_rbNoCloseErr = new QRadioButton(TR("opt.noCloseErr")); m_rbNoCloseErr->setChecked(true);
    m_rbNoClose    = new QRadioButton(TR("opt.noClose"));
    m_rbClose      = new QRadioButton(TR("opt.close"));
    vEnd->addWidget(m_rbNoCloseErr);
    vEnd->addWidget(m_rbNoClose);
    vEnd->addWidget(m_rbClose);
    vMain->addWidget(grpEnd);

    // Colisiones
    auto* grpCol = new QGroupBox(TR("opt.collision"));
    auto* hCol = new QHBoxLayout(grpCol);
    m_cmbCollision = new QComboBox;
    m_cmbCollision->addItems(QStringList{
        TR("opt.col.ask"), TR("opt.col.cancel"), TR("opt.col.skip"),
        TR("opt.col.overwrite"), TR("opt.col.overwriteD"),
        TR("opt.col.renameNew"), TR("opt.col.renameOld")});
    hCol->addWidget(m_cmbCollision); hCol->addStretch();
    vMain->addWidget(grpCol);

    // Errores
    auto* grpErr = new QGroupBox(TR("opt.copyErr"));
    auto* hErr = new QHBoxLayout(grpErr);
    m_cmbErrPolicy = new QComboBox;
    m_cmbErrPolicy->addItems(QStringList{
        TR("opt.err.ask"), TR("opt.err.cancel"), TR("opt.err.skip"),
        TR("opt.err.retryLog"), TR("opt.err.moveEnd")});
    m_cmbErrPolicy->setCurrentIndex(3);
    hErr->addWidget(m_cmbErrPolicy); hErr->addStretch();
    vMain->addWidget(grpErr);

    // Rendimiento
    auto* grpPerf = new QGroupBox(TR("opt.perf"));
    auto* vPerf = new QVBoxLayout(grpPerf);
    auto& cfg = ConfigManager::Instance().Config();

    auto mkSpinRow = [&](QSpinBox*& sp, const QString& label, int mn, int mx, int val) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        sp = new QSpinBox;
        sp->setRange(mn, mx); sp->setValue(val); sp->setFixedWidth(80);
        row->addWidget(sp); row->addStretch();
        vPerf->addLayout(row);
    };
    mkSpinRow(m_spThreads,  TR("opt.threads"),  1, 64,    (int)cfg.threadCount);
    mkSpinRow(m_spBufferKB, TR("opt.bufferKB"), 64, 65536, (int)cfg.bufferSizeKB);

    m_chkNoBuffer = new QCheckBox(TR("opt.noBuffering")); m_chkNoBuffer->setChecked(cfg.useNoBuffering);
    m_chkOverlap  = new QCheckBox(TR("opt.overlapped"));  m_chkOverlap->setChecked(cfg.useOverlapped);
    m_chkVerify   = new QCheckBox(TR("opt.verify"));      m_chkVerify->setChecked(cfg.verifyAfterCopy);
    vPerf->addWidget(m_chkNoBuffer);
    vPerf->addWidget(m_chkOverlap);
    vPerf->addWidget(m_chkVerify);
    vMain->addWidget(grpPerf);

    // Idioma
    auto* grpLang = new QGroupBox(TR("opt.language"));
    auto* hLang = new QHBoxLayout(grpLang);
    m_cmbLanguage = new QComboBox;
    for (auto& [lang, name] : Language::Available())
        m_cmbLanguage->addItem(name, static_cast<int>(lang));
    m_cmbLanguage->setCurrentIndex(static_cast<int>(Language::Instance().Current()));
    connect(m_cmbLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnLanguageChanged);
    hLang->addWidget(m_cmbLanguage); hLang->addStretch();
    vMain->addWidget(grpLang);

    m_btnApplyOpts = new QPushButton(TR("opt.apply"));
    connect(m_btnApplyOpts, &QPushButton::clicked, this, &MainWindow::OnApplyOptions);
    vMain->addWidget(m_btnApplyOpts);
    vMain->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
// Toggle panel — tamaño fijo para el área superior
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnTogglePanel() {
    m_panelVisible = !m_panelVisible;
    m_panelWidget->setVisible(m_panelVisible);
    m_toggleBtn->setText(m_panelVisible ? TR("btn.toggle.hide") : TR("btn.toggle.show"));

    if (m_panelVisible) {
        setMinimumHeight(520);
        if (height() < 520) resize(width(), 620);
    } else {
        setMinimumHeight(200);
        setMaximumHeight(200); // bloquear altura cuando el panel está oculto
        resize(width(), 200);
    }
}

// Cuando el panel vuelve a mostrarse, quitar el límite de altura máxima
void MainWindow::resizeEvent(QResizeEvent* event) {
    if (m_panelVisible) setMaximumHeight(QWIDGETSIZE_MAX);
    QMainWindow::resizeEvent(event);
}

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
void MainWindow::OnStart() {
    if (m_srcEdit->text().isEmpty() || m_dstEdit->text().isEmpty()) {
        QMessageBox::warning(this, TR("app.title"), TR("msg.selectFolders"));
        return;
    }
    if (m_copying) return;

    m_globalBar->setValue(0);
    m_doneFiles = m_failedFiles = m_totalFiles = 0;
    m_doneBytes = m_totalBytes = 0;
    m_statsLabel->clear();
    m_speedLabel->setText("0 MB/s");
    m_etaLabel->setText("--");
    m_copyList->clear();
    m_errorList->clear();

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
            m_paused_ = false;

            // Política fin de copia
            bool hasErrors = (failed > 0);
            if (m_rbClose->isChecked() ||
                (!hasErrors && m_rbNoCloseErr->isChecked())) {
                QTimer::singleShot(1500, this, &QWidget::close);
            }

            statusBar()->showMessage(
                QString(TR("msg.done")).arg(ok).arg(failed).arg(total));
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
// Menú contextual lista de copia
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnCopyListContextMenu(const QPoint& pos) {
    QMenu menu(this);
    menu.addAction(TR("btn.toTop"),    this, &MainWindow::OnMoveToTop);
    menu.addAction(TR("btn.up"),       this, &MainWindow::OnMoveUp);
    menu.addAction(TR("btn.down"),     this, &MainWindow::OnMoveDown);
    menu.addAction(TR("btn.toBottom"), this, &MainWindow::OnMoveToBottom);
    menu.addSeparator();
    menu.addAction(TR("btn.remove"),   this, &MainWindow::OnRemoveSelected);
    menu.addSeparator();
    menu.addAction(TR("ctx.selectAll"), this, [this](){
        m_copyList->selectAll();
    });
    menu.addAction(TR("ctx.invertSel"), this, [this](){
        for (int i = 0; i < m_copyList->topLevelItemCount(); ++i) {
            auto* it = m_copyList->topLevelItem(i);
            it->setSelected(!it->isSelected());
        }
    });
    menu.addAction(TR("ctx.sort"), this, [this](){
        m_copyList->sortByColumn(0, Qt::AscendingOrder);
    });
    menu.exec(m_copyList->viewport()->mapToGlobal(pos));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lista de copia — orden
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnMoveToTop() {
    auto sel = m_copyList->selectedItems();
    for (auto* item : sel) {
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row <= 0) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(0, item);
        item->setSelected(true);
    }
}
void MainWindow::OnMoveUp() {
    auto sel = m_copyList->selectedItems();
    for (auto* item : sel) {
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row <= 0) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(row - 1, item);
        item->setSelected(true);
    }
}
void MainWindow::OnMoveDown() {
    auto sel = m_copyList->selectedItems();
    int total = m_copyList->topLevelItemCount();
    for (int i = sel.size()-1; i >= 0; --i) {
        auto* item = sel[i];
        int row = m_copyList->indexOfTopLevelItem(item);
        if (row >= total-1) continue;
        m_copyList->takeTopLevelItem(row);
        m_copyList->insertTopLevelItem(row+1, item);
        item->setSelected(true);
    }
}
void MainWindow::OnMoveToBottom() {
    auto sel = m_copyList->selectedItems();
    for (int i = sel.size()-1; i >= 0; --i) {
        auto* item = sel[i];
        int row = m_copyList->indexOfTopLevelItem(item);
        m_copyList->takeTopLevelItem(row);
        m_copyList->addTopLevelItem(item);
        item->setSelected(true);
    }
}
void MainWindow::OnAddFiles() {
    // Permitir añadir archivos Y carpetas
    QFileDialog dlg(this, TR("btn.addFiles"));
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setOption(QFileDialog::DontUseNativeDialog, false);
    if (!dlg.exec()) return;

    auto dstBase = m_dstEdit->text();
    for (auto& f : dlg.selectedFiles()) {
        QFileInfo fi(f);
        auto* item = new QTreeWidgetItem;
        item->setText(0, f);
        item->setText(1, fi.isDir() ? "[dir]" : FormatSize(fi.size()));
        item->setText(2, dstBase.isEmpty() ? "" : dstBase + "/" + fi.fileName());
        item->setText(3, TR("status.ready"));
        m_copyList->addTopLevelItem(item);
    }
}
void MainWindow::OnRemoveSelected() {
    for (auto* item : m_copyList->selectedItems()) delete item;
}
void MainWindow::OnSaveList() {
    auto path = QFileDialog::getSaveFileName(this, TR("btn.save"), "",
        "FileCopier List (*.fcl);;Text (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (int i = 0; i < m_copyList->topLevelItemCount(); ++i) {
        auto* it = m_copyList->topLevelItem(i);
        ts << it->text(0) << "|" << it->text(2) << "\n";
    }
}
void MainWindow::OnLoadList() {
    auto path = QFileDialog::getOpenFileName(this, TR("btn.load"), "",
        "FileCopier List (*.fcl);;Text (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        auto line = ts.readLine().trimmed();
        if (line.isEmpty()) continue;
        auto parts = line.split("|");
        if (parts.size() < 2) continue;
        QFileInfo fi(parts[0]);
        auto* item = new QTreeWidgetItem;
        item->setText(0, parts[0]);
        item->setText(1, fi.isDir() ? "[dir]" : FormatSize(fi.size()));
        item->setText(2, parts[1]);
        item->setText(3, TR("status.ready"));
        m_copyList->addTopLevelItem(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnClearErrors() { m_errorList->clear(); }
void MainWindow::OnSaveErrors() {
    auto path = QFileDialog::getSaveFileName(this, TR("btn.saveErrors"), "", "Text (*.txt)");
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
void MainWindow::OnLanguageChanged(int index) {
    Language::Instance().SetLanguage(static_cast<Lang>(
        m_cmbLanguage->itemData(index).toInt()));
    RetranslateUI();
}
void MainWindow::OnApplyOptions() {
    auto& cfg = ConfigManager::Instance().Config();
    cfg.threadCount    = static_cast<size_t>(m_spThreads->value());
    cfg.bufferSizeKB   = static_cast<DWORD>(m_spBufferKB->value());
    cfg.useNoBuffering = m_chkNoBuffer->isChecked();
    cfg.useOverlapped  = m_chkOverlap->isChecked();
    cfg.verifyAfterCopy= m_chkVerify->isChecked();
    SaveSettings();
    ConfigManager::Instance().Save(L"filecopier.ini");
    statusBar()->showMessage(TR("status.optSaved"), 3000);
}
void MainWindow::RetranslateUI() {
    setWindowTitle(TR("app.title"));
    m_btnStart->setText(TR("btn.start"));
    m_btnPause->setText(m_paused_ ? TR("btn.resume") : TR("btn.pause"));
    m_btnCancel->setText(TR("btn.cancel"));
    m_tabs->setTabText(0, TR("tab.copylist"));
    m_tabs->setTabText(1, TR("tab.errors"));
    m_tabs->setTabText(2, TR("tab.options"));
    statusBar()->showMessage(m_copying ? TR("lbl.copying") : TR("status.ready"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots de progreso
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::OnUIFileStarted(const QString& path, qint64 total) {
    if (path == "__scanning__") {
        statusBar()->showMessage(TR("status.scanning")); return;
    }
    ++m_totalFiles;
    if (m_panelVisible) {
        auto* item = new QTreeWidgetItem;
        item->setText(0, path);
        item->setText(1, FormatSize(total));
        item->setText(2, m_dstEdit->text());
        item->setText(3, TR("lbl.copying"));
        m_copyList->insertTopLevelItem(0, item);
    }
}
void MainWindow::OnUIProgress(const QString& /*path*/, qint64 done, qint64 total) {
    if (total > 0) m_globalBar->setValue(static_cast<int>(done * 1000 / total));
}
void MainWindow::OnUIFileCompleted(const QString& path, bool success) {
    success ? ++m_doneFiles : ++m_failedFiles;
    m_statsLabel->setText(QString("%1/%2 | %3 err")
        .arg(m_doneFiles + m_failedFiles).arg(m_totalFiles).arg(m_failedFiles));
    if (!m_panelVisible) return;
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
                           const QString& /*target*/, const QString& errText) {
    // Mostrar pestaña errores automáticamente
    if (m_panelVisible) {
        m_tabs->setCurrentIndex(1);
    } else {
        // Abrir panel si estaba cerrado para que el usuario vea el error
        m_panelVisible = false;
        OnTogglePanel();
        m_tabs->setCurrentIndex(1);
    }
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
    if (total > 0 && mbps > 0.01) {
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
        QMetaObject::invokeMethod(this, "OnUIFileStarted", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(qint64, e.totalBytes));
    });
    bus.OnFileProgress([this](const EventFileProgress& e) {
        QMetaObject::invokeMethod(this, "OnUIProgress", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(qint64, e.bytesCopied), Q_ARG(qint64, e.totalBytes));
    });
    bus.OnFileCompleted([this](const EventFileCompleted& e) {
        QMetaObject::invokeMethod(this, "OnUIFileCompleted", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(bool, e.success));
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
            Q_ARG(double, e.speedMBps),
            Q_ARG(qint64, e.bytesDone), Q_ARG(qint64, e.bytesTotal));
    });
    bus.OnQueueFinished([this](const EventQueueFinished& e) {
        QMetaObject::invokeMethod(this, "OnUIQueueFinished", Qt::QueuedConnection,
            Q_ARG(int,(int)e.total), Q_ARG(int,(int)e.succeeded), Q_ARG(int,(int)e.failed));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
QString MainWindow::FormatSize(qint64 b) const {
    if (b < 1024)           return QString("%1 B").arg(b);
    if (b < 1024*1024)      return QString("%1 KB").arg(b/1024.0,0,'f',1);
    if (b < 1024*1024*1024) return QString("%1 MB").arg(b/1024.0/1024.0,0,'f',2);
    return                         QString("%1 GB").arg(b/1024.0/1024.0/1024.0,0,'f',2);
}
QString MainWindow::FormatETA(double s) const {
    if (s < 60)   return QString("%1s").arg((int)s);
    if (s < 3600) return QString("%1m%2s").arg((int)s/60).arg((int)s%60);
    return               QString("%1h%2m").arg((int)s/3600).arg(((int)s%3600)/60);
}
QString MainWindow::FormatSpeed(double mbps) const {
    if (mbps < 1.0)  return QString("%1 KB/s").arg(mbps*1024,0,'f',0);
    if (mbps < 1000) return QString("%1 MB/s").arg(mbps,0,'f',1);
    return                  QString("%1 GB/s").arg(mbps/1024,0,'f',2);
}
