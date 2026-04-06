#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace FileCopier {

struct FileEntry {
    std::wstring srcPath;
    std::wstring dstPath;
    LONGLONG     size        = 0;
    FILETIME     lastWrite   = {};
    bool         isDirectory = false;
};

using ScanProgressCallback = std::function<void(size_t filesFound)>;

class DirectoryScanner {
public:
    DirectoryScanner() = default;

    // Escanea srcRoot de forma recursiva y rellena entries
    bool Scan(
        const std::wstring& srcRoot,
        const std::wstring& dstRoot,
        std::vector<FileEntry>& entries,
        ScanProgressCallback onProgress = nullptr
    );

    void Cancel() { m_cancelled = true; }

    // Tamaño total de todos los archivos escaneados
    LONGLONG TotalBytes() const { return m_totalBytes.load(); }

private:
    void ScanDirectory(
        const std::wstring& srcDir,
        const std::wstring& dstDir,
        std::vector<FileEntry>& entries,
        ScanProgressCallback& onProgress
    );

    std::atomic<bool>     m_cancelled {false};
    std::atomic<LONGLONG> m_totalBytes{0};
};

} // namespace FileCopier
