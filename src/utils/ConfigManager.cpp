#include "../../include/Utils.h"
#include <fstream>
#include <sstream>
#include <map>

namespace FileCopier {

ConfigManager& ConfigManager::Instance() {
    static ConfigManager inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parsea un archivo INI estilo:
//   threadCount=16
//   bufferSizeKB=1024
// ─────────────────────────────────────────────────────────────────────────────
bool ConfigManager::Load(const std::wstring& iniPath) {
    std::wifstream f(iniPath.c_str());
    if (!f) return false;

    std::map<std::wstring, std::wstring> kv;
    std::wstring line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        kv[key] = val;
    }

    auto getInt = [&](const std::wstring& k, auto def) {
        auto it = kv.find(k);
        if (it == kv.end()) return def;
        try { return static_cast<decltype(def)>(std::stoll(it->second)); }
        catch (...) { return def; }
    };
    auto getBool = [&](const std::wstring& k, bool def) {
        auto it = kv.find(k);
        if (it == kv.end()) return def;
        return it->second == L"1" || it->second == L"true";
    };
    auto getStr = [&](const std::wstring& k, const std::wstring& def) {
        auto it = kv.find(k);
        return (it == kv.end()) ? def : it->second;
    };

    m_config.threadCount     = getInt (L"threadCount",     m_config.threadCount);
    m_config.bufferSizeKB    = getInt (L"bufferSizeKB",    m_config.bufferSizeKB);
    m_config.maxRetries      = getInt (L"maxRetries",      m_config.maxRetries);
    m_config.retryDelayMs    = getInt (L"retryDelayMs",    m_config.retryDelayMs);
    m_config.verifyAfterCopy = getBool(L"verifyAfterCopy", m_config.verifyAfterCopy);
    m_config.skipOnError     = getBool(L"skipOnError",     m_config.skipOnError);
    m_config.useNoBuffering  = getBool(L"useNoBuffering",  m_config.useNoBuffering);
    m_config.useOverlapped   = getBool(L"useOverlapped",   m_config.useOverlapped);
    m_config.logPath         = getStr (L"logPath",         m_config.logPath);
    m_config.statePath       = getStr (L"statePath",       m_config.statePath);

    return true;
}

bool ConfigManager::Save(const std::wstring& iniPath) const {
    std::wofstream f(iniPath.c_str());
    if (!f) return false;

    f << L"; FileCopier configuration\n";
    f << L"threadCount="     << m_config.threadCount     << L"\n";
    f << L"bufferSizeKB="    << m_config.bufferSizeKB    << L"\n";
    f << L"maxRetries="      << m_config.maxRetries      << L"\n";
    f << L"retryDelayMs="    << m_config.retryDelayMs    << L"\n";
    f << L"verifyAfterCopy=" << (m_config.verifyAfterCopy ? 1 : 0) << L"\n";
    f << L"skipOnError="     << (m_config.skipOnError    ? 1 : 0) << L"\n";
    f << L"useNoBuffering="  << (m_config.useNoBuffering ? 1 : 0) << L"\n";
    f << L"useOverlapped="   << (m_config.useOverlapped  ? 1 : 0) << L"\n";
    f << L"logPath="         << m_config.logPath         << L"\n";
    f << L"statePath="       << m_config.statePath       << L"\n";

    return true;
}

} // namespace FileCopier
