#include "../include/AppController.h"

int AppController::CreateCopyJob(const std::wstring& src, const std::wstring& dst)
{
    return jobManager.AddJob(src, dst);
}

void AppController::Start()
{
    jobManager.StartNext();
}

void AppController::Cancel(int id)
{
    jobManager.CancelJob(id);
}

void AppController::Pause(int id)
{
    jobManager.PauseJob(id);
}

JobManager& AppController::GetJobManager()
{
    return jobManager;
}