#include "EventWorker.h"

#include "Common.h"


namespace sduept
{
TaskNotification::TaskNotification(std::function<void()> func, std::string name)
  : func_(std::move(func))
  , name_(std::move(name))
{}

void
  TaskNotification::operator()() const noexcept
{
  try
  {
    func_();
  }
  catch (const std::exception& ex)
  {
    MY_HANDLE(ex.what());
  }
}

EventWorker::EventWorker(Poco::NotificationQueue& queue)
  : queue_(queue)
{ }

void
  EventWorker::run()
{
  Poco::Notification::Ptr notification_ptr = queue_.waitDequeueNotification();

  while (!notification_ptr.isNull())
  {
    const auto func = dynamic_cast<TaskNotification*>(notification_ptr.get());
    if (func)
    {
      (*func)();
    }
    notification_ptr = queue_.waitDequeueNotification();
  }
}
}
