#include "../../include/Utils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace FileCopier {

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Logger::Init(const std::wstring& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file.open(logFilePath.c_str(), std::ios::app);
    if (!m_file)
        std::wcerr << L"[Logger] Cannot open log file: " << logFilePath << L"\n";
}

void Logger::Log(LogLevel level, const std::wstring& message) {
    // Timestamp
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::wostringstream line;
    line << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S") << L" ";

    switch (level) {
        case LogLevel::Debug:    line << L"[DBG] "; break;
        case LogLevel::Info:     line << L"[INF] "; break;
        case LogLevel::Warning:  line << L"[WRN] "; break;
        case LogLevel::Error:    line << L"[ERR] "; break;
        case LogLevel::Critical: line << L"[CRT] "; break;
    }

    line << message << L"\n";

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) m_file << line.str() << std::flush;

    // En debug también a consola
#ifdef _DEBUG
    std::wcout << line.str();
#endif
}

} // namespace FileCopier
