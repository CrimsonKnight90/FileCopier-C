// ConfigDialog.cpp — Ventana de configuración global de FileCopier
// Sincronizada con AppConfig y con la pestaña Opciones de MainWindow

#include "../../include/ui/ConfigDialog.h"
#include "../../include/Utils.h"
#include "../../include/Language.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QCoreApplication>
#include <QFileDialog>
#include <QSettings>
#include <QTimer>
#include <windows.h>

using namespace FileCopier;

ConfigDialog::ConfigDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint)
{
    setWindowTitle(TR("cfg.title"));
    setMinimumSize(550, 500);
    BuildUI();
    LoadFromConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
void ConfigDialog::BuildUI() {
    auto* vRoot = new QVBoxLayout(this);

    m_tabs = new QTabWidget;
    vRoot->addWidget(m_tabs, 1);

    // ── Tab: Idioma ───────────────────────────────────────────────────────
    auto* tabLang = new QWidget;
    {
        auto* v = new QVBoxLayout(tabLang);
        m_grpLang = new QGroupBox(TR("opt.language"));
        auto* h = new QHBoxLayout(m_grpLang);
        m_cmbLang = new QComboBox;
        for (auto& [lang, name] : Language::Available())
            m_cmbLang->addItem(name, (int)lang);
        m_cmbLang->setCurrentIndex((int)Language::Instance().Current());
        connect(m_cmbLang, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ConfigDialog::OnLanguageChanged);
        h->addWidget(m_cmbLang); h->addStretch();
        v->addWidget(m_grpLang); v->addStretch();
    }
    m_tabs->addTab(tabLang, TR("cfg.tab.lang"));

    // ── Tab: Arranque ─────────────────────────────────────────────────────
    auto* tabStartup = new QWidget;
    {
        auto* v = new QVBoxLayout(tabStartup);
        m_grpStartup = new QGroupBox(TR("cfg.startup"));
        auto* vg = new QVBoxLayout(m_grpStartup);
        m_chkAutoStart       = new QCheckBox(TR("cfg.autostart"));
        m_chkActivateOnStart = new QCheckBox(TR("cfg.activateOnStart"));
        vg->addWidget(m_chkAutoStart);
        vg->addWidget(m_chkActivateOnStart);
        v->addWidget(m_grpStartup); v->addStretch();
    }
    m_tabs->addTab(tabStartup, TR("cfg.tab.startup"));

    // ── Tab: Ajustes de copia ─────────────────────────────────────────────
    auto* tabSettings = new QWidget;
    {
        auto* scroll = new QScrollArea(tabSettings);
        scroll->setWidgetResizable(true);
        auto* outer = new QVBoxLayout(tabSettings);
        outer->setContentsMargins(0,0,0,0); outer->addWidget(scroll);
        auto* container = new QWidget;
        scroll->setWidget(container);
        auto* v = new QVBoxLayout(container);
        v->setAlignment(Qt::AlignTop);

        // Fin de copia
        m_grpEndCopy = new QGroupBox(TR("opt.endCopy"));
        auto* vEnd = new QVBoxLayout(m_grpEndCopy);
        m_rbNoCloseErr = new QRadioButton(TR("opt.noCloseErr")); m_rbNoCloseErr->setChecked(true);
        m_rbNoClose    = new QRadioButton(TR("opt.noClose"));
        m_rbClose      = new QRadioButton(TR("opt.close"));
        vEnd->addWidget(m_rbNoCloseErr); vEnd->addWidget(m_rbNoClose); vEnd->addWidget(m_rbClose);
        v->addWidget(m_grpEndCopy);

        // Colisiones
        m_grpCollision = new QGroupBox(TR("opt.collision"));
        auto* hCol = new QHBoxLayout(m_grpCollision);
        m_cmbCollision = new QComboBox;
        m_cmbCollision->addItems(QStringList{
            TR("opt.col.ask"), TR("opt.col.cancel"), TR("opt.col.skip"),
            TR("opt.col.overwrite"), TR("opt.col.overwriteD"),
            TR("opt.col.renameNew"), TR("opt.col.renameOld")});
        hCol->addWidget(m_cmbCollision); hCol->addStretch();
        v->addWidget(m_grpCollision);

        // Errores de copia
        m_grpCopyErr = new QGroupBox(TR("opt.copyErr"));
        auto* hErr = new QHBoxLayout(m_grpCopyErr);
        m_cmbCopyErr = new QComboBox;
        m_cmbCopyErr->addItems(QStringList{
            TR("opt.err.ask"), TR("opt.err.cancel"), TR("opt.err.skip"),
            TR("opt.err.retryLog"), TR("opt.err.moveEnd")});
        m_cmbCopyErr->setCurrentIndex(3);
        hErr->addWidget(m_cmbCopyErr); hErr->addStretch();
        v->addWidget(m_grpCopyErr);
        v->addStretch();
    }
    m_tabs->addTab(tabSettings, TR("cfg.tab.settings"));

    // ── Tab: Comportamiento ───────────────────────────────────────────────
    auto* tabBehavior = new QWidget;
    {
        auto* v = new QVBoxLayout(tabBehavior);
        v->setAlignment(Qt::AlignTop);

        // Nuevas listas
        m_grpBehavior = new QGroupBox(TR("cfg.newList"));
        auto* h = new QHBoxLayout(m_grpBehavior);
        m_cmbNewList = new QComboBox;
        m_cmbNewList->addItems(QStringList{
            TR("cfg.newList.add"), TR("cfg.newList.replace"), TR("cfg.newList.ask")});
        h->addWidget(m_cmbNewList); h->addStretch();
        v->addWidget(m_grpBehavior);

        // Supresión
        m_grpDeletion = new QGroupBox(TR("cfg.deletion"));
        auto* vDel = new QVBoxLayout(m_grpDeletion);
        m_rbDelAlways  = new QRadioButton(TR("cfg.del.always")); m_rbDelAlways->setChecked(true);
        m_rbDelNoError = new QRadioButton(TR("cfg.del.noError"));
        vDel->addWidget(m_rbDelAlways); vDel->addWidget(m_rbDelNoError);
        v->addWidget(m_grpDeletion);

        // Retitulación
        m_grpRename = new QGroupBox(TR("cfg.rename"));
        auto* vRen = new QVBoxLayout(m_grpRename);
        auto mkRow = [&](QLabel*& lbl, QLineEdit*& ed, const QString& labelTxt, const QString& def) {
            auto* row = new QHBoxLayout;
            lbl = new QLabel(labelTxt); lbl->setFixedWidth(180);
            ed  = new QLineEdit(def);
            row->addWidget(lbl); row->addWidget(ed);
            vRen->addLayout(row);
        };
        QLabel *l1=nullptr, *l2=nullptr;
        mkRow(l1, m_edtOldPattern, TR("cfg.rename.old"), "<name>_Old<#>.<ext>");
        mkRow(l2, m_edtNewPattern, TR("cfg.rename.new"), "<name>_New<#>.<ext>");
        v->addWidget(m_grpRename);
        v->addStretch();
    }
    m_tabs->addTab(tabBehavior, TR("cfg.tab.behavior"));

    // ── Tab: Informe de errores ───────────────────────────────────────────
    auto* tabErrors = new QWidget;
    {
        auto* v = new QVBoxLayout(tabErrors);
        v->setAlignment(Qt::AlignTop);
        m_grpErrorLog = new QGroupBox(TR("cfg.errorLog"));
        auto* vg = new QVBoxLayout(m_grpErrorLog);

        m_chkAutoSave = new QCheckBox(TR("cfg.autoSaveLog"));
        connect(m_chkAutoSave, &QCheckBox::toggled,
                this, &ConfigDialog::OnAutoSaveErrorToggled);
        vg->addWidget(m_chkAutoSave);

        auto* rowPath = new QHBoxLayout;
        rowPath->addWidget(new QLabel(TR("cfg.logPath")));
        m_edtLogPath = new QLineEdit;
        m_edtLogPath->setEnabled(false);
        rowPath->addWidget(m_edtLogPath);
        m_btnBrowseLog = new QPushButton("...");
        m_btnBrowseLog->setFixedWidth(28);
        m_btnBrowseLog->setEnabled(false);
        connect(m_btnBrowseLog, &QPushButton::clicked, this, &ConfigDialog::OnBrowseErrorLog);
        rowPath->addWidget(m_btnBrowseLog);
        vg->addLayout(rowPath);

        auto* rowName = new QHBoxLayout;
        rowName->addWidget(new QLabel(TR("cfg.logName")));
        m_edtLogName = new QLineEdit("filecopier_errors");
        m_edtLogName->setEnabled(false);
        rowName->addWidget(m_edtLogName);
        vg->addLayout(rowName);

        v->addWidget(m_grpErrorLog); v->addStretch();
    }
    m_tabs->addTab(tabErrors, TR("cfg.tab.errors"));

    // ── Tab: Procesos ─────────────────────────────────────────────────────
    auto* tabProcess = new QWidget;
    {
        auto* v = new QVBoxLayout(tabProcess);
        v->setAlignment(Qt::AlignTop);
        m_grpProcess = new QGroupBox(TR("cfg.process"));
        auto* vg = new QVBoxLayout(m_grpProcess);

        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(TR("cfg.process.name")));
        m_edtProcess = new QLineEdit("explorer.exe");
        row->addWidget(m_edtProcess);
        vg->addLayout(row);

        m_lblProcessInfo = new QLabel(TR("cfg.process.info"));
        m_lblProcessInfo->setWordWrap(true);
        m_lblProcessInfo->setStyleSheet("color: gray; font-size: 10px;");
        vg->addWidget(m_lblProcessInfo);

        v->addWidget(m_grpProcess); v->addStretch();
    }
    m_tabs->addTab(tabProcess, TR("cfg.tab.process"));

    // ── Tab: Rendimiento ──────────────────────────────────────────────────
    auto* tabPerf = new QWidget;
    {
        auto* v = new QVBoxLayout(tabPerf);
        v->setAlignment(Qt::AlignTop);
        m_grpPerf = new QGroupBox(TR("opt.perf"));
        auto* vg = new QVBoxLayout(m_grpPerf);
        auto& cfg = ConfigManager::Instance().Config();
        auto mkSpin = [&](QSpinBox*& sp, const QString& lbl, int mn, int mx, int val) {
            auto* row = new QHBoxLayout;
            row->addWidget(new QLabel(lbl));
            sp = new QSpinBox; sp->setRange(mn,mx); sp->setValue(val); sp->setFixedWidth(80);
            row->addWidget(sp); row->addStretch();
            vg->addLayout(row);
        };
        mkSpin(m_spThreads,  TR("opt.threads"),  1, 64,     (int)cfg.threadCount);
        mkSpin(m_spBufferKB, TR("opt.bufferKB"), 64, 65536, (int)cfg.bufferSizeKB);
        m_chkNoBuffer = new QCheckBox(TR("opt.noBuffering")); m_chkNoBuffer->setChecked(false);
        m_chkOverlap  = new QCheckBox(TR("opt.overlapped"));  m_chkOverlap->setChecked(false);
        m_chkVerify   = new QCheckBox(TR("opt.verify"));      m_chkVerify->setChecked(false);
        vg->addWidget(m_chkNoBuffer); vg->addWidget(m_chkOverlap); vg->addWidget(m_chkVerify);
        v->addWidget(m_grpPerf); v->addStretch();
    }
    m_tabs->addTab(tabPerf, TR("cfg.tab.perf"));

    // ── Tab: Avanzado ─────────────────────────────────────────────────────
    auto* tabAdv = new QWidget;
    {
        auto* v = new QVBoxLayout(tabAdv);
        v->setAlignment(Qt::AlignTop);
        m_grpAdvanced = new QGroupBox(TR("cfg.advanced"));
        auto* h = new QHBoxLayout(m_grpAdvanced);
        h->addWidget(new QLabel(TR("cfg.priority")));
        m_cmbPriority = new QComboBox;
        m_cmbPriority->addItems(QStringList{
            TR("cfg.priority.normal"), TR("cfg.priority.low"), TR("cfg.priority.high")});
        h->addWidget(m_cmbPriority); h->addStretch();
        v->addWidget(m_grpAdvanced); v->addStretch();
    }
    m_tabs->addTab(tabAdv, TR("cfg.tab.advanced"));

    // ── Botones globales ──────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    m_btnOK     = new QPushButton(TR("btn.ok"));
    m_btnApply  = new QPushButton(TR("opt.apply"));
    m_btnCancel = new QPushButton(TR("btn.cancel"));
    m_btnOK->setDefault(true);
    btnRow->addStretch();
    btnRow->addWidget(m_btnOK);
    btnRow->addWidget(m_btnApply);
    btnRow->addWidget(m_btnCancel);
    vRoot->addLayout(btnRow);

    connect(m_btnOK,     &QPushButton::clicked, this, &ConfigDialog::OnOK);
    connect(m_btnApply,  &QPushButton::clicked, this, &ConfigDialog::OnApply);
    connect(m_btnCancel, &QPushButton::clicked, this, &ConfigDialog::OnCancel);
}

// ─────────────────────────────────────────────────────────────────────────────
void ConfigDialog::LoadFromConfig() {
    auto& cfg = ConfigManager::Instance().Config();
    m_spThreads->setValue((int)cfg.threadCount);
    m_spBufferKB->setValue((int)cfg.bufferSizeKB);
    m_chkNoBuffer->setChecked(cfg.useNoBuffering);
    m_chkOverlap->setChecked(cfg.useOverlapped);
    m_chkVerify->setChecked(cfg.verifyAfterCopy);

    QSettings s("FileCopierDev", "FileCopier");
    m_cmbCollision->setCurrentIndex(s.value("collision", 0).toInt());
    m_cmbCopyErr->setCurrentIndex(s.value("errPolicy", 3).toInt());
    int endMode = s.value("endMode", 0).toInt();
    if (endMode==2) m_rbClose->setChecked(true);
    else if (endMode==1) m_rbNoClose->setChecked(true);
    else m_rbNoCloseErr->setChecked(true);
    m_chkAutoSave->setChecked(s.value("autoSaveLog", false).toBool());
    m_edtLogPath->setText(s.value("logPath", "").toString());
    m_edtLogName->setText(s.value("logName", "filecopier_errors").toString());
    m_edtProcess->setText(s.value("process", "explorer.exe").toString());
    m_cmbPriority->setCurrentIndex(s.value("priority", 0).toInt());
    m_cmbNewList->setCurrentIndex(s.value("newListBehavior", 0).toInt());
    m_rbDelAlways->setChecked(!s.value("delOnlySuccess", false).toBool());
    m_rbDelNoError->setChecked(s.value("delOnlySuccess", false).toBool());
    m_edtOldPattern->setText(s.value("oldPattern", "<name>_Old<#>.<ext>").toString());
    m_edtNewPattern->setText(s.value("newPattern", "<name>_New<#>.<ext>").toString());
    OnAutoSaveErrorToggled(m_chkAutoSave->isChecked());
}

void ConfigDialog::SaveToConfig() {
    auto& cfg = ConfigManager::Instance().Config();
    cfg.threadCount    = (size_t)m_spThreads->value();
    cfg.bufferSizeKB   = (DWORD)m_spBufferKB->value();
    cfg.useNoBuffering = m_chkNoBuffer->isChecked();
    cfg.useOverlapped  = m_chkOverlap->isChecked();
    cfg.verifyAfterCopy= m_chkVerify->isChecked();

    QSettings s("FileCopierDev", "FileCopier");
    s.setValue("collision", m_cmbCollision->currentIndex());
    s.setValue("errPolicy", m_cmbCopyErr->currentIndex());
    s.setValue("endMode",
        m_rbClose->isChecked() ? 2 : m_rbNoClose->isChecked() ? 1 : 0);
    s.setValue("autoSaveLog", m_chkAutoSave->isChecked());
    s.setValue("logPath",     m_edtLogPath->text());
    s.setValue("logName",     m_edtLogName->text());
    s.setValue("process",     m_edtProcess->text());
    s.setValue("priority",    m_cmbPriority->currentIndex());
    s.setValue("newListBehavior", m_cmbNewList->currentIndex());
    s.setValue("delOnlySuccess",  m_rbDelNoError->isChecked());
    s.setValue("oldPattern",  m_edtOldPattern->text());
    s.setValue("newPattern",  m_edtNewPattern->text());
    s.setValue("language",    (int)Language::Instance().Current());
    s.setValue("threads",     (int)cfg.threadCount);
    s.setValue("bufferKB",    (int)cfg.bufferSizeKB);
    s.setValue("noBuffer",    cfg.useNoBuffering);
    s.setValue("overlapped",  cfg.useOverlapped);
    s.setValue("verify",      cfg.verifyAfterCopy);
    ConfigManager::Instance().Save(L"filecopier.ini");

    // Aplicar prioridad del proceso
    DWORD priority = NORMAL_PRIORITY_CLASS;
    if (m_cmbPriority->currentIndex() == 1) priority = BELOW_NORMAL_PRIORITY_CLASS;
    if (m_cmbPriority->currentIndex() == 2) priority = ABOVE_NORMAL_PRIORITY_CLASS;
    ::SetPriorityClass(::GetCurrentProcess(), priority);

    // Arranque con Windows
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (m_chkAutoStart->isChecked())
        reg.setValue("FileCopier", QCoreApplication::applicationFilePath());
    else
        reg.remove("FileCopier");

    emit settingsChanged();
}

void ConfigDialog::OnApply()  { SaveToConfig(); }
void ConfigDialog::OnOK()     { SaveToConfig(); accept(); }
void ConfigDialog::OnCancel() { reject(); }

void ConfigDialog::OnLanguageChanged(int index) {
    Language::Instance().SetLanguage(
        static_cast<Lang>(m_cmbLang->itemData(index).toInt()));
    RetranslateUI();
    emit settingsChanged();
}

void ConfigDialog::OnAutoSaveErrorToggled(bool checked) {
    m_edtLogPath->setEnabled(checked);
    m_edtLogName->setEnabled(checked);
    m_btnBrowseLog->setEnabled(checked);
}

void ConfigDialog::OnBrowseErrorLog() {
    auto dir = QFileDialog::getExistingDirectory(this, TR("cfg.logPath"));
    if (!dir.isEmpty()) m_edtLogPath->setText(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
void ConfigDialog::RetranslateUI() {
    setWindowTitle(TR("cfg.title"));
    m_tabs->setTabText(0, TR("cfg.tab.lang"));
    m_tabs->setTabText(1, TR("cfg.tab.startup"));
    m_tabs->setTabText(2, TR("cfg.tab.settings"));
    m_tabs->setTabText(3, TR("cfg.tab.behavior"));
    m_tabs->setTabText(4, TR("cfg.tab.errors"));
    m_tabs->setTabText(5, TR("cfg.tab.process"));
    m_tabs->setTabText(6, TR("cfg.tab.perf"));
    m_tabs->setTabText(7, TR("cfg.tab.advanced"));
    m_grpLang->setTitle(TR("opt.language"));
    m_grpStartup->setTitle(TR("cfg.startup"));
    m_chkAutoStart->setText(TR("cfg.autostart"));
    m_chkActivateOnStart->setText(TR("cfg.activateOnStart"));
    m_grpEndCopy->setTitle(TR("opt.endCopy"));
    m_rbNoCloseErr->setText(TR("opt.noCloseErr"));
    m_rbNoClose->setText(TR("opt.noClose"));
    m_rbClose->setText(TR("opt.close"));
    m_grpCollision->setTitle(TR("opt.collision"));
    m_grpCopyErr->setTitle(TR("opt.copyErr"));
    m_grpBehavior->setTitle(TR("cfg.newList"));
    m_grpDeletion->setTitle(TR("cfg.deletion"));
    m_rbDelAlways->setText(TR("cfg.del.always"));
    m_rbDelNoError->setText(TR("cfg.del.noError"));
    m_grpRename->setTitle(TR("cfg.rename"));
    m_grpErrorLog->setTitle(TR("cfg.errorLog"));
    m_chkAutoSave->setText(TR("cfg.autoSaveLog"));
    m_grpProcess->setTitle(TR("cfg.process"));
    m_lblProcessInfo->setText(TR("cfg.process.info"));
    m_grpPerf->setTitle(TR("opt.perf"));
    m_chkNoBuffer->setText(TR("opt.noBuffering"));
    m_chkOverlap->setText(TR("opt.overlapped"));
    m_chkVerify->setText(TR("opt.verify"));
    m_grpAdvanced->setTitle(TR("cfg.advanced"));
    m_btnOK->setText(TR("btn.ok"));
    m_btnApply->setText(TR("opt.apply"));
    m_btnCancel->setText(TR("btn.cancel"));
}
