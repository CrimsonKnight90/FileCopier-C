// main.cpp
// Punto de entrada de FileCopier.
// Inicializa Qt, carga configuración, arranca el logger y muestra la ventana principal.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../include/Utils.h"
#include "../include/EventBus.h"

// Qt
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QLabel>
#include <QProgressBar>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QAction>
#include <QSettings>
#include <QString>
#include <QThread>
#include <QMetaObject>
#include <QDir>
#include <QStyleFactory>

// Incluir aquí las clases definidas en los .cpp de UI
// (En un proyecto real cada clase estaría en su propio .h/.cpp con MOC normal;
//  aquí los incluimos directamente para mantener la estructura de un solo .cpp por archivo.)
#include "ui/ProgressPanel.cpp"
#include "ui/ErrorDialog.cpp"
#include "ui/MainWindow.cpp"

using namespace FileCopier;

// ─────────────────────────────────────────────────────────────────────────────
// Punto de entrada para aplicación Windows con o sin consola
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int nShowCmd)
{
    // Qt necesita argc/argv
    int    argc = 0;
    char** argv = nullptr;
    // Convertir línea de comandos real
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    // Convertir a char** para QApplication (simplificado; en prod usar QCoreApplication)
    std::vector<std::string> argStrs;
    std::vector<char*>       argPtrs;
    for (int i = 0; i < argc; ++i) {
        std::wstring ws(wargv[i]);
        argStrs.emplace_back(ws.begin(), ws.end());
    }
    for (auto& s : argStrs) argPtrs.push_back(s.data());
    ::LocalFree(wargv);

    // ── Qt Application ────────────────────────────────────────────────────
    QApplication app(argc, argPtrs.data());
    app.setApplicationName("FileCopier");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FileCopierDev");

    // Estilo moderno (Fusion disponible en todas las plataformas con Qt)
    app.setStyle(QStyleFactory::create("Fusion"));

    // Paleta oscura opcional (comentar para paleta clara)
    /*
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(45, 45, 45));
    dark.setColor(QPalette::WindowText,      Qt::white);
    dark.setColor(QPalette::Base,            QColor(30, 30, 30));
    dark.setColor(QPalette::AlternateBase,   QColor(55, 55, 55));
    dark.setColor(QPalette::ToolTipBase,     Qt::white);
    dark.setColor(QPalette::ToolTipText,     Qt::white);
    dark.setColor(QPalette::Text,            Qt::white);
    dark.setColor(QPalette::Button,          QColor(55, 55, 55));
    dark.setColor(QPalette::ButtonText,      Qt::white);
    dark.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(dark);
    */

    // ── Configuración ─────────────────────────────────────────────────────
    auto& cfg = ConfigManager::Instance();
    if (!cfg.Load(L"filecopier.ini")) {
        // Primera ejecución: guardar defaults
        cfg.Save(L"filecopier.ini");
    }

    // ── Logger ────────────────────────────────────────────────────────────
    Logger::Instance().Init(cfg.Config().logPath);
    LOG_INFO(L"=== FileCopier started ===");
    LOG_INFO(L"Threads: " + std::to_wstring(cfg.Config().threadCount));
    LOG_INFO(L"BufferSize: " + std::to_wstring(cfg.Config().bufferSizeKB) + L" KB");

    // ── Ventana principal ─────────────────────────────────────────────────
    MainWindow window;

    // Restaurar geometría guardada
    QSettings settings("FileCopierDev", "FileCopier");
    if (settings.contains("geometry"))
        window.restoreGeometry(settings.value("geometry").toByteArray());
    else
        window.resize(900, 650);

    window.show();

    int ret = app.exec();

    // Guardar geometría al salir
    settings.setValue("geometry", window.saveGeometry());

    LOG_INFO(L"=== FileCopier exited ===");
    EventBus::Instance().ClearAll();

    return ret;
}

// Alias para compilar en modo consola también
int main(int argc, char* argv[]) {
    return wWinMain(nullptr, nullptr, nullptr, SW_SHOW);
}
