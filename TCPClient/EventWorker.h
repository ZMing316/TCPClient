#pragma once

#include <functional>

#include <Poco/NotificationQueue.h>
#include <Poco/Runnable.h>

namespace sduept
{
class TaskNotification : public Poco::Notification
{
public:
  TaskNotification(std::function<void()> func, std::string name = "");

  std::string
    name() const override
  {
    return name_;
  }

  void operator()() const noexcept;

protected:
  ~TaskNotification() override = default;
private:
  std::function<void()> func_;
  std::string name_;
};

class EventWorker : public Poco::Runnable
{
public:
  EventWorker(Poco::NotificationQueue& queue);

  void run() override;

private:
  Poco::NotificationQueue& queue_;
};
}
