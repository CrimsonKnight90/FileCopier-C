#include "../../include/Jobs.h"
#include <algorithm>

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
void JobQueue::Push(Job job) {
    std::lock_guard<std::mutex> lock(m_mutex);
    job.id = m_nextId++;
    m_jobs.push_back(std::move(job));
}

std::optional<Job> JobQueue::Pop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs) {
        if (j.status == JobStatus::Pending) {
            j.status = JobStatus::Copying;
            return j;
        }
    }
    return std::nullopt;
}

void JobQueue::UpdateStatus(size_t id, JobStatus status, LONGLONG bytesDone) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs) {
        if (j.id == id) {
            j.status    = status;
            j.bytesDone = bytesDone;
            return;
        }
    }
}

void JobQueue::SetError(size_t id, const std::wstring& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs) {
        if (j.id == id) {
            j.status   = JobStatus::Error;
            j.errorMsg = msg;
            return;
        }
    }
}

size_t JobQueue::TotalJobs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.size();
}

size_t JobQueue::CompletedJobs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = 0;
    for (auto& j : m_jobs) if (j.status == JobStatus::Completed) ++n;
    return n;
}

size_t JobQueue::ErrorJobs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = 0;
    for (auto& j : m_jobs) if (j.status == JobStatus::Error) ++n;
    return n;
}

bool JobQueue::IsEmpty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs)
        if (j.status == JobStatus::Pending || j.status == JobStatus::Copying)
            return false;
    return true;
}

std::vector<Job> JobQueue::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs;
}

void JobQueue::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.clear();
    m_nextId = 0;
}

} // namespace FileCopier
