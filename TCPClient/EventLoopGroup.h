#pragma once
/*!
 * \file EventLoopGroup.h
 *
 * \author ZMing
 * \date ���� 2018
 *
 * 
 */
#include <atomic>
#include <memory>
#include <functional>

#include "DLL.h"
#include "Noncopyable.h"

namespace Poco
{
namespace Net
{
class SocketReactor;
}

class ThreadPool;
}

namespace zm
{
class TCPClient;

class API EventLoopGroup final : public Noncopyable 
{
public:
  struct Loop : Noncopyable
  {
    Loop(Poco::Net::SocketReactor& r, std::atomic<uint16_t>& c);
    ~Loop();
    Poco::Net::SocketReactor& reactor;
    std::atomic<uint16_t>& count;
  };

  /**
   * \brief Init the event loop.
   * \param size The numbers of reactor to handle the tcp client.
   */
  explicit EventLoopGroup(int size);
  ~EventLoopGroup();
  std::unique_ptr<Loop> reactor();

  void loop();
  void quit();

private:
  struct Impl;
  Impl* impl_;
};
}
