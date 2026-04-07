#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include "../include/Utils.h"
#include "../include/EventBus.h"
#include "../include/ui/MainWindow.h"

#include <QApplication>
#include <QSettings>
#include <QStyleFactory>

using namespace FileCopier;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");
    app.setStyle(QStyleFactory::create("Fusion"));

    // ── Configuración ─────────────────────────────
    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini"))
        cfg.Save(L"filecopier.ini");

    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");

    // ── UI ───────────────────────────────────────
    MainWindow window;

    QSettings settings("FileCopierDev", "FileCopier");

    if (settings.contains("geometry"))
        window.restoreGeometry(settings.value("geometry").toByteArray());
    else
        window.resize(1100, 720);

    window.show();

    // ── Loop principal ───────────────────────────
    int ret = app.exec();

    // ── Persistencia ─────────────────────────────
    settings.setValue("geometry", window.saveGeometry());

    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();

    return ret;
}