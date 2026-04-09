#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>

namespace FileCopier {

// ── Logger ───────────────────────────────────────────────────────────────────
enum class LogLevel { Debug, Info, Warning, Error, Critical };

class Logger {
public:
    static Logger& Instance();

    void Init(const std::wstring& logFilePath);
    void Log(LogLevel level, const std::wstring& message);

    void Debug   (const std::wstring& msg) { Log(LogLevel::Debug,    msg); }
    void Info    (const std::wstring& msg) { Log(LogLevel::Info,     msg); }
    void Warning (const std::wstring& msg) { Log(LogLevel::Warning,  msg); }
    void Error   (const std::wstring& msg) { Log(LogLevel::Error,    msg); }
    void Critical(const std::wstring& msg) { Log(LogLevel::Critical, msg); }

private:
    Logger() = default;
    std::wofstream  m_file;
    std::mutex      m_mutex;
};

#define LOG_INFO(msg)     FileCopier::Logger::Instance().Info(msg)
#define LOG_WARNING(msg)  FileCopier::Logger::Instance().Warning(msg)
#define LOG_ERROR(msg)    FileCopier::Logger::Instance().Error(msg)

// ── ConfigManager ────────────────────────────────────────────────────────────
struct AppConfig {
    size_t  threadCount    = 8;
    DWORD   bufferSizeKB   = 1024;      // 1 MB
    int     maxRetries     = 3;
    DWORD   retryDelayMs   = 500;
    bool    verifyAfterCopy= false;
    bool    skipOnError    = true;
    bool    useNoBuffering = false;
    bool    useOverlapped  = false;  // false=simple seguro; true=overlapped avanzado
    std::wstring logPath   = L"filecopier.log";
    std::wstring statePath = L"filecopier_state.json";
};

class ConfigManager {
public:
    static ConfigManager& Instance();

    bool Load(const std::wstring& iniPath);
    bool Save(const std::wstring& iniPath) const;

    AppConfig&       Config()       { return m_config; }
    const AppConfig& Config() const { return m_config; }

private:
    ConfigManager() = default;
    AppConfig m_config;
};

// ── ErrorHandler ─────────────────────────────────────────────────────────────
enum class ErrorAction { Retry, Skip, Abort };

class ErrorHandler {
public:
    // Decide qué hacer con un error dado el número de reintentos ya realizados
    static ErrorAction Handle(
        const std::wstring& filePath,
        DWORD               winError,
        int                 retriesDone,
        int                 maxRetries,
        bool                skipOnError
    );

    static std::wstring FormatWinError(DWORD code);
};

} // namespace FileCopier
