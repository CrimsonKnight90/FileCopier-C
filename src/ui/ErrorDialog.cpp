// ErrorDialog.cpp
// Diálogo modal que se muestra cuando ocurre un error durante la copia.
// El usuario puede elegir: Retry, Skip, Skip All o Abort.
//
// IMPORTANTE: cada .cpp puede tener UNA SOLA clase Q_OBJECT cuando se usa
// #include "Foo.moc". ErrorLog ha sido movido a ErrorLog.cpp separado.

#include "../../include/EventBus.h"
#include "../../include/Utils.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QString>
#include <QTextStream>
#include <QVBoxLayout>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// ErrorDialog
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
        , m_action(Action::Skip)
    {
        setWindowTitle("Copy Error");
        setMinimumWidth(480);
        BuildUI(filePath, errorMessage, errorCode);
    }

    Action ChosenAction() const { return m_action; }

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
    Action m_action;

    void BuildUI(const QString& filePath, const QString& errorMessage, DWORD errorCode)
    {
        auto* vMain = new QVBoxLayout(this);
        vMain->setSpacing(12);

        // Icono + texto
        auto* topRow    = new QHBoxLayout;
        auto* iconLabel = new QLabel;
        iconLabel->setPixmap(
            QApplication::style()
                ->standardIcon(QStyle::SP_MessageBoxWarning)
                .pixmap(48, 48));
        topRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto* msgLayout = new QVBoxLayout;
        msgLayout->addWidget(new QLabel("<b>An error occurred while copying a file.</b>"));

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

        // Separador
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        vMain->addWidget(sep);

        // Botones
        auto* btnLayout  = new QHBoxLayout;
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

        auto* helpLbl = new QLabel(
            "<small>"
            "<b>Retry</b>: try again  &middot;  "
            "<b>Skip</b>: ignore this file  &middot;  "
            "<b>Skip All</b>: ignore all future errors  &middot;  "
            "<b>Abort</b>: stop the entire operation"
            "</small>");
        helpLbl->setWordWrap(true);
        helpLbl->setStyleSheet("color: gray;");
        vMain->addWidget(helpLbl);
    }
};

#include "ErrorDialog.moc"
