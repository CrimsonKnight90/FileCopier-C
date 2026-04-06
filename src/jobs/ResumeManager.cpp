#include "../../include/Jobs.h"
#include "../../include/Utils.h"
#include <fstream>
#include <sstream>

namespace FileCopier {

ResumeManager::ResumeManager(const std::wstring& stateFile)
    : m_stateFile(stateFile)
{}

// ─────────────────────────────────────────────────────────────────────────────
// Formato JSON mínimo (sin dependencias externas):
// [{"src":"...","dst":"...","done":12345}, ...]
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring EscapeJson(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t c : s) {
        if      (c == L'\\') out += L"\\\\";
        else if (c == L'"')  out += L"\\\"";
        else                 out += c;
    }
    return out;
}

void ResumeManager::Save(const std::vector<Job>& jobs) {
    std::wofstream f(m_stateFile);
    if (!f) {
        LOG_WARNING(L"ResumeManager: cannot write state file");
        return;
    }

    f << L"[\n";
    bool first = true;
    for (const auto& j : jobs) {
        // Solo persiste los que no terminaron bien
        if (j.status == JobStatus::Completed) continue;

        if (!first) f << L",\n";
        first = false;

        f << L"  {\"src\":\"" << EscapeJson(j.file.srcPath)
          << L"\",\"dst\":\""  << EscapeJson(j.file.dstPath)
          << L"\",\"done\":"   << j.bytesDone
          << L"}";
    }
    f << L"\n]\n";
    LOG_INFO(L"ResumeManager: state saved → " + m_stateFile);
}

bool ResumeManager::Load(std::vector<ResumeEntry>& entries) {
    entries.clear();
    std::wifstream f(m_stateFile);
    if (!f) return false;

    std::wstringstream ss;
    ss << f.rdbuf();
    std::wstring content = ss.str();

    // Parser JSON mínimo: busca objetos { ... }
    size_t pos = 0;
    while ((pos = content.find(L'{', pos)) != std::wstring::npos) {
        size_t end = content.find(L'}', pos);
        if (end == std::wstring::npos) break;

        std::wstring obj = content.substr(pos + 1, end - pos - 1);
        pos = end + 1;

        auto extractStr = [&](const std::wstring& key) -> std::wstring {
            std::wstring needle = L"\"" + key + L"\":\"";
            size_t p = obj.find(needle);
            if (p == std::wstring::npos) return {};
            p += needle.size();
            size_t q = obj.find(L'"', p);
            if (q == std::wstring::npos) return {};
            return obj.substr(p, q - p);
        };
        auto extractNum = [&](const std::wstring& key) -> LONGLONG {
            std::wstring needle = L"\"" + key + L"\":";
            size_t p = obj.find(needle);
            if (p == std::wstring::npos) return 0;
            p += needle.size();
            return std::stoll(obj.substr(p));
        };

        ResumeEntry e;
        e.srcPath   = extractStr(L"src");
        e.dstPath   = extractStr(L"dst");
        e.bytesDone = extractNum(L"done");
        if (!e.srcPath.empty())
            entries.push_back(std::move(e));
    }

    LOG_INFO(L"ResumeManager: loaded " + std::to_wstring(entries.size()) + L" entries");
    return !entries.empty();
}

void ResumeManager::Clear() {
    ::DeleteFileW(m_stateFile.c_str());
}

} // namespace FileCopier
