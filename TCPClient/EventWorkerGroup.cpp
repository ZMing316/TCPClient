#include "EventWorkerGroup.h"

#include <iostream>

#include <Poco/NotificationQueue.h>
#include <Poco/ThreadPool.h>
#include <Poco/Format.h>

#include "Common.h"
#include "EventWorker.h"
#include "TCPClient.h"


namespace sduept
{
struct EventWorkerGroup::Impl
{
  Impl(int num)
    : notification_queues(num)
    , workers_pool("WorkerThreadPool", num)
  {
    for (auto& queue : notification_queues)
    {
      workers.emplace_back(EventWorker(queue));
    }
  }

  std::vector<Poco::NotificationQueue> notification_queues;
  std::vector<EventWorker> workers;
  Poco::ThreadPool workers_pool;
};

EventWorkerGroup::EventWorkerGroup(int worker_num)
{
  impl_ = new Impl(worker_num);
}

EventWorkerGroup::~EventWorkerGroup()
{
  delete impl_;
}

void
  EventWorkerGroup::dispatchEvent(const callback::TcpConnectionPtr& client, std::function<void()> functor)
{
  // 根据 TCP ID 绑定 Callback  工作线程
  constexpr auto K_MAX_UNPROCESSED_NOTIFICATION_UPPER = 5;
  const auto queue_sn = client->id() % impl_->workers.size();
  if (impl_->notification_queues[queue_sn].size() > K_MAX_UNPROCESSED_NOTIFICATION_UPPER)
  {
      std::cerr << Poco::format(u8"[%04z]号队列被占用!\n", queue_sn);
  }
  impl_->notification_queues[queue_sn].enqueueNotification(new TaskNotification(std::move(functor)));

}

void
  EventWorkerGroup::start()
{
  for (auto& worker : impl_->workers)
  {
    impl_->workers_pool.start(worker);
  }

}

void
  EventWorkerGroup::stopAll()
{
  for (auto& queue : impl_->notification_queues)
  {
    queue.wakeUpAll();
  }

  impl_->workers_pool.joinAll();
}
}
