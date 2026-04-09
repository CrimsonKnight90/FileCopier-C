// FileCopyEngine.cpp — Motor de copia robusto
// CopySimple: método principal, 100% correcto, sin flags especiales
// CopyOverlapped: método de alto rendimiento (opcional, activar desde Opciones)

#include "../../include/FileCopyEngine.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include "../../include/BufferManager.h"
#include <algorithm>

namespace FileCopier {

FileCopyEngine::FileCopyEngine(const CopyOptions& opts) : m_opts(opts) {}
FileCopyEngine::~FileCopyEngine() = default;

void FileCopyEngine::Pause()  { m_paused    = true;  }
void FileCopyEngine::Resume() { m_paused    = false; }
void FileCopyEngine::Cancel() { m_cancelled = true;  }

// ─────────────────────────────────────────────────────────────────────────────
DWORD FileCopyEngine::QuerySectorSize(const std::wstring& path) {
    wchar_t root[MAX_PATH] = {};
    if (!::GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return 512;
    DWORD spc = 0, bps = 0, fc = 0, tc = 0;
    if (!::GetDiskFreeSpaceW(root, &spc, &bps, &fc, &tc)) return 512;
    return bps > 0 ? bps : 512;
}

// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyFile(
    const std::wstring& src, const std::wstring& dst,
    ProgressCallback  onProgress,
    ErrorCallback     onError,
    CompletedCallback onCompleted)
{
    m_cancelled = false;
    m_paused    = false;
    CopyResult result;

    // Prefijo para rutas largas
    std::wstring longSrc = L"\\\\?\\" + src;
    std::wstring longDst = L"\\\\?\\" + dst;

    // Crear directorio destino si no existe
    {
        size_t pos = dst.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring dstDir = dst.substr(0, pos);
            if (!dstDir.empty())
                ::CreateDirectoryW((L"\\\\?\\" + dstDir).c_str(), nullptr);
        }
    }

    // ── Abrir fuente (sin flags especiales = máxima compatibilidad) ───────
    HANDLE hSrc = ::CreateFileW(longSrc.c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    if (hSrc == INVALID_HANDLE_VALUE) {
        result.lastError = ::GetLastError();
        std::wstring msg = L"Cannot open source [" + std::to_wstring(result.lastError)
                         + L"]: " + ErrorHandler::FormatWinError(result.lastError);
        if (onError) onError(msg, result.lastError);
        LOG_ERROR(msg + L" — " + src);
        if (onCompleted) onCompleted(false);
        return result;
    }

    // Tamaño del archivo
    LARGE_INTEGER fs{};
    ::GetFileSizeEx(hSrc, &fs);
    LONGLONG fileSize = fs.QuadPart;

    EventBus::Instance().Emit(EventFileStarted{src, fileSize});

    // ── Abrir destino ─────────────────────────────────────────────────────
    DWORD dstFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    if (m_opts.useNoBuffering) dstFlags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;

    HANDLE hDst = ::CreateFileW(longDst.c_str(),
        GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, dstFlags, nullptr);

    if (hDst == INVALID_HANDLE_VALUE) {
        result.lastError = ::GetLastError();
        std::wstring msg = L"Cannot open destination [" + std::to_wstring(result.lastError)
                         + L"]: " + ErrorHandler::FormatWinError(result.lastError);
        if (onError) onError(msg, result.lastError);
        LOG_ERROR(msg + L" — " + dst);
        ::CloseHandle(hSrc);
        if (onCompleted) onCompleted(false);
        return result;
    }

    // ── Copiar ────────────────────────────────────────────────────────────
    if (m_opts.useOverlappedIO && !m_opts.useNoBuffering && fileSize > 0)
        result = CopyOverlapped(hSrc, hDst, fileSize, onProgress);
    else
        result = CopySimple(hSrc, hDst, fileSize, onProgress);

    // Truncar al tamaño real si usamos NO_BUFFERING (puede sobrepasar)
    if (m_opts.useNoBuffering && result.success) {
        LARGE_INTEGER li; li.QuadPart = fileSize;
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
        ::SetEndOfFile(hDst);
    }

    ::CloseHandle(hSrc);
    ::CloseHandle(hDst);

    if (result.success && m_opts.verifyAfterCopy)
        result.success = VerifySize(src, dst);

    if (!result.success && result.lastError != 0) {
        std::wstring msg = L"Copy failed [" + std::to_wstring(result.lastError)
                         + L"]: " + ErrorHandler::FormatWinError(result.lastError);
        if (onError) onError(msg, result.lastError);
    }

    EventBus::Instance().Emit(EventFileCompleted{src, result.success});
    if (onCompleted) onCompleted(result.success);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CopySimple — método principal, robusto y correcto
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopySimple(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize, ProgressCallback& onProgress)
{
    CopyResult result;

    // Buffer normal (no alineado) — sin NO_BUFFERING
    std::vector<BYTE> buf(m_opts.bufferSizeBytes);

    LONGLONG copied = m_resumeOffset;
    if (copied > 0) {
        LARGE_INTEGER li; li.QuadPart = copied;
        ::SetFilePointerEx(hSrc, li, nullptr, FILE_BEGIN);
        ::SetFilePointerEx(hDst, li, nullptr, FILE_BEGIN);
    }

    ULONGLONG lastTick  = ::GetTickCount64();
    LONGLONG  lastBytes = copied;

    // Para archivos vacíos, consideramos éxito inmediato
    if (fileSize == 0) {
        result.success = true;
        if (onProgress) onProgress(0, 0);
        return result;
    }

    while (copied < fileSize && !m_cancelled) {
        while (m_paused && !m_cancelled) ::Sleep(20);
        if (m_cancelled) break;

        DWORD toRead = static_cast<DWORD>(
            std::min<LONGLONG>((LONGLONG)m_opts.bufferSizeBytes, fileSize - copied));
        DWORD bytesRead = 0;

        if (!::ReadFile(hSrc, buf.data(), toRead, &bytesRead, nullptr)) {
            result.lastError = ::GetLastError();
            LOG_ERROR(L"ReadFile error " + std::to_wstring(result.lastError)
                      + L": " + ErrorHandler::FormatWinError(result.lastError));
            return result;
        }
        if (bytesRead == 0) break; // EOF inesperado

        DWORD bytesWritten = 0;
        if (!::WriteFile(hDst, buf.data(), bytesRead, &bytesWritten, nullptr)
            || bytesWritten != bytesRead)
        {
            result.lastError = ::GetLastError();
            if (result.lastError == 0) result.lastError = ERROR_WRITE_FAULT;
            LOG_ERROR(L"WriteFile error " + std::to_wstring(result.lastError)
                      + L": " + ErrorHandler::FormatWinError(result.lastError));
            return result;
        }

        copied += bytesWritten;
        if (onProgress) onProgress(copied, fileSize);

        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 300) {
            double mbps = (double)(copied - lastBytes)
                          / 1024.0 / 1024.0
                          / ((now - lastTick) / 1000.0);
            EventBus::Instance().Emit(EventSpeedUpdate{mbps, fileSize, copied});
            lastTick  = now;
            lastBytes = copied;
        }
    }

    result.bytesCopied = copied;
    result.success = !m_cancelled && (copied >= fileSize) && (result.lastError == 0);
    m_resumeOffset = result.success ? 0 : copied;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CopyOverlapped — doble buffer asíncrono (solo si !useNoBuffering)
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyOverlapped(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize,
    DWORD /*sector*/, ProgressCallback& onProgress)
{
    // Re-abrir los handles con FILE_FLAG_OVERLAPPED
    // Nota: esta función recibe handles ya abiertos SIN overlapped flag,
    // así que usamos ReadFile/WriteFile con offsets explícitos via OVERLAPPED
    // sin el flag en el handle — esto es válido y funciona como "positional I/O"

    CopyResult result;
    const DWORD BUF = m_opts.bufferSizeBytes;
    std::vector<BYTE> bufA(BUF), bufB(BUF);
    BYTE* bufs[2] = { bufA.data(), bufB.data() };

    OVERLAPPED ovR{}, ovW{};
    // Sin handles de evento: usar ReadFile/WriteFile sync con OVERLAPPED para
    // especificar offset (funciona aunque el handle no tenga FILE_FLAG_OVERLAPPED)
    // Este patrón da acceso posicional sin overhead de eventos

    LONGLONG readOff  = m_resumeOffset;
    LONGLONG writeOff = m_resumeOffset;
    LONGLONG copied   = m_resumeOffset;
    int cur = 0;

    auto setOff = [](OVERLAPPED& ov, LONGLONG off) {
        ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)off;
        ov.Offset = u.LowPart; ov.OffsetHigh = u.HighPart;
    };

    ULONGLONG lastTick  = ::GetTickCount64();
    LONGLONG  lastBytes = copied;

    while (readOff < fileSize && !m_cancelled) {
        while (m_paused && !m_cancelled) ::Sleep(20);
        if (m_cancelled) break;

        DWORD toRead = (DWORD)std::min<LONGLONG>(BUF, fileSize - readOff);
        DWORD bytesRead = 0, bytesWritten = 0;

        setOff(ovR, readOff);
        if (!::ReadFile(hSrc, bufs[cur], toRead, &bytesRead, &ovR)
            || bytesRead == 0) {
            result.lastError = ::GetLastError();
            if (result.lastError == ERROR_HANDLE_EOF || bytesRead == 0) break;
            LOG_ERROR(L"Overlapped ReadFile error " + std::to_wstring(result.lastError));
            return result;
        }
        readOff += bytesRead;

        DWORD toWrite = (DWORD)std::min<LONGLONG>((LONGLONG)bytesRead, fileSize - writeOff);
        setOff(ovW, writeOff);
        if (!::WriteFile(hDst, bufs[cur], toWrite, &bytesWritten, &ovW)
            || bytesWritten != toWrite) {
            result.lastError = ::GetLastError();
            if (result.lastError == 0) result.lastError = ERROR_WRITE_FAULT;
            LOG_ERROR(L"Overlapped WriteFile error " + std::to_wstring(result.lastError));
            return result;
        }

        writeOff += bytesWritten;
        copied   += bytesWritten;
        cur ^= 1;

        if (onProgress) onProgress(copied, fileSize);

        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 300) {
            double mbps = (double)(copied - lastBytes) / 1024.0 / 1024.0
                          / ((now - lastTick) / 1000.0);
            EventBus::Instance().Emit(EventSpeedUpdate{mbps, fileSize, copied});
            lastTick  = now; lastBytes = copied;
        }
    }

    result.bytesCopied = copied;
    result.success = !m_cancelled && (copied >= fileSize) && (result.lastError == 0);
    m_resumeOffset = result.success ? 0 : copied;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::VerifySize(const std::wstring& src, const std::wstring& dst) {
    auto open = [](const std::wstring& p) {
        return ::CreateFileW((L"\\\\?\\" + p).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    };
    HANDLE hS = open(src), hD = open(dst);
    if (hS == INVALID_HANDLE_VALUE || hD == INVALID_HANDLE_VALUE) {
        if (hS != INVALID_HANDLE_VALUE) ::CloseHandle(hS);
        if (hD != INVALID_HANDLE_VALUE) ::CloseHandle(hD);
        return false;
    }
    LARGE_INTEGER sS{}, sD{};
    ::GetFileSizeEx(hS, &sS); ::GetFileSizeEx(hD, &sD);
    ::CloseHandle(hS); ::CloseHandle(hD);
    return sS.QuadPart == sD.QuadPart;
}

} // namespace FileCopier
