#include "../../include/Jobs.h"

namespace FileCopier {

JobState::JobState(size_t jobId) : m_id(jobId) {}

void JobState::SetStatus(JobStatus s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = s;
}

void JobState::SetProgress(LONGLONG bytes, LONGLONG total) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bytesDone = bytes;
    m_total     = total;
}

void JobState::SetError(const std::wstring& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status   = JobStatus::Error;
    m_errorMsg = msg;
}

JobStatus JobState::Status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

LONGLONG JobState::BytesDone() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bytesDone;
}

LONGLONG JobState::TotalBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_total;
}

std::wstring JobState::ErrorMsg() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_errorMsg;
}

} // namespace FileCopier
