#include "../../include/core/FileCopyEngine.h"
#include <windows.h>
#include <vector>

static const size_t BUFFER_SIZE = 1024 * 1024;

bool CopyFileStable(Job& job)
{
    HANDLE hSrc = CreateFileW(job.source.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hSrc == INVALID_HANDLE_VALUE) {
        job.status = JobStatus::Failed;
        return false;
    }

    HANDLE hDst = CreateFileW(job.destination.c_str(), GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hDst == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrc);
        job.status = JobStatus::Failed;
        return false;
    }

    LARGE_INTEGER size;
    GetFileSizeEx(hSrc, &size);
    job.totalBytes = size.QuadPart;

    std::vector<char> buffer(BUFFER_SIZE);

    DWORD bytesRead = 0;
    DWORD bytesWritten = 0;

    job.status = JobStatus::Running;

    while (ReadFile(hSrc, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr) && bytesRead > 0)
    {
        if (job.cancelRequested) {
            job.status = JobStatus::Cancelled;
            break;
        }

        while (job.pauseRequested) {
            Sleep(100);
        }

        if (!WriteFile(hDst, buffer.data(), bytesRead, &bytesWritten, nullptr)) {
            job.status = JobStatus::Failed;
            break;
        }

        job.bytesCopied += bytesWritten;
    }

    CloseHandle(hSrc);
    CloseHandle(hDst);

    if (job.status == JobStatus::Running)
        job.status = JobStatus::Completed;

    return job.status == JobStatus::Completed;
}