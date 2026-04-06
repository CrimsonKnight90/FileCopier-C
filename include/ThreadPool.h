#pragma once
#include <windows.h>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

namespace FileCopier {

using Task = std::function<void()>;

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Encola una tarea y retorna un future para obtener el resultado
    template<typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    void Pause();
    void Resume();
    void WaitAll();       // bloquea hasta que la cola esté vacía
    void Shutdown();

    size_t ActiveCount()  const { return m_active.load(); }
    size_t PendingCount() const;
    size_t ThreadCount()  const { return m_workers.size(); }

private:
    void WorkerLoop();

    std::vector<std::thread>          m_workers;
    std::queue<Task>                  m_tasks;
    mutable std::mutex                m_mutex;
    std::condition_variable           m_cv;
    std::condition_variable           m_cvDone;
    std::atomic<bool>                 m_stop   {false};
    std::atomic<bool>                 m_paused {false};
    std::atomic<size_t>               m_active {0};
};

// ── Template implementation ──────────────────────────────────────────────────
template<typename F, typename... Args>
auto ThreadPool::Enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using RetT = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<RetT()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<RetT> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) throw std::runtime_error("ThreadPool is stopped");
        m_tasks.emplace([task]{ (*task)(); });
    }
    m_cv.notify_one();
    return result;
}

} // namespace FileCopier
