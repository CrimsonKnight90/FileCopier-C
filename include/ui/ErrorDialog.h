#pragma once
#include <QDialog>
#include <QString>
#include <windows.h>

class ErrorDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { Retry, Skip, SkipAll, Abort };

    explicit ErrorDialog(const QString& filePath,
                         const QString& errorMessage,
                         DWORD errorCode,
                         QWidget* parent = nullptr);

    Action ChosenAction() const { return m_action; }

    static Action Ask(const QString& filePath,
                      const QString& errorMessage,
                      DWORD errorCode,
                      QWidget* parent = nullptr);

private slots:
    void OnRetry();
    void OnSkip();
    void OnSkipAll();
    void OnAbort();

private:
    void BuildUI(const QString& filePath, const QString& errorMessage, DWORD errorCode);
    Action m_action = Action::Skip;
};
