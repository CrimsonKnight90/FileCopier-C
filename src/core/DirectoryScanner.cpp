#include "../../include/DirectoryScanner.h"
#include "../../include/Utils.h"
#include <shlwapi.h>

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
bool DirectoryScanner::Scan(
    const std::wstring& srcRoot,
    const std::wstring& dstRoot,
    std::vector<FileEntry>& entries,
    ScanProgressCallback onProgress)
{
    m_cancelled  = false;
    m_totalBytes = 0;
    entries.clear();

    if (!::PathFileExistsW(srcRoot.c_str())) {
        LOG_ERROR(L"Source directory not found: " + srcRoot);
        return false;
    }

    ScanDirectory(srcRoot, dstRoot, entries, onProgress);
    return !m_cancelled;
}

// ─────────────────────────────────────────────────────────────────────────────
void DirectoryScanner::ScanDirectory(
    const std::wstring& srcDir,
    const std::wstring& dstDir,
    std::vector<FileEntry>& entries,
    ScanProgressCallback& onProgress)
{
    if (m_cancelled) return;

    // Remover prefijo \\?\ si existe para evitar duplicación
    auto cleanPath = [](const std::wstring& p) -> std::wstring {
        if (p.size() >= 4 && p.substr(0, 4) == L"\\\\?\\") {
            return p.substr(4);
        }
        return p;
    };

    std::wstring cleanSrc = cleanPath(srcDir);
    std::wstring cleanDst = cleanPath(dstDir);

    WIN32_FIND_DATAW fd{};
    std::wstring pattern = L"\\\\?\\" + cleanSrc + L"\\*";
    HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARNING(L"Cannot open directory: " + cleanSrc);
        return;
    }

    do {
        if (m_cancelled) break;

        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring srcPath = cleanSrc + L"\\" + name;
        std::wstring dstPath = cleanDst + L"\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Registrar el propio directorio (para crearlo en destino)
            FileEntry dir;
            dir.srcPath     = srcPath;
            dir.dstPath     = dstPath;
            dir.isDirectory = true;
            dir.lastWrite   = fd.ftLastWriteTime;
            entries.push_back(dir);

            // Recursión
            ScanDirectory(srcPath, dstPath, entries, onProgress);
        } else {
            FileEntry f;
            f.srcPath    = srcPath;
            f.dstPath    = dstPath;
            f.isDirectory= false;
            f.lastWrite  = fd.ftLastWriteTime;

            LARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = static_cast<LONG>(fd.nFileSizeHigh);
            f.size      = sz.QuadPart;

            m_totalBytes += f.size;
            entries.push_back(f);

            if (onProgress) onProgress(entries.size());
        }

    } while (::FindNextFileW(hFind, &fd));

    ::FindClose(hFind);
}

} // namespace FileCopier
