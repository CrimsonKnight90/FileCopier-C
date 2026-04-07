#include "../../include/ui/ErrorLog.h"
#include "../../include/EventBus.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPushButton>
#include <QTextStream>
#include <QTime>
#include <QVBoxLayout>

using namespace FileCopier;

ErrorLog::ErrorLog(QWidget* parent) : QWidget(parent) {
    BuildUI();
    ConnectEventBus();
}

void ErrorLog::Clear() {
    m_list->clear();
    m_countLabel->setText("No errors");
    m_errorCount = 0;
}

void ErrorLog::OnUIError(const QString& path, const QString& msg, int /*code*/) {
    ++m_errorCount;
    m_list->addItem(QString("[%1] %2  %3  %4")
        .arg(m_errorCount)
        .arg(QTime::currentTime().toString("HH:mm:ss"))
        .arg(path).arg(msg));
    m_list->scrollToBottom();
    m_countLabel->setText(
        QString("<b style='color:red'>%1 error(s)</b>").arg(m_errorCount));
}

void ErrorLog::ExportLog() {
    QString savePath = QFileDialog::getSaveFileName(
        this, "Export error log", "filecopier_errors.txt", "Text files (*.txt)");
    if (savePath.isEmpty()) return;
    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (int i = 0; i < m_list->count(); ++i)
        ts << m_list->item(i)->text() << "\n";
}

void ErrorLog::BuildUI() {
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

    auto* exportBtn = new QPushButton("Export log...");
    connect(exportBtn, &QPushButton::clicked, this, &ErrorLog::ExportLog);
    vMain->addWidget(exportBtn);
}

void ErrorLog::ConnectEventBus() {
    EventBus::Instance().OnError([this](const EventError& e) {
        QMetaObject::invokeMethod(this, "OnUIError", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdWString(e.path)),
            Q_ARG(QString, QString::fromStdWString(e.message)),
            Q_ARG(int,     static_cast<int>(e.code)));
    });
}
