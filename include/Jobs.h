#pragma once
#include "DirectoryScanner.h"
#include <queue>
#include <vector>
#include <mutex>
#include <optional>
#include <atomic>
#include <string>

namespace FileCopier {

// ── Estado de cada tarea ─────────────────────────────────────────────────────
enum class JobStatus {
    Pending,
    Copying,
    Paused,
    Completed,
    Error,
    Skipped,
    Cancelled
};

struct Job {
    size_t      id          = 0;
    FileEntry   file;
    JobStatus   status      = JobStatus::Pending;
    LONGLONG    bytesDone   = 0;
    int         retries     = 0;
    std::wstring errorMsg;
};

// ── Cola de trabajos ─────────────────────────────────────────────────────────
class JobQueue {
public:
    void           Push(Job job);
    std::optional<Job> Pop();          // retorna el siguiente Pending
    void           UpdateStatus(size_t id, JobStatus status, LONGLONG bytesDone = 0);
    void           SetError(size_t id, const std::wstring& msg);

    size_t         TotalJobs()     const;
    size_t         CompletedJobs() const;
    size_t         ErrorJobs()     const;
    bool           IsEmpty()       const;

    // Snapshot para UI
    std::vector<Job> Snapshot() const;

    void           Clear();

private:
    mutable std::mutex m_mutex;
    std::vector<Job>   m_jobs;
    size_t             m_nextId = 0;
};

// ── Estado individual ────────────────────────────────────────────────────────
class JobState {
public:
    explicit JobState(size_t jobId);

    void SetStatus(JobStatus s);
    void SetProgress(LONGLONG bytes, LONGLONG total);
    void SetError(const std::wstring& msg);

    JobStatus   Status()     const;
    LONGLONG    BytesDone()  const;
    LONGLONG    TotalBytes() const;
    std::wstring ErrorMsg()  const;
    size_t      Id()         const { return m_id; }

private:
    size_t                    m_id;
    mutable std::mutex        m_mutex;
    JobStatus                 m_status    = JobStatus::Pending;
    LONGLONG                  m_bytesDone = 0;
    LONGLONG                  m_total     = 0;
    std::wstring              m_errorMsg;
};

// ── Gestión de reanudación ───────────────────────────────────────────────────
struct ResumeEntry {
    std::wstring srcPath;
    std::wstring dstPath;
    LONGLONG     bytesDone = 0;
};

class ResumeManager {
public:
    explicit ResumeManager(const std::wstring& stateFile);

    void Save(const std::vector<Job>& jobs);
    bool Load(std::vector<ResumeEntry>& entries);
    void Clear();

private:
    std::wstring m_stateFile;
};

} // namespace FileCopier
