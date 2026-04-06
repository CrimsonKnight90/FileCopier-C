#include "../../include/Utils.h"

namespace FileCopier {

ErrorAction ErrorHandler::Handle(
    const std::wstring& filePath,
    DWORD               winError,
    int                 retriesDone,
    int                 maxRetries,
    bool                skipOnError)
{
    std::wstring msg = L"Error " + std::to_wstring(winError)
                     + L" on: " + filePath
                     + L" — " + FormatWinError(winError);

    LOG_ERROR(msg);

    // Errores irrecuperables → saltar o abortar
    switch (winError) {
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            if (retriesDone < maxRetries) return ErrorAction::Retry;
            return skipOnError ? ErrorAction::Skip : ErrorAction::Abort;

        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
            // Sin espacio: no tiene sentido reintentar
            return ErrorAction::Abort;

        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return skipOnError ? ErrorAction::Skip : ErrorAction::Abort;

        case ERROR_LOCK_VIOLATION:
        case ERROR_NETWORK_BUSY:
            if (retriesDone < maxRetries) return ErrorAction::Retry;
            return skipOnError ? ErrorAction::Skip : ErrorAction::Abort;

        default:
            if (retriesDone < maxRetries) return ErrorAction::Retry;
            return skipOnError ? ErrorAction::Skip : ErrorAction::Abort;
    }
}

std::wstring ErrorHandler::FormatWinError(DWORD code) {
    LPWSTR buf = nullptr;
    DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr
    );

    std::wstring msg;
    if (len && buf) {
        msg = std::wstring(buf, len);
        // Quitar \r\n final
        while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r'))
            msg.pop_back();
    }
    ::LocalFree(buf);
    return msg.empty() ? L"Unknown error" : msg;
}

} // namespace FileCopier
