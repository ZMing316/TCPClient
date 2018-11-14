#pragma once

#include <functional>
#include <memory>

#include "Callback.h"
#include "DLL.h"

namespace zm
{
class API EventWorkerGroup final
{
public:
  EventWorkerGroup(int worker_num);
  ~EventWorkerGroup();
  void dispatchEvent(const callback::TcpConnectionPtr&, std::function<void()> functor);
  void start();
  void stopAll();

private:
  struct Impl;
  Impl* impl_;
};
}
