#pragma once

/*!
 * \file TCPClient.h  
 *
 * \author ZMing
 * \date 八月 2018
 *
 * 
 */


#include <cstdint>
#include <future>
#include <string>

#include "DLL.h"
#include "Noncopyable.h"

namespace sduept
{
class EventWorkerGroup;

namespace callback
{
  struct TCPClientCallback;
}

class EventLoopGroup;

constexpr auto K_4K = 4096;

class TCPClient;

template  class API std::weak_ptr<TCPClient>;
template  class API std::enable_shared_from_this<TCPClient>;

class API TCPClient final : public Noncopyable, public std::enable_shared_from_this<TCPClient>
{
public:
  using byte = unsigned char;
  TCPClient(EventLoopGroup& event_loop, EventWorkerGroup& worker_group, const std::string& ip, uint16_t port, int max_high_mark = K_4K * 256);

  ~TCPClient();
  // 应用层如果存在心跳帧 通常不需要传输层keepAlive机制(默认开启)
  void enableKeepAlive(bool enable = true);
  void init(const callback::TCPClientCallback& cl);

  inline uint64_t id() const;
  void shutdownReceive();
  void shutdownSend();
  void close();
  bool connect(uint16_t timeout /*second*/);
  void connectNB();

  void sendMessage(byte* buffer, int size, int offset = 0);

  bool isConnected() const;
  std::string peerAddress() const;
private:
  uint64_t id_;
  struct Impl;
  Impl* impl_;
};

uint64_t TCPClient::id() const
{
    return id_;
}
}
