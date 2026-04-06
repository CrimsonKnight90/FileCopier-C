#pragma once
#include <windows.h>
#include <functional>
#include <vector>
#include <mutex>
#include <string>

namespace FileCopier {

// Eventos que el motor emite hacia la UI
struct EventFileStarted   { std::wstring path; LONGLONG totalBytes; };
struct EventFileProgress  { std::wstring path; LONGLONG bytesCopied; LONGLONG totalBytes; double speedMBps; };
struct EventFileCompleted { std::wstring path; bool success; };
struct EventError         { std::wstring path; std::wstring message; DWORD code; };
struct EventSpeedUpdate   { double speedMBps; LONGLONG bytesTotal; LONGLONG bytesDone; };
struct EventQueueFinished { size_t total; size_t succeeded; size_t failed; };

// Bus de eventos simple basado en callbacks
class EventBus {
public:
    static EventBus& Instance();

    // Registro de listeners
    void OnFileStarted  (std::function<void(const EventFileStarted&)>   cb);
    void OnFileProgress (std::function<void(const EventFileProgress&)>  cb);
    void OnFileCompleted(std::function<void(const EventFileCompleted&)> cb);
    void OnError        (std::function<void(const EventError&)>         cb);
    void OnSpeedUpdate  (std::function<void(const EventSpeedUpdate&)>   cb);
    void OnQueueFinished(std::function<void(const EventQueueFinished&)> cb);

    // Emisión de eventos (llamada desde el motor, hilo worker)
    void Emit(const EventFileStarted&)   const;
    void Emit(const EventFileProgress&)  const;
    void Emit(const EventFileCompleted&) const;
    void Emit(const EventError&)         const;
    void Emit(const EventSpeedUpdate&)   const;
    void Emit(const EventQueueFinished&) const;

    void ClearAll();

private:
    EventBus() = default;
    mutable std::mutex m_mutex;

    std::vector<std::function<void(const EventFileStarted&)>>   m_onStarted;
    std::vector<std::function<void(const EventFileProgress&)>>  m_onProgress;
    std::vector<std::function<void(const EventFileCompleted&)>> m_onCompleted;
    std::vector<std::function<void(const EventError&)>>         m_onError;
    std::vector<std::function<void(const EventSpeedUpdate&)>>   m_onSpeed;
    std::vector<std::function<void(const EventQueueFinished&)>> m_onFinished;
};

} // namespace FileCopier
