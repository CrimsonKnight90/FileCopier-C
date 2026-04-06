// ErrorDialog.cpp
// Diálogo modal que se muestra cuando ocurre un error durante la copia.
// El usuario puede elegir: Retry (reintentar), Skip (saltar archivo) o Abort (cancelar todo).
// También incluye un log acumulativo de todos los errores de la sesión.

#include "../../include/EventBus.h"
#include "../../include/Utils.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QIcon>
#include <QStyle>
#include <QApplication>
#include <QMetaObject>
#include <QString>
#include <functional>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// ErrorDialog: diálogo bloqueante que pregunta qué hacer con un error
// ─────────────────────────────────────────────────────────────────────────────
class ErrorDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { Retry, Skip, SkipAll, Abort };

    explicit ErrorDialog(
        const QString& filePath,
        const QString& errorMessage,
        DWORD          errorCode,
        QWidget*       parent = nullptr)
        : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
    {
        setWindowTitle("Copy Error");
        setMinimumWidth(480);
        BuildUI(filePath, errorMessage, errorCode);
    }

    Action ChosenAction() const { return m_action; }

    // Muestra el diálogo y devuelve la acción elegida de forma estática
    static Action Ask(
        const QString& filePath,
        const QString& errorMessage,
        DWORD          errorCode,
        QWidget*       parent = nullptr)
    {
        ErrorDialog dlg(filePath, errorMessage, errorCode, parent);
        dlg.exec();
        return dlg.ChosenAction();
    }

private slots:
    void OnRetry()   { m_action = Action::Retry;   accept(); }
    void OnSkip()    { m_action = Action::Skip;    accept(); }
    void OnSkipAll() { m_action = Action::SkipAll; accept(); }
    void OnAbort()   { m_action = Action::Abort;   reject(); }

private:
    void BuildUI(const QString& filePath, const QString& errorMessage, DWORD errorCode) {
        auto* vMain = new QVBoxLayout(this);
        vMain->setSpacing(12);

        // ── Icono + mensaje ───────────────────────────────────────────────
        auto* topRow = new QHBoxLayout;

        auto* iconLabel = new QLabel;
        iconLabel->setPixmap(
            QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning)
                .pixmap(48, 48));
        topRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto* msgLayout = new QVBoxLayout;
        auto* titleLbl  = new QLabel("<b>An error occurred while copying a file.</b>");
        msgLayout->addWidget(titleLbl);

        auto* fileLbl = new QLabel(QString("<b>File:</b> %1").arg(filePath));
        fileLbl->setWordWrap(true);
        fileLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        msgLayout->addWidget(fileLbl);

        auto* errLbl = new QLabel(
            QString("<b>Error [%1]:</b> %2").arg(errorCode).arg(errorMessage));
        errLbl->setWordWrap(true);
        errLbl->setStyleSheet("color: #c0392b;");
        errLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        msgLayout->addWidget(errLbl);

        topRow->addLayout(msgLayout, 1);
        vMain->addLayout(topRow);

        // ── Separador ────────────────────────────────────────────────────
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        vMain->addWidget(sep);

        // ── Botones de acción ────────────────────────────────────────────
        auto* btnLayout = new QHBoxLayout;
        btnLayout->setSpacing(8);

        auto* retryBtn   = new QPushButton("Retry");
        auto* skipBtn    = new QPushButton("Skip");
        auto* skipAllBtn = new QPushButton("Skip All");
        auto* abortBtn   = new QPushButton("Abort");

        retryBtn->setDefault(true);
        abortBtn->setStyleSheet("color: #c0392b; font-weight: bold;");

        btnLayout->addStretch();
        btnLayout->addWidget(retryBtn);
        btnLayout->addWidget(skipBtn);
        btnLayout->addWidget(skipAllBtn);
        btnLayout->addWidget(abortBtn);

        connect(retryBtn,   &QPushButton::clicked, this, &ErrorDialog::OnRetry);
        connect(skipBtn,    &QPushButton::clicked, this, &ErrorDialog::OnSkip);
        connect(skipAllBtn, &QPushButton::clicked, this, &ErrorDialog::OnSkipAll);
        connect(abortBtn,   &QPushButton::clicked, this, &ErrorDialog::OnAbort);

        vMain->addLayout(btnLayout);

        // ── Descripción de acciones ───────────────────────────────────────
        auto* helpLbl = new QLabel(
            "<small>"
            "<b>Retry</b>: try again  ·  "
            "<b>Skip</b>: ignore this file  ·  "
            "<b>Skip All</b>: ignore all future errors  ·  "
            "<b>Abort</b>: stop the entire operation"
            "</small>");
        helpLbl->setWordWrap(true);
        helpLbl->setStyleSheet("color: gray;");
        vMain->addWidget(helpLbl);
    }

    Action m_action = Action::Skip; // Default: saltar si el usuario cierra
};

// ─────────────────────────────────────────────────────────────────────────────
// ErrorLog: widget no modal que acumula todos los errores de la sesión
// ─────────────────────────────────────────────────────────────────────────────
class ErrorLog : public QWidget {
    Q_OBJECT
public:
    explicit ErrorLog(QWidget* parent = nullptr) : QWidget(parent) {
        BuildUI();
        ConnectEventBus();
    }

    void Clear() {
        m_list->clear();
        m_countLabel->setText("No errors");
        m_errorCount = 0;
    }

    int ErrorCount() const { return m_errorCount; }

public slots:
    void OnUIError(const QString& path, const QString& msg, int code) {
        ++m_errorCount;
        m_list->addItem(
            QString("[%1] %2 — %3 (code %4)")
                .arg(m_errorCount)
                .arg(QTime::currentTime().toString("HH:mm:ss"))
                .arg(path)
                .arg(msg)
        );
        m_list->scrollToBottom();
        m_countLabel->setText(
            QString("<b style='color:red'>%1 error(s)</b>").arg(m_errorCount));
    }

private:
    void BuildUI() {
        auto* vMain = new QVBoxLayout(this);
        vMain->setContentsMargins(0, 0, 0, 0);

        auto* header = new QHBoxLayout;
        header->addWidget(new QLabel("<b>Error log</b>"));
        header->addStretch();
        m_countLabel = new QLabel("No errors");
        header->addWidget(m_countLabel);
        auto* clearBtn = new QPushButton("Clear");
        clearBtn->setFixedWidth(60);
        connect(clearBtn, &QPushButton::clicked, this, &ErrorLog::Clear);
        header->addWidget(clearBtn);
        vMain->addLayout(header);

        m_list = new QListWidget;
        m_list->setAlternatingRowColors(true);
        m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        vMain->addWidget(m_list);

        // Botón exportar log
        auto* exportBtn = new QPushButton("Export log…");
        connect(exportBtn, &QPushButton::clicked, this, &ErrorLog::ExportLog);
        vMain->addWidget(exportBtn);
    }

    void ConnectEventBus() {
        EventBus::Instance().OnError([this](const EventError& e) {
            QMetaObject::invokeMethod(this, "OnUIError", Qt::QueuedConnection,
                Q_ARG(QString, QString::fromStdWString(e.path)),
                Q_ARG(QString, QString::fromStdWString(e.message)),
                Q_ARG(int,     static_cast<int>(e.code)));
        });
    }

private slots:
    void ExportLog() {
        auto path = QFileDialog::getSaveFileName(
            this, "Export error log", "filecopier_errors.txt",
            "Text files (*.txt)");
        if (path.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream ts(&f);
        for (int i = 0; i < m_list->count(); ++i)
            ts << m_list->item(i)->text() << "\n";
    }

    QListWidget* m_list       = nullptr;
    QLabel*      m_countLabel = nullptr;
    int          m_errorCount = 0;
};

#include "ErrorDialog.moc"
