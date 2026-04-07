<FILE filename="src/main.cpp" size="2487 bytes">
// main.cpp - Versión corregida para Qt 6 + MinGW + WIN32
// NO define wWinMain → Qt lo maneja automáticamente

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "../include/Utils.h"
#include "../include/EventBus.h"
#include "../include/ui/MainWindow.h"

#include <QApplication>
#include <QSettings>
#include <QStyleFactory>
#include <QString>
#include <vector>

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    // Convertimos argumentos Windows a estilo Qt (compatible con MinGW)
    int wargc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);

    std::vector<std::string> argStrs;
    argStrs.reserve(wargc);
    for (int i = 0; i < wargc; ++i) {
        std::wstring ws(wargv[i]);
        argStrs.emplace_back(ws.begin(), ws.end());
    }
    ::LocalFree(wargv);

    std::vector<char*> argPtrs(argStrs.size());
    for (size_t i = 0; i < argStrs.size(); ++i)
        argPtrs[i] = argStrs[i].data();

    QApplication app(argc, argPtrs.data());
    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");
    app.setStyle(QStyleFactory::create("Fusion"));

    // Cargar configuración
    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini"))
        cfg.Save(L"filecopier.ini");

    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");

    MainWindow window;

    QSettings settings("FileCopierDev", "FileCopier");
    if (settings.contains("geometry"))
        window.restoreGeometry(settings.value("geometry").toByteArray());
    else
        window.resize(1100, 720);

    window.show();

    int ret = app.exec();

    settings.setValue("geometry", window.saveGeometry());
    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();

    return ret;
}
</FILE>