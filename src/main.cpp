// main.cpp
// Punto de entrada de FileCopier.

#include "../include/Utils.h"
#include "../include/EventBus.h"

#include <QApplication>
#include <QMainWindow>
#include <QSettings>
#include <QStyleFactory>
#include <QString>
#include <shellapi.h>   // CommandLineToArgvW

#include "ui/ProgressPanel.cpp"
#include "ui/ErrorDialog.cpp"
#include "ui/ErrorLog.cpp"
#include "ui/MainWindow.cpp"

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // Obtener argc/argv reales desde la línea de comandos de Windows
    int wargc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);

    // Convertir a char** que QApplication necesita
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

    // ── Qt Application ────────────────────────────────────────────────────
    QApplication app(argc, argPtrs.data());
    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");
    app.setStyle(QStyleFactory::create("Fusion"));

    // ── Configuración ─────────────────────────────────────────────────────
    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini"))
        cfg.Save(L"filecopier.ini");

    // ── Logger ────────────────────────────────────────────────────────────
    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");

    // ── Ventana principal ─────────────────────────────────────────────────
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

// Alias para que Qt pueda llamar wWinMain desde su propio main()
int main(int, char**) {
    return wWinMain(nullptr, nullptr, nullptr, SW_SHOW);
}
