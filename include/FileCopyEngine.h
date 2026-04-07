#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <cstdint>

namespace FileCopier {

using ProgressCallback  = std::function<void(LONGLONG bytesCopied, LONGLONG totalBytes)>;
using ErrorCallback     = std::function<void(const std::wstring& message, DWORD errorCode)>;
using CompletedCallback = std::function<void(bool success)>;

struct CopyOptions {
    DWORD bufferSizeBytes = 4 * 1024 * 1024; // 4 MB (se ajusta al sector)
    int   maxRetries      = 3;
    DWORD retryDelayMs    = 500;
    bool  useNoBuffering  = false; // OFF por defecto: requiere alineación estricta
    bool  useOverlappedIO = true;  // double-buffer ping-pong
    bool  verifyAfterCopy = false;
    bool  skipOnError     = true;

    // Colisión de archivos
    enum class CollisionPolicy {
        Ask, Skip, Overwrite, OverwriteIfDifferent, RenameNew, RenameOld
    } collision = CollisionPolicy::Ask;

    // Error de copia
    enum class ErrorPolicy {
        Ask, CancelAll, Skip, RetryOnceThenLog, MoveToEnd
    } errorPolicy = ErrorPolicy::RetryOnceThenLog;
};

// Resultado de una copia individual
struct CopyResult {
    bool    success    = false;
    DWORD   lastError  = 0;
    LONGLONG bytesCopied = 0;
};

class FileCopyEngine {
public:
    explicit FileCopyEngine(const CopyOptions& opts = {});
    ~FileCopyEngine();

    CopyResult CopyFile(
        const std::wstring& src,
        const std::wstring& dst,
        ProgressCallback  onProgress  = nullptr,
        ErrorCallback     onError     = nullptr,
        CompletedCallback onCompleted = nullptr
    );

    void Pause();
    void Resume();
    void Cancel();

    bool IsPaused()    const { return m_paused.load();    }
    bool IsCancelled() const { return m_cancelled.load(); }

    // Offset desde el que reanudar (para resume entre sesiones)
    void SetResumeOffset(LONGLONG offset) { m_resumeOffset = offset; }

private:
    // Consulta el tamaño de sector real del disco donde está dst
    static DWORD QuerySectorSize(const std::wstring& path);

    // Alinea 'val' hacia arriba al múltiplo de 'align'
    static DWORD AlignUp(DWORD val, DWORD align) {
        return (val + align - 1) & ~(align - 1);
    }

    // Overlapped double-buffer (el método de alto rendimiento correcto)
    CopyResult CopyOverlapped(HANDLE hSrc, HANDLE hDst,
                               LONGLONG fileSize, DWORD sector,
                               ProgressCallback& cb);

    // Fallback secuencial simple (sin NO_BUFFERING)
    CopyResult CopySimple(HANDLE hSrc, HANDLE hDst,
                          LONGLONG fileSize,
                          ProgressCallback& cb);

    bool VerifySize(const std::wstring& src, const std::wstring& dst);

    CopyOptions       m_opts;
    std::atomic<bool> m_paused    {false};
    std::atomic<bool> m_cancelled {false};
    LONGLONG          m_resumeOffset {0};
};

} // namespace FileCopier
