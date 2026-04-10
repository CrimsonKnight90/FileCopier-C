#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QRadioButton>
#include <QGroupBox>
#include <QPushButton>

// Ventana de configuración global — independiente de la ventana principal
// Sincronizada con la pestaña "Opciones" de MainWindow
class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget* parent = nullptr);
    void RetranslateUI();

signals:
    void settingsChanged(); // para sincronizar con MainWindow

private slots:
    void OnApply();
    void OnOK();
    void OnCancel();
    void OnLanguageChanged(int index);
    void OnAutoSaveErrorToggled(bool checked);
    void OnBrowseErrorLog();

private:
    void BuildUI();
    void LoadFromConfig();
    void SaveToConfig();

    QTabWidget*   m_tabs           = nullptr;

    // ── Idioma ────────────────────────────────────────────────────────────
    QGroupBox*    m_grpLang        = nullptr;
    QComboBox*    m_cmbLang        = nullptr;

    // ── Arranque ──────────────────────────────────────────────────────────
    QGroupBox*    m_grpStartup     = nullptr;
    QCheckBox*    m_chkAutoStart   = nullptr;
    QCheckBox*    m_chkActivateOnStart = nullptr;

    // ── Ajustes de copia ──────────────────────────────────────────────────
    QGroupBox*    m_grpEndCopy     = nullptr;
    QRadioButton* m_rbNoCloseErr   = nullptr;
    QRadioButton* m_rbNoClose      = nullptr;
    QRadioButton* m_rbClose        = nullptr;

    QGroupBox*    m_grpCollision   = nullptr;
    QComboBox*    m_cmbCollision   = nullptr;

    QGroupBox*    m_grpCopyErr     = nullptr;
    QComboBox*    m_cmbCopyErr     = nullptr;

    // ── Comportamiento ────────────────────────────────────────────────────
    QGroupBox*    m_grpBehavior    = nullptr;
    QComboBox*    m_cmbNewList     = nullptr; // qué hacer con nuevas listas

    // ── Supresión ─────────────────────────────────────────────────────────
    QGroupBox*    m_grpDeletion    = nullptr;
    QRadioButton* m_rbDelAlways    = nullptr;
    QRadioButton* m_rbDelNoError   = nullptr;

    // ── Retitulación ──────────────────────────────────────────────────────
    QGroupBox*    m_grpRename      = nullptr;
    QLineEdit*    m_edtOldPattern  = nullptr;
    QLineEdit*    m_edtNewPattern  = nullptr;

    // ── Informe de errores ────────────────────────────────────────────────
    QGroupBox*    m_grpErrorLog    = nullptr;
    QCheckBox*    m_chkAutoSave    = nullptr;
    QLineEdit*    m_edtLogPath     = nullptr;
    QLineEdit*    m_edtLogName     = nullptr;
    QPushButton*  m_btnBrowseLog   = nullptr;

    // ── Procesos ──────────────────────────────────────────────────────────
    QGroupBox*    m_grpProcess     = nullptr;
    QLineEdit*    m_edtProcess     = nullptr;
    QLabel*       m_lblProcessInfo = nullptr;

    // ── Rendimiento ───────────────────────────────────────────────────────
    QGroupBox*    m_grpPerf        = nullptr;
    QSpinBox*     m_spThreads      = nullptr;
    QSpinBox*     m_spBufferKB     = nullptr;
    QCheckBox*    m_chkNoBuffer    = nullptr;
    QCheckBox*    m_chkOverlap     = nullptr;
    QCheckBox*    m_chkVerify      = nullptr;

    // ── Avanzado ──────────────────────────────────────────────────────────
    QGroupBox*    m_grpAdvanced    = nullptr;
    QComboBox*    m_cmbPriority    = nullptr;

    // ── Botones ───────────────────────────────────────────────────────────
    QPushButton*  m_btnOK          = nullptr;
    QPushButton*  m_btnApply       = nullptr;
    QPushButton*  m_btnCancel      = nullptr;
};
