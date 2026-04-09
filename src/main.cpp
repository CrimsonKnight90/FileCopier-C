// main.cpp
// Qt con WIN32 subsystem maneja wWinMain internamente via libQt6EntryPoint.
// Solo necesitamos definir main() normal — Qt lo envuelve en su propio wWinMain.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "../include/Utils.h"
#include "../include/EventBus.h"
#include "../include/Language.h"
#include "../include/ui/MainWindow.h"

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

    // Configuración
    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini"))
        cfg.Save(L"filecopier.ini");

    // Logger
    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");

    // Idioma
    Language::Instance().SetLanguage(Lang::ES);

    // Ventana principal
    MainWindow window;

    QSettings settings("FileCopierDev", "FileCopier");
    if (settings.contains("geometry"))
        window.restoreGeometry(settings.value("geometry").toByteArray());
    else
        window.resize(900, 580);

    window.show();
    int ret = app.exec();

    settings.setValue("geometry", window.saveGeometry());
    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();
    return ret;
}
