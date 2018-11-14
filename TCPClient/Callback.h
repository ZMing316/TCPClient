#pragma once

/*!
 * \file Callback.h
 *
 * \author ZMing
 * \date 2018
 *
 * 
 */

#include <functional>
#include <memory>
#include <vector>


namespace zm
{
class TCPClient;

namespace callback
{

typedef std::shared_ptr<TCPClient> TcpConnectionPtr;
typedef std::function<void()> TimerCallback;
typedef std::function<void (const TcpConnectionPtr&)> ConnectionCallback;
typedef std::function<void (const TcpConnectionPtr&)> CloseCallback;
typedef std::function<void (const TcpConnectionPtr&)> WriteCompleteCallback;
typedef std::function<void (const TcpConnectionPtr&, size_t)> HighWaterMarkCallback;
typedef std::function<void (const TcpConnectionPtr&,
                            std::vector<unsigned char>)> MessageCallback;

struct TCPClientCallback
{
  ConnectionCallback connectionCallback;
  MessageCallback messageCallback;
  WriteCompleteCallback writeCompleteCallback;
  HighWaterMarkCallback highWaterMarkCallback;
  CloseCallback closeCallback;
};
}
}
