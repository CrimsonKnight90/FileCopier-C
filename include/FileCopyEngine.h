#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>

namespace FileCopier {

// Callbacks de progreso
using ProgressCallback  = std::function<void(LONGLONG bytesCopied, LONGLONG totalBytes)>;
using ErrorCallback     = std::function<void(const std::wstring& message, DWORD errorCode)>;
using CompletedCallback = std::function<void(bool success)>;

struct CopyOptions {
    DWORD   bufferSize        = 1024 * 1024; // 1 MB por defecto
    int     maxRetries        = 3;
    DWORD   retryDelayMs      = 500;
    bool    useNoBuffering    = true;
    bool    useOverlappedIO   = true;
    bool    verifyAfterCopy   = false;
    bool    skipOnError       = false;
};

class FileCopyEngine {
public:
    explicit FileCopyEngine(const CopyOptions& opts = {});
    ~FileCopyEngine();

    // Copia síncrona de un archivo
    bool CopyFile(
        const std::wstring& src,
        const std::wstring& dst,
        ProgressCallback  onProgress  = nullptr,
        ErrorCallback     onError     = nullptr,
        CompletedCallback onCompleted = nullptr
    );

    // Pausa / reanuda la operación actual
    void Pause();
    void Resume();
    void Cancel();

    bool IsPaused()    const { return m_paused.load(); }
    bool IsCancelled() const { return m_cancelled.load(); }

private:
    bool CopyWithOverlapped(HANDLE hSrc, HANDLE hDst, LONGLONG fileSize,
                            ProgressCallback& onProgress);
    bool CopyBuffered(HANDLE hSrc, HANDLE hDst, LONGLONG fileSize,
                      ProgressCallback& onProgress);
    bool VerifyFiles(const std::wstring& src, const std::wstring& dst);

    CopyOptions     m_opts;
    std::atomic<bool> m_paused    {false};
    std::atomic<bool> m_cancelled {false};
    LONGLONG        m_resumeOffset{0};
};

} // namespace FileCopier
