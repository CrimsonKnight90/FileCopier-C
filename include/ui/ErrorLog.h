#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>

class ErrorLog : public QWidget {
    Q_OBJECT
public:
    explicit ErrorLog(QWidget* parent = nullptr);
    int ErrorCount() const { return m_errorCount; }

public slots:
    void Clear();
    void OnUIError(const QString& path, const QString& msg, int code);

private slots:
    void ExportLog();

private:
    void BuildUI();
    void ConnectEventBus();

    QListWidget* m_list        = nullptr;
    QLabel*      m_countLabel  = nullptr;
    int          m_errorCount  = 0;
};
