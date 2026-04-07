// main.cpp
// Punto de entrada de FileCopier.
// NO incluye otros .cpp — cada unidad de compilación se compila una sola vez.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "../include/Utils.h"
#include "../include/EventBus.h"

#include <QApplication>
#include <QSettings>
#include <QStyleFactory>
#include <QString>
#include <vector>
#include <string>

// Las clases MainWindow, ProgressPanel, etc. se declaran en sus propios .h
// y se compilan en sus propios .cpp — main.cpp solo instancia MainWindow.
#include "../include/ui/MainWindow.h"

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int wargc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);

    std::vector<std::string> argStrs;
    argStrs.reserve(wargc);
    for (int i = 0; i < wargc; ++i) {
        std::wstring ws(wargv[i]);
        argStrs.emplace_back(ws.begin(), ws.end());
    }
    ::LocalFree(wargv);

    std::vector<char*> argPtrs;
    argPtrs.reserve(argStrs.size());
    for (auto& s : argStrs) argPtrs.push_back(s.data());
    int argc = static_cast<int>(argPtrs.size());

    QApplication app(argc, argPtrs.data());
    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");
    app.setStyle(QStyleFactory::create("Fusion"));

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
        window.resize(900, 650);

    window.show();
    int ret = app.exec();

    settings.setValue("geometry", window.saveGeometry());
    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();
    return ret;
}
