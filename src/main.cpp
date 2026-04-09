#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../include/Utils.h"
#include "../include/EventBus.h"
#include "../include/Language.h"
#include "../include/ui/MainWindow.h"
#include "../include/ui/TrayIcon.h"

#include <QApplication>
#include <QSettings>
#include <QStyleFactory>

using namespace FileCopier;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");
    app.setStyle(QStyleFactory::create("Fusion"));

    // No cerrar la app cuando se cierra la ventana principal (vive en bandeja)
    app.setQuitOnLastWindowClosed(false);

    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini")) cfg.Save(L"filecopier.ini");

    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");

    Language::Instance().SetLanguage(Lang::ES);

    MainWindow window;
    TrayIcon   tray(&window);

    // El manejo de minimizar a bandeja se hace en MainWindow::changeEvent

    window.show();
    int ret = app.exec();

    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();
    return ret;
}
