#pragma once
#include "jobs/JobManager.h"

class AppController {
public:
    int CreateCopyJob(const std::wstring& src, const std::wstring& dst);
    void Start();
    void Cancel(int id);
    void Pause(int id);

    JobManager& GetJobManager();

private:
    JobManager jobManager;
};