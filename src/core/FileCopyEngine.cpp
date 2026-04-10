// FileCopyEngine.cpp — Motor de copia robusto
// NOTA: Todos los paths deben llegar con backslash (\), no slash (/)
// QFileDialog retorna paths con / — normalizar ANTES de llamar a CopyFile

#include "../../include/FileCopyEngine.h"
#include "../../include/EventBus.h"
#include "../../include/Utils.h"
#include "../../include/BufferManager.h"
#include <algorithm>
#include <vector>

namespace FileCopier {

FileCopyEngine::FileCopyEngine(const CopyOptions& opts) : m_opts(opts) {}
FileCopyEngine::~FileCopyEngine() = default;

void FileCopyEngine::Pause()  { m_paused    = true;  }
void FileCopyEngine::Resume() { m_paused    = false; }
void FileCopyEngine::Cancel() { m_cancelled = true;  }

// Normaliza path: reemplaza / por \ y quita trailing slash
static std::wstring NormalizePath(std::wstring p) {
    for (auto& c : p) if (c == L'/') c = L'\\';
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
DWORD FileCopyEngine::QuerySectorSize(const std::wstring& path) {
    wchar_t root[MAX_PATH] = {};
    if (!::GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return 512;
    DWORD spc=0, bps=0, fc=0, tc=0;
    if (!::GetDiskFreeSpaceW(root, &spc, &bps, &fc, &tc)) return 512;
    return bps > 0 ? bps : 512;
}

// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyFile(
    const std::wstring& srcRaw, const std::wstring& dstRaw,
    ProgressCallback  onProgress,
    ErrorCallback     onError,
    CompletedCallback onCompleted)
{
    m_cancelled = false;
    m_paused    = false;
    CopyResult result;

    // Normalizar paths (/ → \)
    std::wstring src = NormalizePath(srcRaw);
    std::wstring dst = NormalizePath(dstRaw);

    // Prefijo para rutas largas (solo con backslash)
    std::wstring longSrc = L"\\\\?\\" + src;
    std::wstring longDst = L"\\\\?\\" + dst;

    // Crear directorio destino
    {
        size_t pos = dst.rfind(L'\\');
        if (pos != std::wstring::npos && pos > 0) {
            std::wstring dstDir = dst.substr(0, pos);
            // Crear recursivamente
            std::wstring partial;
            for (size_t i = 0; i < dstDir.size(); ++i) {
                partial += dstDir[i];
                if (dstDir[i] == L'\\' || i == dstDir.size()-1) {
                    if (partial.size() > 3) // no intentar crear "C:\"
                        ::CreateDirectoryW((L"\\\\?\\" + partial).c_str(), nullptr);
                }
            }
        }
    }

    // ── Abrir fuente ──────────────────────────────────────────────────────
    HANDLE hSrc = ::CreateFileW(longSrc.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    if (hSrc == INVALID_HANDLE_VALUE) {
        result.lastError = ::GetLastError();
        std::wstring msg = L"No se puede abrir origen ["
            + std::to_wstring(result.lastError) + L"]: "
            + ErrorHandler::FormatWinError(result.lastError)
            + L" → " + src;
        if (onError) onError(msg, result.lastError);
        LOG_ERROR(msg);
        if (onCompleted) onCompleted(false);
        return result;
    }

    LARGE_INTEGER fs{};
    ::GetFileSizeEx(hSrc, &fs);
    LONGLONG fileSize = fs.QuadPart;
    EventBus::Instance().Emit(EventFileStarted{src, fileSize});

    // ── Abrir destino ──────────────────────────────────────────────────────
    HANDLE hDst = ::CreateFileW(longDst.c_str(),
        GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    if (hDst == INVALID_HANDLE_VALUE) {
        result.lastError = ::GetLastError();
        std::wstring msg = L"No se puede abrir destino ["
            + std::to_wstring(result.lastError) + L"]: "
            + ErrorHandler::FormatWinError(result.lastError)
            + L" → " + dst;
        if (onError) onError(msg, result.lastError);
        LOG_ERROR(msg);
        ::CloseHandle(hSrc);
        if (onCompleted) onCompleted(false);
        return result;
    }

    // ── Copiar ────────────────────────────────────────────────────────────
    result = CopySimple(hSrc, hDst, fileSize, onProgress);

    ::CloseHandle(hSrc);
    ::CloseHandle(hDst);

    // Si falló, eliminar el archivo destino incompleto
    if (!result.success && !m_cancelled) {
        ::DeleteFileW(longDst.c_str());
    }

    if (result.success && m_opts.verifyAfterCopy)
        result.success = VerifySize(src, dst);

    if (!result.success && result.lastError != 0) {
        std::wstring msg = L"Copia fallida ["
            + std::to_wstring(result.lastError) + L"]: "
            + ErrorHandler::FormatWinError(result.lastError);
        if (onError) onError(msg, result.lastError);
        LOG_ERROR(msg + L" src=" + src + L" dst=" + dst);
    }

    EventBus::Instance().Emit(EventFileCompleted{src, result.success});
    if (onCompleted) onCompleted(result.success);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CopySimple — secuencial, sin flags especiales, con std::vector
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopySimple(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize, ProgressCallback& onProgress)
{
    CopyResult result;

    if (fileSize == 0) {
        result.success = true;
        if (onProgress) onProgress(0, 0);
        return result;
    }

    const DWORD CHUNK = m_opts.bufferSizeBytes;
    std::vector<BYTE> buf(CHUNK);

    LONGLONG copied = 0;
    ULONGLONG lastTick  = ::GetTickCount64();
    LONGLONG  lastBytes = 0;

    while (copied < fileSize && !m_cancelled) {
        while (m_paused && !m_cancelled) ::Sleep(20);
        if (m_cancelled) { result.lastError = ERROR_OPERATION_ABORTED; return result; }

        DWORD toRead = (DWORD)std::min<LONGLONG>(CHUNK, fileSize - copied);
        DWORD bytesRead = 0;

        BOOL rOK = ::ReadFile(hSrc, buf.data(), toRead, &bytesRead, nullptr);
        if (!rOK || bytesRead == 0) {
            result.lastError = rOK ? ERROR_HANDLE_EOF : ::GetLastError();
            LOG_ERROR(L"ReadFile fallo: " + ErrorHandler::FormatWinError(result.lastError));
            return result;
        }

        DWORD bytesWritten = 0;
        BOOL wOK = ::WriteFile(hDst, buf.data(), bytesRead, &bytesWritten, nullptr);
        if (!wOK || bytesWritten != bytesRead) {
            result.lastError = wOK ? ERROR_WRITE_FAULT : ::GetLastError();
            LOG_ERROR(L"WriteFile fallo: " + ErrorHandler::FormatWinError(result.lastError));
            return result;
        }

        copied += bytesWritten;
        if (onProgress) onProgress(copied, fileSize);

        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 400) {
            double mbps = (double)(copied - lastBytes) / 1024.0 / 1024.0
                          / ((now - lastTick) / 1000.0);
            EventBus::Instance().Emit(EventSpeedUpdate{mbps, fileSize, copied});
            lastTick  = now;
            lastBytes = copied;
        }
    }

    result.bytesCopied = copied;
    result.success = !m_cancelled && (copied == fileSize);
    m_resumeOffset = result.success ? 0 : copied;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CopyOverlapped — stub, por ahora igual a CopySimple (para no romper el header)
// ─────────────────────────────────────────────────────────────────────────────
CopyResult FileCopyEngine::CopyOverlapped(
    HANDLE hSrc, HANDLE hDst, LONGLONG fileSize,
    DWORD /*sector*/, ProgressCallback& onProgress)
{
    return CopySimple(hSrc, hDst, fileSize, onProgress);
}

bool FileCopyEngine::VerifySize(const std::wstring& src, const std::wstring& dst) {
    auto normSrc = NormalizePath(src);
    auto normDst = NormalizePath(dst);
    auto open = [](const std::wstring& p) {
        return ::CreateFileW((L"\\\\?\\" + p).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    };
    HANDLE hS = open(normSrc), hD = open(normDst);
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
