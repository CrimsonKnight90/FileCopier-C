#pragma once
#include <vector>
#include <memory>
#include "Job.h"

class JobManager {
public:
    int AddJob(const std::wstring& src, const std::wstring& dst);
    void StartNext();
    void CancelJob(int id);
    void PauseJob(int id);

    std::vector<std::shared_ptr<Job>> GetJobs();

private:
    std::vector<std::shared_ptr<Job>> jobs;
    int nextId = 1;
};