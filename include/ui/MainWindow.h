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
#include <QResizeEvent>
#include <atomic>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void requestCancel();

public slots:
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
    void OnCopyListContextMenu(const QPoint& pos);
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
    void SaveSettings();
    void LoadSettings();

    QString FormatSize(qint64 bytes) const;
    QString FormatETA(double secs) const;
    QString FormatSpeed(double mbps) const;

    // Layout
    QWidget*      m_panelWidget   = nullptr;
    QTabWidget*   m_tabs          = nullptr;
    QPushButton*  m_toggleBtn     = nullptr;
    bool          m_panelVisible  = false;

    // Controles superiores
    QLineEdit*    m_srcEdit       = nullptr;
    QLineEdit*    m_dstEdit       = nullptr;
    QProgressBar* m_globalBar     = nullptr;
    QLabel*       m_speedLabel    = nullptr;
    QLabel*       m_etaLabel      = nullptr;
    QLabel*       m_statsLabel    = nullptr;
    QPushButton*  m_btnStart      = nullptr;
    QPushButton*  m_btnPause      = nullptr;
    QPushButton*  m_btnCancel     = nullptr;

    // Tab lista de copia
    QTreeWidget*  m_copyList      = nullptr;
    QPushButton*  m_btnToTop      = nullptr;
    QPushButton*  m_btnUp         = nullptr;
    QPushButton*  m_btnDown       = nullptr;
    QPushButton*  m_btnToBottom   = nullptr;
    QPushButton*  m_btnAddFiles   = nullptr;
    QPushButton*  m_btnRemove     = nullptr;
    QPushButton*  m_btnSaveList   = nullptr;
    QPushButton*  m_btnLoadList   = nullptr;

    // Tab errores
    QTreeWidget*  m_errorList     = nullptr;
    QPushButton*  m_btnClearErr   = nullptr;
    QPushButton*  m_btnSaveErr    = nullptr;

    // Tab opciones
    QRadioButton* m_rbNoCloseErr  = nullptr;
    QRadioButton* m_rbNoClose     = nullptr;
    QRadioButton* m_rbClose       = nullptr;
    QComboBox*    m_cmbCollision  = nullptr;
    QComboBox*    m_cmbErrPolicy  = nullptr;
    QSpinBox*     m_spThreads     = nullptr;
    QSpinBox*     m_spBufferKB    = nullptr;
    QCheckBox*    m_chkNoBuffer   = nullptr;
    QCheckBox*    m_chkOverlap    = nullptr;
    QCheckBox*    m_chkVerify     = nullptr;
    QComboBox*    m_cmbLanguage   = nullptr;
    QPushButton*  m_btnApplyOpts  = nullptr;

    // Estado
    std::atomic<bool> m_copying  {false};
    bool              m_paused_  {false};
    qint64  m_totalBytes = 0, m_doneBytes = 0;
    int     m_totalFiles = 0, m_doneFiles = 0, m_failedFiles = 0;

    class CopyController;
    CopyController* m_controller = nullptr;
};
