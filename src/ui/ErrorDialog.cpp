#include "../../include/ui/ErrorDialog.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

ErrorDialog::ErrorDialog(const QString& filePath,
                         const QString& errorMessage,
                         DWORD errorCode,
                         QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
    , m_action(Action::Skip)
{
    setWindowTitle("Copy Error");
    setMinimumWidth(480);
    BuildUI(filePath, errorMessage, errorCode);
}

ErrorDialog::Action ErrorDialog::Ask(const QString& filePath,
                                     const QString& errorMessage,
                                     DWORD errorCode,
                                     QWidget* parent)
{
    ErrorDialog dlg(filePath, errorMessage, errorCode, parent);
    dlg.exec();
    return dlg.ChosenAction();
}

void ErrorDialog::OnRetry()   { m_action = Action::Retry;   accept(); }
void ErrorDialog::OnSkip()    { m_action = Action::Skip;    accept(); }
void ErrorDialog::OnSkipAll() { m_action = Action::SkipAll; accept(); }
void ErrorDialog::OnAbort()   { m_action = Action::Abort;   reject(); }

void ErrorDialog::BuildUI(const QString& filePath,
                          const QString& errorMessage,
                          DWORD errorCode)
{
    auto* vMain = new QVBoxLayout(this);
    vMain->setSpacing(12);

    auto* topRow    = new QHBoxLayout;
    auto* iconLabel = new QLabel;
    iconLabel->setPixmap(
        QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(48, 48));
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

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vMain->addWidget(sep);

    auto* btnLayout  = new QHBoxLayout;
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
        "<small><b>Retry</b>: try again  &middot;  "
        "<b>Skip</b>: ignore this file  &middot;  "
        "<b>Skip All</b>: ignore all future errors  &middot;  "
        "<b>Abort</b>: stop the entire operation</small>");
    helpLbl->setWordWrap(true);
    helpLbl->setStyleSheet("color: gray;");
    vMain->addWidget(helpLbl);
}
