#include "../../include/ui/TrayIcon.h"
#include "../../include/ui/MainWindow.h"
#include "../../include/ui/ConfigDialog.h"
#include "../../include/Language.h"
#include <QApplication>
#include <QStyle>

using namespace FileCopier;

TrayIcon::TrayIcon(MainWindow* window, QObject* parent)
    : QSystemTrayIcon(parent), m_window(window)
{
    // Usar icono de la aplicación o uno de sistema
    setIcon(QApplication::style()->standardIcon(
        QStyle::SP_FileDialogStart));
    setToolTip("FileCopier");

    BuildMenu();

    connect(this, &QSystemTrayIcon::activated,
            this, &TrayIcon::OnActivated);

    show();
}

void TrayIcon::BuildMenu() {
    m_menu = new QMenu();

    // Nueva tarea
    auto* menuNew = m_menu->addMenu(TR("tray.newTask"));
    m_actNewCopy = menuNew->addAction(TR("tray.newCopy"),  this, &TrayIcon::OnNewCopy);
    menuNew->addAction(TR("tray.newMove"), this, &TrayIcon::OnNewCopy); // mismo diálogo, modo diferente

    m_menu->addSeparator();
    m_actConfig = m_menu->addAction(TR("tray.config"), this, &TrayIcon::OnShowConfig);
    m_menu->addSeparator();
    m_actQuit = m_menu->addAction(TR("tray.quit"), this, &TrayIcon::OnQuit);

    setContextMenu(m_menu);
}

void TrayIcon::UpdateCopyingState(bool copying) {
    setToolTip(copying ? "FileCopier — " + TR("lbl.copying") : "FileCopier");
}

void TrayIcon::OnActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick ||
        reason == QSystemTrayIcon::Trigger) {
        if (m_window->isVisible()) {
            m_window->raise();
            m_window->activateWindow();
        } else {
            m_window->show();
            m_window->raise();
        }
    }
}

void TrayIcon::OnNewCopy() {
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void TrayIcon::OnShowConfig() {
    // Abrir ventana de configuración global (separada de la ventana principal)
    auto* dlg = new ConfigDialog(m_window);
    connect(dlg, &ConfigDialog::settingsChanged, m_window, [this](){
        // Si la ventana principal está visible, sincronizarla
        if (m_window->isVisible()) m_window->SyncFromConfig();
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void TrayIcon::OnQuit() {
    QApplication::quit();
}
