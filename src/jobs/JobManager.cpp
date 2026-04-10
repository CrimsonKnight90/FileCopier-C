#include "../../include/jobs/JobManager.h"
#include "../../include/core/FileCopyEngine.h"
#include <thread>

int JobManager::AddJob(const std::wstring& src, const std::wstring& dst)
{
    auto job = std::make_shared<Job>();
    job->id = nextId++;
    job->source = src;
    job->destination = dst;

    jobs.push_back(job);
    return job->id;
}

void JobManager::StartNext()
{
    for (auto& job : jobs) {
        if (job->status == JobStatus::Pending) {

            std::thread([job]() {
                CopyFileStable(*job);
            }).detach();

            return;
        }
    }
}

void JobManager::CancelJob(int id)
{
    for (auto& job : jobs)
        if (job->id == id)
            job->cancelRequested = true;
}

void JobManager::PauseJob(int id)
{
    for (auto& job : jobs)
        if (job->id == id)
            job->pauseRequested = !job->pauseRequested;
}

std::vector<std::shared_ptr<Job>> JobManager::GetJobs()
{
    return jobs;
}