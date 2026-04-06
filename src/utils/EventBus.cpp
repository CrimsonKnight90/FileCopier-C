#include "../../include/EventBus.h"

namespace FileCopier {

EventBus& EventBus::Instance() {
    static EventBus inst;
    return inst;
}

void EventBus::OnFileStarted  (std::function<void(const EventFileStarted&)>   cb) { std::lock_guard<std::mutex> l(m_mutex); m_onStarted.push_back(std::move(cb)); }
void EventBus::OnFileProgress (std::function<void(const EventFileProgress&)>  cb) { std::lock_guard<std::mutex> l(m_mutex); m_onProgress.push_back(std::move(cb)); }
void EventBus::OnFileCompleted(std::function<void(const EventFileCompleted&)> cb) { std::lock_guard<std::mutex> l(m_mutex); m_onCompleted.push_back(std::move(cb)); }
void EventBus::OnError        (std::function<void(const EventError&)>         cb) { std::lock_guard<std::mutex> l(m_mutex); m_onError.push_back(std::move(cb)); }
void EventBus::OnSpeedUpdate  (std::function<void(const EventSpeedUpdate&)>   cb) { std::lock_guard<std::mutex> l(m_mutex); m_onSpeed.push_back(std::move(cb)); }
void EventBus::OnQueueFinished(std::function<void(const EventQueueFinished&)> cb) { std::lock_guard<std::mutex> l(m_mutex); m_onFinished.push_back(std::move(cb)); }

void EventBus::Emit(const EventFileStarted& e)   const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onStarted)   f(e); }
void EventBus::Emit(const EventFileProgress& e)  const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onProgress)  f(e); }
void EventBus::Emit(const EventFileCompleted& e) const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onCompleted) f(e); }
void EventBus::Emit(const EventError& e)         const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onError)     f(e); }
void EventBus::Emit(const EventSpeedUpdate& e)   const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onSpeed)     f(e); }
void EventBus::Emit(const EventQueueFinished& e) const { std::lock_guard<std::mutex> l(m_mutex); for (auto& f : m_onFinished)  f(e); }

void EventBus::ClearAll() {
    std::lock_guard<std::mutex> l(m_mutex);
    m_onStarted.clear();
    m_onProgress.clear();
    m_onCompleted.clear();
    m_onError.clear();
    m_onSpeed.clear();
    m_onFinished.clear();
}

} // namespace FileCopier
