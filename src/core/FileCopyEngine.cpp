#include "../../include/FileCopyEngine.h"
#include <algorithm>
#include "../../include/BufferManager.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include <stdexcept>
#include <cassert>

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
FileCopyEngine::FileCopyEngine(const CopyOptions& opts)
    : m_opts(opts)
{}

FileCopyEngine::~FileCopyEngine() = default;

// ─────────────────────────────────────────────────────────────────────────────
void FileCopyEngine::Pause()    { m_paused    = true;  }
void FileCopyEngine::Resume()   { m_paused    = false; }
void FileCopyEngine::Cancel()   { m_cancelled = true;  }

// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::CopyFile(
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

    // Crear directorio destino si no existe
    {
        auto dstDir = dst.substr(0, dst.find_last_of(L"\\/"));
        if (!dstDir.empty())
            ::CreateDirectoryW((L"\\\\?\\" + dstDir).c_str(), nullptr);
    }

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

    HANDLE hSrc = ::CreateFileW(longSrc.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, srcFlags, nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        if (onError) onError(ErrorHandler::FormatWinError(err), err);
        LOG_ERROR(L"Cannot open source: " + src);
        if (onCompleted) onCompleted(false);
        return false;
    }

    // Obtener tamaño
    LARGE_INTEGER fileSize{};
    ::GetFileSizeEx(hSrc, &fileSize);
    LONGLONG totalBytes = fileSize.QuadPart;

    EventBus::Instance().Emit(EventFileStarted{ src, totalBytes });

    HANDLE hDst = ::CreateFileW(longDst.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, dstFlags, nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        if (onError) onError(ErrorHandler::FormatWinError(err), err);
        LOG_ERROR(L"Cannot open destination: " + dst);
        ::CloseHandle(hSrc);
        if (onCompleted) onCompleted(false);
        return false;
    }

    bool ok = false;
    if (m_opts.useOverlappedIO)
        ok = CopyWithOverlapped(hSrc, hDst, totalBytes, onProgress);
    else
        ok = CopyBuffered(hSrc, hDst, totalBytes, onProgress);

    ::CloseHandle(hSrc);
    ::CloseHandle(hDst);

    if (ok && m_opts.verifyAfterCopy)
        ok = VerifyFiles(src, dst);

    EventBus::Instance().Emit(EventFileCompleted{ src, ok });
    if (onCompleted) onCompleted(ok);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlapped (asíncrono) I/O — doble buffer ping-pong
// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::CopyWithOverlapped(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize, ProgressCallback& onProgress)
{
    const DWORD BUF = m_opts.bufferSize;
    // Alineamos a 4 KB para NO_BUFFERING
    BufferManager bufA(BUF), bufB(BUF);
    OVERLAPPED    ovRead{}, ovWrite{};
    ovRead.hEvent  = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovWrite.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    LONGLONG offset   = m_resumeOffset;
    LONGLONG copied   = offset;
    ULARGE_INTEGER uOff;
    bool ok = true;

    auto setOffset = [&](OVERLAPPED& ov, LONGLONG off) {
        uOff.QuadPart     = static_cast<ULONGLONG>(off);
        ov.Offset         = uOff.LowPart;
        ov.OffsetHigh     = uOff.HighPart;
    };

    while (copied < fileSize && !m_cancelled) {
        // Pausa cooperativa
        while (m_paused && !m_cancelled) ::Sleep(50);
        if (m_cancelled) { ok = false; break; }

        DWORD toRead = static_cast<DWORD>(std::min<LONGLONG>(BUF, fileSize - offset));
        // Ajuste para NO_BUFFERING: redondear al sector
        if (m_opts.useNoBuffering) {
            toRead = (toRead + 511) & ~511u;
        }

        setOffset(ovRead, offset);
        ::ResetEvent(ovRead.hEvent);
        DWORD bytesRead = 0;
        if (!::ReadFile(hSrc, bufA.Data(), toRead, nullptr, &ovRead)) {
            if (::GetLastError() != ERROR_IO_PENDING) { ok = false; break; }
        }
        ::WaitForSingleObject(ovRead.hEvent, INFINITE);
        if (!::GetOverlappedResult(hSrc, &ovRead, &bytesRead, FALSE)) {
            ok = false; break;
        }
        if (bytesRead == 0) break;

        // Recortar a tamaño real (puede haber padding por sector)
        DWORD toWrite = std::min<DWORD>(bytesRead, static_cast<DWORD>(fileSize - copied));

        setOffset(ovWrite, copied);
        ::ResetEvent(ovWrite.hEvent);
        DWORD bytesWritten = 0;
        if (!::WriteFile(hDst, bufA.Data(), toWrite, nullptr, &ovWrite)) {
            if (::GetLastError() != ERROR_IO_PENDING) { ok = false; break; }
        }
        ::WaitForSingleObject(ovWrite.hEvent, INFINITE);
        if (!::GetOverlappedResult(hDst, &ovWrite, &bytesWritten, FALSE)) {
            ok = false; break;
        }

        offset += bytesRead;
        copied += bytesWritten;
        m_resumeOffset = copied;

        if (onProgress) onProgress(copied, fileSize);

        static ULONGLONG lastTick = ::GetTickCount64();
        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 500) {
            double mbps = (double)copied / (1024.0*1024.0) / ((now - lastTick + 1) / 1000.0);
            EventBus::Instance().Emit(EventSpeedUpdate{ mbps, fileSize, copied });
            lastTick = now;
        }
    }

    ::CloseHandle(ovRead.hEvent);
    ::CloseHandle(ovWrite.hEvent);

    if (ok) m_resumeOffset = 0; // reset al terminar bien
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Copia secuencial simple (fallback)
// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::CopyBuffered(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize, ProgressCallback& onProgress)
{
    BufferManager buf(m_opts.bufferSize);
    LONGLONG copied = m_resumeOffset;
    bool ok = true;

    while (copied < fileSize && !m_cancelled) {
        while (m_paused && !m_cancelled) ::Sleep(50);
        if (m_cancelled) { ok = false; break; }

        DWORD toRead = static_cast<DWORD>(std::min<LONGLONG>(m_opts.bufferSize, fileSize - copied));
        DWORD bytesRead = 0, bytesWritten = 0;

        if (!::ReadFile(hSrc, buf.Data(), toRead, &bytesRead, nullptr) || bytesRead == 0)
            break;
        if (!::WriteFile(hDst, buf.Data(), bytesRead, &bytesWritten, nullptr)) {
            ok = false; break;
        }

        copied += bytesWritten;
        m_resumeOffset = copied;
        if (onProgress) onProgress(copied, fileSize);
    }

    if (ok) m_resumeOffset = 0;
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
bool FileCopyEngine::VerifyFiles(const std::wstring& src, const std::wstring& dst)
{
    // Verificación básica por tamaño
    HANDLE hS = ::CreateFileW((L"\\\\?\\" + src).c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE hD = ::CreateFileW((L"\\\\?\\" + dst).c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

    if (hS == INVALID_HANDLE_VALUE || hD == INVALID_HANDLE_VALUE) {
        if (hS != INVALID_HANDLE_VALUE) ::CloseHandle(hS);
        if (hD != INVALID_HANDLE_VALUE) ::CloseHandle(hD);
        return false;
    }

    LARGE_INTEGER sSize{}, dSize{};
    ::GetFileSizeEx(hS, &sSize);
    ::GetFileSizeEx(hD, &dSize);
    ::CloseHandle(hS);
    ::CloseHandle(hD);

    return sSize.QuadPart == dSize.QuadPart;
}

} // namespace FileCopier
