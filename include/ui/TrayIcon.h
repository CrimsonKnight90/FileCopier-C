#pragma once
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

class MainWindow;

class TrayIcon : public QSystemTrayIcon {
    Q_OBJECT
public:
    explicit TrayIcon(MainWindow* window, QObject* parent = nullptr);
    void UpdateCopyingState(bool copying);

private slots:
    void OnActivated(QSystemTrayIcon::ActivationReason reason);
    void OnNewCopy();
    void OnShowConfig();
    void OnQuit();

private:
    void BuildMenu();
    MainWindow* m_window;
    QMenu*      m_menu        = nullptr;
    QAction*    m_actNewCopy  = nullptr;
    QAction*    m_actConfig   = nullptr;
    QAction*    m_actQuit     = nullptr;
};
