#include "TCPClient.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <functional>

#include <Poco/Delegate.h>
#include <Poco/Net/SocketNotification.h>
#include <Poco/Net/SocketReactor.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/NetException.h>
#include <Poco/Observer.h>

#include "Callback.h"
#include "EventLoopGroup.h"
#include "EventWorkerGroup.h"
#include "Common.h"

namespace sduept
{
struct TCPClient::Impl
{
  Impl(EventLoopGroup& event_loop, EventWorkerGroup& worker_group, const std::string& ip, uint16_t port, int high_mark);
  ~Impl();

  inline bool connect(uint16_t timeout);
  inline void connectNB();
  inline void shutdownReceive();
  inline void shutdownSend();
  inline void close();
  bool isConnected() const;

  void setCallback(const callback::TCPClientCallback& cl);

  inline int sendImpl(const byte* buffer, int length, const Poco::Timespan& timeout);
  inline bool sendInLoop(const byte* buffer, int length);
  inline void sendMessage(const byte* buffer,
                          int length,
                          const Poco::Timespan& timeout = Poco::Timespan(0, 0),
                          bool in_loop = true);

  void initHandler();

  void clearHandler();

  void clearBuffer();

  inline Impl* self()
  {
    return this;
  }

private:

  void onReadable(bool& b);

  void onWritable(Poco::Net::WritableNotification* n);

  void onReadable(Poco::Net::ReadableNotification* n);

  void onError(Poco::Net::ErrorNotification* n);

  void onShutdown(Poco::Net::ShutdownNotification* n);

  void onIdle(Poco::Net::IdleNotification* n);

  void onTimeout(Poco::Net::TimeoutNotification* n);

private:
  friend class TCPClient;

  std::atomic<bool> connected;
  bool over{false};
  bool send;
  bool receive;
  Poco::Net::SocketAddress address;
  Poco::Net::StreamSocket socket;
  Poco::FIFOBuffer sendBuffer;
  Poco::FIFOBuffer receiveBuffer;
  std::unique_ptr<callback::TCPClientCallback> callback{nullptr};
  std::weak_ptr<TCPClient> client{};
  std::unique_ptr<EventLoopGroup::Loop> loop;
  EventWorkerGroup& workers;
  static std::atomic<uint64_t> sn;
};

std::atomic<uint64_t> TCPClient::Impl::sn(0);

TCPClient::Impl::Impl(EventLoopGroup& event_loop,
                      EventWorkerGroup& worker_group,
                      const std::string& ip,
                      uint16_t port,
                      int high_mark): connected(false)
                                  , send(true)
                                  , receive(true)
                                  , address(ip, port)
                                  , sendBuffer(high_mark)
                                  , receiveBuffer(K_4K)
                                  , loop(event_loop.reactor())
                                  , workers(worker_group)
{
  sendBuffer.setNotify(true);
  sendBuffer.readable += Poco::delegate<Impl>(this, &Impl::onReadable);
}

TCPClient::Impl::~Impl()
{
  if (connected.load(std::memory_order_acquire))
  {
    shutdownReceive();
    while (connected.load(std::memory_order_acquire) && !sendBuffer.isEmpty())
    {
      // 保证数据正常发送完毕
    }
    shutdownSend();
  }
}

void TCPClient::Impl::setCallback(const callback::TCPClientCallback& cl)
{
  callback.reset(new callback::TCPClientCallback(cl));
}

void TCPClient::Impl::initHandler()
{
  send = true;
  receive = true;
  over = false;

  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::ErrorNotification>(*this, &Impl::onError)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::ErrorNotification>(*this, &Impl::onError));
  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::IdleNotification>(*this, &Impl::onIdle)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::IdleNotification>(*this, &Impl::onIdle));
  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::TimeoutNotification>(*this, &Impl::onTimeout)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::TimeoutNotification>(*this, &Impl::onTimeout));
  if (!loop->reactor.hasEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::ShutdownNotification>(*this, &Impl::onShutdown)))
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::ShutdownNotification>(*this, &Impl::onShutdown));

  loop->reactor.wakeUp();
}

void TCPClient::Impl::clearHandler()
{
  if (loop->reactor.hasEventHandler(socket,
                                    Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
  {
    loop->reactor.removeEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
  }

  if (loop->reactor.hasEventHandler(socket,
                                    Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable)))
  {
    loop->reactor.removeEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
  }

  loop->reactor.removeEventHandler(socket,
                                   Poco::Observer<Impl, Poco::Net::ErrorNotification>(*this, &Impl::onError));
  loop->reactor.removeEventHandler(socket,
                                   Poco::Observer<Impl, Poco::Net::IdleNotification>(*this, &Impl::onIdle));
  loop->reactor.removeEventHandler(socket,
                                   Poco::Observer<Impl, Poco::Net::TimeoutNotification>(*this, &Impl::onTimeout));
  loop->reactor.removeEventHandler(socket,
                                   Poco::Observer<Impl, Poco::Net::ShutdownNotification>(*this, &Impl::onShutdown));
}

void TCPClient::Impl::clearBuffer()
{
  sendBuffer.drain();
  receiveBuffer.drain();
}

void TCPClient::Impl::onReadable(bool& b)
{
  if (b)
  {
    // 当 SendBuffer 变为可读状态， 为 Socket添加写监听事件
    assert(!loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::
      onWritable)));
    loop->reactor.addEventHandler(socket,
                                  Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    loop->reactor.wakeUp();
  }
  else
  {
    // 当 SendBuffer中不存在可用数据， 移除Socket可写监听， 防止busy loop
    if (loop->reactor.hasEventHandler(socket,
                                      Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
    {
      loop->reactor.removeEventHandler(socket,
                                       Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    }
  }
}

void TCPClient::Impl::onWritable(Poco::Net::WritableNotification* n)
{
  n->release();

  // 使用非阻塞连接 socket 变为可写状态 表示连接建立
  if (!connected.load(std::memory_order_acquire))
  {
    socket.setKeepAlive(true);
    socket.setBlocking(false);
    loop->reactor.removeEventHandler(socket,
                                     Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    connected.store(true, std::memory_order_release);

    if (callback)
    {
      if (const auto tcp_connection_ptr = client.lock())
      {
        workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->connectionCallback, tcp_connection_ptr));
      }
    }

    return;
  }

  try
  {
    if (!sendBuffer.isEmpty())
    {
      const auto send = socket.sendBytes(sendBuffer);
      if (send < 0)
      {
        close();
      }
      else
      {
        if (sendBuffer.isEmpty())
        {
          if (callback)
          {
            if (const auto tcp_connection_ptr = client.lock())
            {
              workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->writeCompleteCallback, tcp_connection_ptr));
            }
          }
        }
      }
    }
  }
  catch (const Poco::Exception& ex)
  {
    MY_HANDLE(ex.displayText());
    close();
  }
}

void TCPClient::Impl::onReadable(Poco::Net::ReadableNotification* n)
{
  n->release();

  try
  {
    const auto ret = socket.receiveBytes(receiveBuffer);
    if (ret == 0)
    {
      printf("Graceful close by peer.\n");
      close();
      return;
    }

    if (callback)
    {
      if (const auto tcp_connection_ptr = client.lock())
      {
        std::vector<unsigned char> buffer(receiveBuffer.begin(), receiveBuffer.begin() + receiveBuffer.used());
        workers.dispatchEvent(tcp_connection_ptr,
                              std::bind(callback->messageCallback, tcp_connection_ptr, std::move(buffer)));
      }
    }
    receiveBuffer.drain();
  }
  catch (const Poco::TimeoutException& ex)
  {
    MY_HANDLE(ex.displayText());
    close();
  }
  catch (const Poco::Exception& ex)
  {
    MY_HANDLE(ex.displayText());
    close();
  }
}

void TCPClient::Impl::onError(Poco::Net::ErrorNotification* n)
{
  n->release();
  close();
}

void TCPClient::Impl::onShutdown(Poco::Net::ShutdownNotification* n)
{
  n->release();
  close();
}

void TCPClient::Impl::onIdle(Poco::Net::IdleNotification* n)
{
  n->release();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void TCPClient::Impl::onTimeout(Poco::Net::TimeoutNotification* n)
{
  n->release();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  assert(!loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::
    onWritable)));
}

bool TCPClient::Impl::connect(uint16_t timeout)
{
  if (!connected.load(std::memory_order_acquire))
  {
    try
    {
      clearBuffer();
      socket.connect(address, Poco::Timespan(timeout, 0));
      connected.store(true, std::memory_order_release);
      initHandler();
    }
    catch (const Poco::Exception& ex)
    {
      MY_HANDLE(ex.displayText());
    }
  }
  return connected.load(std::memory_order_acquire);
}

void TCPClient::Impl::connectNB()
{
  if (!connected.load(std::memory_order_acquire))
  {
    clearBuffer();
    try
    {
      socket.connectNB(address);
    }
    catch (...)
    {}
    initHandler();
  }
}

void TCPClient::Impl::shutdownReceive()
{
  if (connected.load(std::memory_order_acquire) && receive)
  {
    receive = false;
    socket.shutdownReceive();
    if (loop->reactor.hasEventHandler(socket,
                                      Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable)))
    {
      loop->reactor.removeEventHandler(socket,
                                       Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
    }
    connected.store(send | receive, std::memory_order_release);
  }
  if (!connected.load(std::memory_order_acquire))
  {
    close();
  }
}

void TCPClient::Impl::shutdownSend()
{
  if (connected.load(std::memory_order_acquire) && send)
  {
    send = false;
    socket.shutdownSend();
    if (loop->reactor.hasEventHandler(socket,
                                      Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
    {
      loop->reactor.removeEventHandler(socket,
                                       Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    }
    connected.store(send | receive, std::memory_order_release);
  }
  if (!connected.load(std::memory_order_acquire))
  {
    close();
  }
}

void TCPClient::Impl::close()
{
  clearHandler();
  clearBuffer();
  if (connected.exchange(false))
  {
    socket.close();
    if (callback)
    {
      if (const auto tcp_connection_ptr = client.lock())
      {
        workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->closeCallback, tcp_connection_ptr));
      }
    }
  }
}

bool TCPClient::Impl::isConnected() const
{
  return connected.load(std::memory_order_acquire);
}

void TCPClient::Impl::sendMessage(const byte* buffer, int length, const Poco::Timespan& timeout, bool in_loop)
{
  if (!isConnected())
    return;
  assert(length > 0);

  if (send)
  {
    try
    {
      // 先尝试同步非阻塞发送
      const auto sent = in_loop ? 0 : sendImpl(buffer, length, timeout);
      const auto left_byte = length - sent;

      if (left_byte > 0)
      {
        // 如果数据未全部发送， 将剩余数据放入sendBuffer，后台 select fd writable 事件后，自动发送剩余数据
        // 异步非阻塞发送
        if (!sendInLoop(buffer + sent, left_byte))
        {
          // 对于超出发送缓冲区， 发送部分数据的情况
          if (sent != 0)
          {
            while (!sendInLoop(buffer + sent, left_byte))
            {
              std::this_thread::yield();
            }
          }
        }
      }
      else
      {
        if (callback)
        {
          if (const auto tcp_connection_ptr = client.lock())
          {
            workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->writeCompleteCallback, tcp_connection_ptr));
          }
        }
      }
    }
    catch (const Poco::Exception& ex)
    {
      MY_HANDLE(ex.message());
      close();
    }
  }
}

int TCPClient::Impl::sendImpl(const byte* buffer, int length, const Poco::Timespan& timeout)
{
  auto sent = 0;
  if (sendBuffer.isEmpty() && socket.poll(timeout, Poco::Net::Socket::SelectMode::SELECT_WRITE))
  {
    sent = socket.sendBytes(buffer, length);
    if (sent < 0)
    {
      close();
      throw std::runtime_error("Send < 0");
    }
  }
  return sent;
}

bool TCPClient::Impl::sendInLoop(const byte* buffer, int length)
{
  if (sendBuffer.available() >= size_t(length))
  {
    const auto sent = sendBuffer.write(reinterpret_cast<const char*>(buffer), length);
    loop->reactor.wakeUp();
    assert(loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::
      onWritable)));
    return int(sent) == length;
  }


  if (callback)
  {
    if (const auto tcp_connection_ptr = client.lock())
    {
      // 不采用分发直接回调
      std::bind(callback->highWaterMarkCallback, tcp_connection_ptr, sendBuffer.used())();
    }
  }

  return false;
}

TCPClient::TCPClient(EventLoopGroup& event_loop,
                     EventWorkerGroup& worker_group,
                     const std::string& ip,
                     uint16_t port,
                     int max_high_mark) : id_(Impl::sn.fetch_add(1))
                                      , impl_(new Impl(event_loop, worker_group, ip, port, max_high_mark))
{}

TCPClient::~TCPClient()
{
  delete impl_;
}

void TCPClient::enableKeepAlive(bool enable)
{
  impl_->socket.setKeepAlive(enable);
}

void TCPClient::init(const callback::TCPClientCallback& cl)
{
  impl_->client = this->shared_from_this();
  impl_->setCallback(cl);
}

void TCPClient::shutdownReceive()
{
  impl_->shutdownReceive();
}

void TCPClient::shutdownSend()
{
  impl_->shutdownSend();
}

void TCPClient::close()
{
  impl_->close();
}

bool TCPClient::connect(uint16_t timeout)
{
  try
  {
    impl_->connect(timeout);
  }
  catch (const Poco::Exception& ex)
  {
    MY_HANDLE(ex.what());
    return false;
  }

  return true;
}

void TCPClient::connectNB()
{
  impl_->connectNB();
}

bool TCPClient::isConnected() const
{
  return impl_->isConnected();
}

std::string TCPClient::peerAddress() const
{
  if (!isConnected())
    return std::string{""};

  return impl_->socket.peerAddress().toString();
}

void TCPClient::sendMessage(const byte* buffer, int size, int offset, bool in_loop)
{
  assert(size - offset > 0);
  impl_->sendMessage(buffer + offset, size - offset, in_loop);
}
}
