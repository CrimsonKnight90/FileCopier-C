#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <QMainWindow>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QTabWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <atomic>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void requestCancel();

public slots:
    // Actualizaciones desde hilos de copia (via QueuedConnection)
    void OnUIFileStarted(const QString& path, qint64 total);
    void OnUIProgress(const QString& path, qint64 done, qint64 total);
    void OnUIFileCompleted(const QString& path, bool success);
    void OnUIError(const QString& path, const QString& action,
                   const QString& target, const QString& errText);
    void OnUISpeed(double mbps, qint64 done, qint64 total);
    void OnUIQueueFinished(int total, int ok, int failed);

private slots:
    void OnBrowseSrc();
    void OnBrowseDst();
    void OnStart();
    void OnPause();
    void OnCancel();
    void OnTogglePanel();
    void OnAddFiles();
    void OnRemoveSelected();
    void OnMoveToTop();
    void OnMoveUp();
    void OnMoveDown();
    void OnMoveToBottom();
    void OnSaveList();
    void OnLoadList();
    void OnClearErrors();
    void OnSaveErrors();
    void OnLanguageChanged(int index);
    void OnApplyOptions();

private:
    void BuildUI();
    void BuildCopyListTab(QWidget* parent);
    void BuildErrorTab(QWidget* parent);
    void BuildOptionsTab(QWidget* parent);
    void ConnectEventBus();
    void RetranslateUI();
    void ApplyMinSize();

    QString FormatSize(qint64 bytes) const;
    QString FormatETA(double secs) const;
    QString FormatSpeed(double mbps) const;

    // ── Layout principal ──────────────────────────────────────────────────
    QWidget*      m_topBar        = nullptr; // fila source/dest/progress
    QWidget*      m_panelWidget   = nullptr; // sección colapsable (tabs)
    QTabWidget*   m_tabs          = nullptr;
    QPushButton*  m_toggleBtn     = nullptr;
    bool          m_panelVisible  = false;

    // Controles superior
    QLineEdit*    m_srcEdit       = nullptr;
    QLineEdit*    m_dstEdit       = nullptr;
    QProgressBar* m_globalBar     = nullptr;
    QLabel*       m_speedLabel    = nullptr;
    QLabel*       m_etaLabel      = nullptr;
    QLabel*       m_statsLabel    = nullptr;
    QPushButton*  m_btnStart      = nullptr;
    QPushButton*  m_btnPause      = nullptr;
    QPushButton*  m_btnCancel     = nullptr;

    // ── Tab 1: Lista de Copia ─────────────────────────────────────────────
    QTreeWidget*  m_copyList      = nullptr;
    QPushButton*  m_btnToTop      = nullptr;
    QPushButton*  m_btnUp         = nullptr;
    QPushButton*  m_btnDown       = nullptr;
    QPushButton*  m_btnToBottom   = nullptr;
    QPushButton*  m_btnAddFiles   = nullptr;
    QPushButton*  m_btnRemove     = nullptr;
    QPushButton*  m_btnSaveList   = nullptr;
    QPushButton*  m_btnLoadList   = nullptr;

    // ── Tab 2: Informe de errores ─────────────────────────────────────────
    QTreeWidget*  m_errorList     = nullptr;
    QPushButton*  m_btnClearErr   = nullptr;
    QPushButton*  m_btnSaveErr    = nullptr;

    // ── Tab 3: Opciones ───────────────────────────────────────────────────
    // Fin de copia
    QRadioButton* m_rbNoCloseErr  = nullptr;
    QRadioButton* m_rbNoClose     = nullptr;
    QRadioButton* m_rbClose       = nullptr;
    // Colisiones
    QComboBox*    m_cmbCollision  = nullptr;
    // Errores de copia
    QComboBox*    m_cmbErrPolicy  = nullptr;
    // Performance
    QSpinBox*     m_spThreads     = nullptr;
    QSpinBox*     m_spBufferKB    = nullptr;
    QCheckBox*    m_chkNoBuffer   = nullptr;
    QCheckBox*    m_chkOverlap    = nullptr;
    QCheckBox*    m_chkVerify     = nullptr;
    // Idioma
    QComboBox*    m_cmbLanguage   = nullptr;
    QPushButton*  m_btnApplyOpts  = nullptr;

    // ── Estado de copia ───────────────────────────────────────────────────
    std::atomic<bool> m_copying   {false};
    std::atomic<bool> m_paused_   {false};
    qint64  m_totalBytes          = 0;
    qint64  m_doneBytes           = 0;
    int     m_totalFiles          = 0;
    int     m_doneFiles           = 0;
    int     m_failedFiles         = 0;

    // Worker thread (puntero para poder controlarlo)
    class CopyController;
    CopyController* m_controller  = nullptr;
};
