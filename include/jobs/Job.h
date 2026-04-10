#pragma once
#include <string>
#include <atomic>
#include <cstdint>

enum class JobStatus {
    Pending,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct Job {
    int id;
    std::wstring source;
    std::wstring destination;

    std::atomic<JobStatus> status{JobStatus::Pending};
    std::atomic<uint64_t> bytesCopied{0};
    uint64_t totalBytes{0};

    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> pauseRequested{false};
};