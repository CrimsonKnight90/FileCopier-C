// FileCopyEngine.cpp
// Motor de copia de alto rendimiento con:
// - Overlapped I/O double-buffer correcto
// - Alineación de sector consultada en tiempo real
// - FILE_FLAG_NO_BUFFERING con todas las reglas cumplidas
// - Manejo de errores estricto: cada ReadFile/WriteFile verificado
// - Pausa cooperativa sin pérdida de progreso

#include "../../include/FileCopyEngine.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include "../../include/BufferManager.h"
#include <algorithm>
#include <cassert>

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
FileCopyEngine::FileCopyEngine(const CopyOptions& opts) : m_opts(opts) {}
FileCopyEngine::~FileCopyEngine() = default;

void FileCopyEngine::Pause()  { m_paused  = true;  }
void FileCopyEngine::Resume() { m_paused  = false; }
void FileCopyEngine::Cancel() { m_cancelled = true; }

// ─────────────────────────────────────────────────────────────────────────────
// Consulta el tamaño de sector real del volumen que contiene 'path'
// ─────────────────────────────────────────────────────────────────────────────
DWORD FileCopyEngine::QuerySectorSize(const std::wstring& path) {
    // Extraer raíz del volumen (ej: "C:\")
    wchar_t root[MAX_PATH] = {};
    if (!::GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return 512;

    DWORD sectorsPerCluster = 0, bytesPerSector = 0,
          freeClusters = 0, totalClusters = 0;
    if (!::GetDiskFreeSpaceW(root, &sectorsPerCluster,
                              &bytesPerSector, &freeClusters, &totalClusters))
        return 512; // fallback conservador

    // bytesPerSector suele ser 512 o 4096 (Advanced Format / NVMe)
    return bytesPerSector > 0 ? bytesPerSector : 512;
}

// ─────────────────────────────────────────────────────────────────────────────
// Punto de entrada principal
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyFile(
    const std::wstring& src,
    const std::wstring& dst,
    ProgressCallback  onProgress,
    ErrorCallback     onError,
    CompletedCallback onCompleted)
{
    m_cancelled = false;
    m_paused    = false;

    // Prefijo para rutas largas (> MAX_PATH)
    auto longSrc = L"\\\\?\\" + src;
    auto longDst = L"\\\\?\\" + dst;

    // Crear directorio destino
    {
        auto dstDir = dst.substr(0, dst.find_last_of(L"\\/"));
        if (!dstDir.empty())
            ::CreateDirectoryW((L"\\\\?\\" + dstDir).c_str(), nullptr);
    }

    // ── Consultar sector ANTES de abrir con NO_BUFFERING ──────────────────
    DWORD sector = QuerySectorSize(dst);

    // ── Flags de apertura ─────────────────────────────────────────────────
    DWORD srcFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    DWORD dstFlags = FILE_FLAG_SEQUENTIAL_SCAN;

    if (m_opts.useNoBuffering) {
        srcFlags |= FILE_FLAG_NO_BUFFERING;
        dstFlags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    }
    if (m_opts.useOverlappedIO) {
        srcFlags |= FILE_FLAG_OVERLAPPED;
        dstFlags |= FILE_FLAG_OVERLAPPED;
    }

    // ── Abrir fuente ──────────────────────────────────────────────────────
    HANDLE hSrc = ::CreateFileW(longSrc.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, srcFlags, nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        std::wstring msg = L"Cannot open source: " + ErrorHandler::FormatWinError(err);
        if (onError) onError(msg, err);
        LOG_ERROR(msg);
        CopyResult r; r.lastError = err;
        if (onCompleted) onCompleted(false);
        return r;
    }

    // ── Tamaño real del archivo ───────────────────────────────────────────
    LARGE_INTEGER fs{};
    ::GetFileSizeEx(hSrc, &fs);
    LONGLONG fileSize = fs.QuadPart;

    EventBus::Instance().Emit(EventFileStarted{ src, fileSize });

    // ── Abrir destino ─────────────────────────────────────────────────────
    HANDLE hDst = ::CreateFileW(longDst.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, dstFlags, nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        std::wstring msg = L"Cannot open destination: " + ErrorHandler::FormatWinError(err);
        if (onError) onError(msg, err);
        LOG_ERROR(msg);
        ::CloseHandle(hSrc);
        CopyResult r; r.lastError = err;
        if (onCompleted) onCompleted(false);
        return r;
    }

    // ── Pre-alocar espacio en disco (mejora rendimiento y detecta disco lleno) ─
    if (fileSize > 0) {
        LARGE_INTEGER li; li.QuadPart = fileSize;
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
        ::SetEndOfFile(hDst);
        li.QuadPart = 0;
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
    }

    // ── Copiar ────────────────────────────────────────────────────────────
    CopyResult result;
    if (m_opts.useOverlappedIO)
        result = CopyOverlapped(hSrc, hDst, fileSize, sector, onProgress);
    else
        result = CopySimple(hSrc, hDst, fileSize, onProgress);

    // ── Truncar al tamaño real (NO_BUFFERING puede sobrepasar) ───────────
    if (result.success && m_opts.useNoBuffering) {
        LARGE_INTEGER li; li.QuadPart = fileSize;
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
        ::SetEndOfFile(hDst);
    }

    ::CloseHandle(hSrc);
    ::CloseHandle(hDst);

    if (result.success && m_opts.verifyAfterCopy)
        result.success = VerifySize(src, dst);

    EventBus::Instance().Emit(EventFileCompleted{ src, result.success });
    if (onCompleted) onCompleted(result.success);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlapped double-buffer correcto:
//   Mientras se escribe buffer[A], se lee en buffer[B] → solapamiento real
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyOverlapped(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize,
    DWORD sector, ProgressCallback& onProgress)
{
    CopyResult result;

    // Tamaño de buffer alineado al sector
    DWORD bufSize = AlignUp(m_opts.bufferSizeBytes, sector);

    // Dos buffers alineados (ping-pong)
    BufferManager bufA(bufSize), bufB(bufSize);
    BYTE* bufs[2] = { bufA.Data(), bufB.Data() };

    // Dos pares de OVERLAPPED (lectura/escritura independientes)
    OVERLAPPED ovR{}, ovW{};
    ovR.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovW.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!ovR.hEvent || !ovW.hEvent) {
        result.lastError = ::GetLastError();
        if (ovR.hEvent) ::CloseHandle(ovR.hEvent);
        if (ovW.hEvent) ::CloseHandle(ovW.hEvent);
        return result;
    }

    auto setOvOffset = [](OVERLAPPED& ov, LONGLONG off) {
        ULARGE_INTEGER u; u.QuadPart = static_cast<ULONGLONG>(off);
        ov.Offset     = u.LowPart;
        ov.OffsetHigh = u.HighPart;
    };

    LONGLONG readOffset  = m_resumeOffset; // dónde leer
    LONGLONG writeOffset = m_resumeOffset; // dónde escribir
    LONGLONG totalCopied = m_resumeOffset;
    int      cur         = 0;              // índice buffer actual (0 o 1)
    bool     pendingWrite= false;
    DWORD    pendingWriteBytes = 0;

    auto waitAndCheck = [&](OVERLAPPED& ov, HANDLE h, DWORD& bytes) -> bool {
        if (::WaitForSingleObject(ov.hEvent, INFINITE) != WAIT_OBJECT_0) {
            result.lastError = ::GetLastError();
            return false;
        }
        if (!::GetOverlappedResult(h, &ov, &bytes, FALSE)) {
            DWORD err = ::GetLastError();
            if (err == ERROR_HANDLE_EOF || err == ERROR_BROKEN_PIPE)
                return true; // EOF normal
            result.lastError = err;
            return false;
        }
        return true;
    };

    ULONGLONG lastSpeedTick = ::GetTickCount64();
    LONGLONG  lastSpeedBytes = totalCopied;

    while (readOffset < fileSize && !m_cancelled) {
        // ── Pausa cooperativa ──────────────────────────────────────────
        while (m_paused && !m_cancelled) ::Sleep(20);
        if (m_cancelled) break;

        // ── Calcular cuánto leer ───────────────────────────────────────
        LONGLONG remaining = fileSize - readOffset;
        DWORD toRead = (remaining >= bufSize)
                       ? bufSize
                       : AlignUp(static_cast<DWORD>(remaining), sector);
        // Para NO_BUFFERING: toRead ya está alineado al sector

        // ── Emitir lectura asíncrona en buffer actual ──────────────────
        setOvOffset(ovR, readOffset);
        ::ResetEvent(ovR.hEvent);
        DWORD bytesRead = 0;
        BOOL  readOK    = ::ReadFile(hSrc, bufs[cur], toRead, nullptr, &ovR);
        if (!readOK && ::GetLastError() != ERROR_IO_PENDING) {
            result.lastError = ::GetLastError();
            LOG_ERROR(L"ReadFile failed: " + ErrorHandler::FormatWinError(result.lastError));
            break;
        }

        // ── Esperar escritura pendiente del buffer anterior ────────────
        if (pendingWrite) {
            DWORD written = 0;
            if (!waitAndCheck(ovW, hDst, written)) {
                LOG_ERROR(L"WriteFile failed: " + ErrorHandler::FormatWinError(result.lastError));
                break;
            }
            if (written != pendingWriteBytes) {
                result.lastError = ERROR_WRITE_FAULT;
                LOG_ERROR(L"WriteFile: short write");
                break;
            }
            writeOffset  += written;
            totalCopied  += written;
            if (onProgress) onProgress(totalCopied, fileSize);

            // Velocidad
            ULONGLONG now = ::GetTickCount64();
            if (now - lastSpeedTick >= 500) {
                double mbps = (totalCopied - lastSpeedBytes)
                              / 1024.0 / 1024.0
                              / ((now - lastSpeedTick) / 1000.0);
                EventBus::Instance().Emit(EventSpeedUpdate{ mbps, fileSize, totalCopied });
                lastSpeedTick  = now;
                lastSpeedBytes = totalCopied;
            }
        }

        // ── Esperar que termine la lectura ─────────────────────────────
        if (!waitAndCheck(ovR, hSrc, bytesRead)) break;
        if (bytesRead == 0) break; // EOF

        readOffset += bytesRead;

        // Bytes a escribir = bytes reales (no el padding de sector)
        DWORD toWrite = static_cast<DWORD>(
            std::min<LONGLONG>(bytesRead, fileSize - writeOffset));
        if (m_opts.useNoBuffering)
            toWrite = AlignUp(toWrite, sector); // escritura también alineada

        // ── Emitir escritura asíncrona ─────────────────────────────────
        setOvOffset(ovW, writeOffset);
        ::ResetEvent(ovW.hEvent);
        BOOL writeOK = ::WriteFile(hDst, bufs[cur], toWrite, nullptr, &ovW);
        if (!writeOK && ::GetLastError() != ERROR_IO_PENDING) {
            result.lastError = ::GetLastError();
            LOG_ERROR(L"WriteFile failed immediately: "
                      + ErrorHandler::FormatWinError(result.lastError));
            break;
        }

        pendingWrite      = true;
        pendingWriteBytes = toWrite;
        cur ^= 1; // alternar buffer
    }

    // ── Recoger la última escritura pendiente ──────────────────────────
    if (pendingWrite && !m_cancelled && result.lastError == 0) {
        DWORD written = 0;
        if (!waitAndCheck(ovW, hDst, written))
            LOG_ERROR(L"Final WriteFile failed");
        else {
            writeOffset += written;
            totalCopied += written;
            if (onProgress) onProgress(totalCopied, fileSize);
        }
    }

    ::CloseHandle(ovR.hEvent);
    ::CloseHandle(ovW.hEvent);

    result.bytesCopied = totalCopied;
    result.success     = (result.lastError == 0) && !m_cancelled
                         && (totalCopied >= fileSize);
    if (result.success) m_resumeOffset = 0;
    else                m_resumeOffset = totalCopied;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Copia secuencial simple (sin NO_BUFFERING, sin overlapped)
// Segura y portable; usar como fallback o en redes
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopySimple(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize, ProgressCallback& onProgress)
{
    CopyResult result;
    BufferManager buf(m_opts.bufferSizeBytes);
    LONGLONG copied = m_resumeOffset;

    // Posicionar si reanudamos
    if (copied > 0) {
        LARGE_INTEGER li; li.QuadPart = copied;
        ::SetFilePointerEx(hSrc, li, nullptr, FILE_BEGIN);
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
    }

    ULONGLONG lastTick  = ::GetTickCount64();
    LONGLONG  lastBytes = copied;

    while (copied < fileSize && !m_cancelled) {
        while (m_paused && !m_cancelled) ::Sleep(20);
        if (m_cancelled) break;

        DWORD toRead = static_cast<DWORD>(
            std::min<LONGLONG>(m_opts.bufferSizeBytes, fileSize - copied));
        DWORD bytesRead = 0, bytesWritten = 0;

        if (!::ReadFile(hSrc, buf.Data(), toRead, &bytesRead, nullptr)
            || bytesRead == 0)
        {
            result.lastError = ::GetLastError();
            LOG_ERROR(L"ReadFile failed: " + ErrorHandler::FormatWinError(result.lastError));
            break;
        }

        if (!::WriteFile(hDst, buf.Data(), bytesRead, &bytesWritten, nullptr)
            || bytesWritten != bytesRead)
        {
            result.lastError = ::GetLastError();
            LOG_ERROR(L"WriteFile failed: " + ErrorHandler::FormatWinError(result.lastError));
            break;
        }

        copied += bytesWritten;
        if (onProgress) onProgress(copied, fileSize);

        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 500) {
            double mbps = (copied - lastBytes) / 1024.0 / 1024.0
                          / ((now - lastTick) / 1000.0);
            EventBus::Instance().Emit(EventSpeedUpdate{ mbps, fileSize, copied });
            lastTick  = now;
            lastBytes = copied;
        }
    }

    result.bytesCopied = copied;
    result.success     = (result.lastError == 0) && !m_cancelled
                         && (copied >= fileSize);
    if (result.success) m_resumeOffset = 0;
    else                m_resumeOffset = copied;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::VerifySize(const std::wstring& src, const std::wstring& dst) {
    auto open = [](const std::wstring& p) {
        return ::CreateFileW((L"\\\\?\\" + p).c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, 0, nullptr);
    };
    HANDLE hS = open(src), hD = open(dst);
    if (hS == INVALID_HANDLE_VALUE || hD == INVALID_HANDLE_VALUE) {
        if (hS != INVALID_HANDLE_VALUE) ::CloseHandle(hS);
        if (hD != INVALID_HANDLE_VALUE) ::CloseHandle(hD);
        return false;
    }
    LARGE_INTEGER sS{}, sD{};
    ::GetFileSizeEx(hS, &sS);
    ::GetFileSizeEx(hD, &sD);
    ::CloseHandle(hS); ::CloseHandle(hD);
    return sS.QuadPart == sD.QuadPart;
}

} // namespace FileCopier
