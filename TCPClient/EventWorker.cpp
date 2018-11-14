#include "EventWorker.h"

#include <iostream>

namespace zm
{
TaskNotification::TaskNotification(std::function<void()> func, std::string name)
  : func_(std::move(func))
  , name_(std::move(name))
{}

void
  TaskNotification::operator()() noexcept
{
  try
  {
    func_();
  }
  catch (const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
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
    const auto func = static_cast<TaskNotification*>(notification_ptr.get());
    (*func)();
    notification_ptr = queue_.waitDequeueNotification();
  }
}
}
