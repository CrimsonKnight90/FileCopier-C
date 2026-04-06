#include "../../include/ThreadPool.h"

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i)
        m_workers.emplace_back(&ThreadPool::WorkerLoop, this);
}

ThreadPool::~ThreadPool() {
    Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
void ThreadPool::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stop || (!m_paused && !m_tasks.empty());
            });

            if (m_stop && m_tasks.empty()) return;
            if (m_paused) continue;

            task = std::move(m_tasks.front());
            m_tasks.pop();
            ++m_active;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_active;
        }
        m_cvDone.notify_all();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void ThreadPool::Pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
}

void ThreadPool::Resume() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = false;
    }
    m_cv.notify_all();
}

// ─────────────────────────────────────────────────────────────────────────────
void ThreadPool::WaitAll() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cvDone.wait(lock, [this] {
        return m_tasks.empty() && m_active == 0;
    });
}

void ThreadPool::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& w : m_workers)
        if (w.joinable()) w.join();
    m_workers.clear();
}

size_t ThreadPool::PendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

} // namespace FileCopier
