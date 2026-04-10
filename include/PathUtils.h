#pragma once
#include <QString>
#include <string>

namespace FileCopier {

// Convierte path de Qt (con /) a Windows (con \)
inline std::wstring ToWinPath(const QString& path) {
    QString p = QDir::toNativeSeparators(path);
    return p.toStdWString();
}

inline std::wstring ToWinPath(const std::wstring& path) {
    std::wstring p = path;
    for (auto& c : p) if (c == L'/') c = L'\\';
    return p;
}

} // namespace FileCopier
