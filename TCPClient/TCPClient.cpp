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

namespace zm 
{
struct TCPClient::Impl
{
  Impl(EventLoopGroup& event_loop, EventWorkerGroup& worker_group, const std::string& ip, uint16_t port, int high_mark)
    : connected(false)
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

  ~Impl()
  {
    if (connected)
	  {
      shutdownReceive();
      while (connected && !sendBuffer.isEmpty())
      {
        // 保证数据正常发送完毕
      }
      shutdownSend();
	  }
  }

  inline bool connect(uint16_t timeout);
  inline void connectNB();
  inline void shutdownReceive();
  inline void shutdownSend();
  inline void close();
  bool isConnected();

  void
    setCallback(const callback::TCPClientCallback& cl)
  {
    callback.reset(new callback::TCPClientCallback(cl));
  }

  inline int sendImpl(byte* buffer, int length, const Poco::Timespan& timeout);
  inline bool sendInLoop(byte* buffer, int length);
  inline void sendMessage(byte* buffer, int length, const Poco::Timespan& timeout = Poco::Timespan(0, 0));

  void
    initHandler()
  {
    send = true;
    receive = true;

    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::ErrorNotification>(*this, &Impl::onError));
    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::IdleNotification>(*this, &Impl::onIdle));
    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::TimeoutNotification>(*this, &Impl::onTimeout));
    loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::ShutdownNotification>(*this, &Impl::onShutdown));

    loop->reactor.wakeUp();
  }

  void
    clearHandler()
  {
    if (loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
    {
      loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    }

    if (loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable)))
    {
      loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
    }

    loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::ErrorNotification>(*this, &Impl::onError));
    loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::IdleNotification>(*this, &Impl::onIdle));
    loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::TimeoutNotification>(*this, &Impl::onTimeout));
    loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::ShutdownNotification>(*this, &Impl::onShutdown));
  }

  void
    clearBuffer()
  {
    sendBuffer.drain();
    receiveBuffer.drain();
  }

private:

  void
    onReadable(bool& b)
  {
    if (b)
    {
      // 当 SendBuffer 变为可读状态， 为 Socket添加写监听事件
      assert(!loop.reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)));
      loop->reactor.addEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
      loop->reactor.wakeUp();
    }
    else
    {
      // 当 SendBuffer中不存在可用数据， 移除Socket可写监听， 防止busy loop
      if (loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
      {
        loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
      }
    }
  }

  void
    onWritable(Poco::Net::WritableNotification* n)
  {
    printf("%s\n", __func__);
    n->release();

    // 使用非阻塞连接 socket 变为可写状态 表示连接建立
    if (!connected)
    {
      socket.setKeepAlive(true);
      socket.setBlocking(false);
      loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
      connected = true;

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
        printf("Sent: %d\n", send);
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
      std::cerr << ex.displayText() << std::endl;
      close();
    }
  }

  void
    onReadable(Poco::Net::ReadableNotification* n)
  {
    printf("%s\n", __func__);
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
          workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->messageCallback, tcp_connection_ptr, std::move(buffer)));
        }
      }
      receiveBuffer.drain();
    }
    catch (const Poco::TimeoutException& ex)
    {
      std::cerr << __LINE__ << ":" << ex.displayText() << std::endl;
      close();
    }
    catch (const Poco::Exception& ex)
    {
      std::cerr << __LINE__ << ":" << ex.displayText() << std::endl;
      close();
    }
  }

  void
    onError(Poco::Net::ErrorNotification* n)
  {
    n->release();
    close();
    printf("%s\n", __func__);
  }

  void
    onShutdown(Poco::Net::ShutdownNotification* n)
  {
    n->release();
    close();
    printf("%s\n", __func__);
  }

  void
    onIdle(Poco::Net::IdleNotification* n)
  {
    n->release();
    printf("%s\n", __func__);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  void
    onTimeout(Poco::Net::TimeoutNotification* n)
  {
    std::cout << std::this_thread::get_id() << ":" << std::endl;
    n->release();
    if (!sendBuffer.isEmpty())
    {
      assert(loop.reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)));
    }
  }

private:
  friend class TCPClient;

  bool connected;
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

bool
  TCPClient::Impl::connect(uint16_t timeout)
{
  if (!connected)
  {
    try
    {
      clearBuffer();
      socket.connect(address, Poco::Timespan(timeout, 0));
      connected = true;
      initHandler();
    }
    catch (const Poco::Exception& ex)
    {
      std::cerr << __LINE__ << ":" << ex.displayText() << std::endl;
    }
  }
  return connected;
}

void
  TCPClient::Impl::connectNB()
{
  if (!connected)
  {
    clearBuffer();

    socket.connectNB(address);
    initHandler();
  }
}

void
  TCPClient::Impl::shutdownReceive()
{
  if (connected && receive)
  {
    receive = false;
    socket.shutdownReceive();
    if (loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable)))
    {
      loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::ReadableNotification>(*this, &Impl::onReadable));
    }
    connected = send | receive;
  }
  if (!connected)
  {
    close();
  }
}

void
  TCPClient::Impl::shutdownSend()
{
  if (connected && send)
  {
    send = false;
    socket.shutdownSend();
    if (loop->reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)))
    {
      loop->reactor.removeEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable));
    }
    connected = send | receive;
  }
  if (!connected)
  {
    close();
  }
}

void
  TCPClient::Impl::close()
{
  clearHandler();
  clearBuffer();
  if (connected)
  {
    socket.close();
    connected = false;
    if (callback)
    {
      if (const auto tcp_connection_ptr = client.lock())
      {
        workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->closeCallback, tcp_connection_ptr));
      }
    }
  }
}

bool
  TCPClient::Impl::isConnected()
{
  return connected;
}

void
  TCPClient::Impl::sendMessage(byte* buffer, int length, const Poco::Timespan& timeout)
{
  if (!isConnected())
    return;
  assert(length > 0);

  if (send)
  {
    try
    {
      // 先尝试同步非阻塞发送
      const auto sent = sendImpl(buffer, length, 0);
      const auto left_byte = length - sent;

      if (left_byte > 0)
      {
        // 如果数据未全部发送， 将剩余数据放入sendbuffer，后台 select fd writable 事件后，自动发送剩余数据
        // 异步非阻塞发送
        if (!sendInLoop(buffer + sent, left_byte))
        {
          // 对于超出发送缓冲区， 发送部分数据的情况
          if (sent != 0)
          {
            while (!sendInLoop(buffer + sent, left_byte))
            {
              printf("Partly send---------\n");
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
      std::cerr << __LINE__ << ":" << ex.displayText() << std::endl;
      close();
    }
  }
}

int
  TCPClient::Impl::sendImpl(byte* buffer, int length, const Poco::Timespan& timeout)
{
  auto sent = 0;
  if (sendBuffer.isEmpty() && socket.poll(timeout, Poco::Net::Socket::SelectMode::SELECT_WRITE))
  {
    sent = socket.sendBytes(buffer, length);
    if (sent < 0)
    {
      close();
      throw Poco::Exception("sent < 0");
    }
  }
  return sent;
}

bool
  TCPClient::Impl::sendInLoop(byte* buffer, int length)
{
  if (sendBuffer.available() >= size_t(length))
  {
    sendBuffer.write(reinterpret_cast<char*>(buffer), length);
    loop->reactor.wakeUp();
    assert(reactor.hasEventHandler(socket, Poco::Observer<Impl, Poco::Net::WritableNotification>(*this, &Impl::onWritable)));
    return true;
  }

  if (callback)
  {
    if (const auto tcp_connection_ptr = client.lock())
    {
      workers.dispatchEvent(tcp_connection_ptr, std::bind(callback->highWaterMarkCallback, tcp_connection_ptr, sendBuffer.used()));
    }
  }

  return false;
}

TCPClient::TCPClient(EventLoopGroup& event_loop, EventWorkerGroup& worker_group, const std::string& ip, uint16_t port, int max_high_mark)
  : id_(Impl::sn.fetch_add(1))
  , impl_(new Impl(event_loop, worker_group, ip, port, max_high_mark))
{}

TCPClient::~TCPClient()
{
  delete impl_;
}

void
  TCPClient::enableKeepAlive(bool enable)
{
  impl_->socket.setKeepAlive(enable);
}

void
  TCPClient::init(const callback::TCPClientCallback& cl)
{
  impl_->client = this->shared_from_this();
  impl_->setCallback(cl);
}

void
  TCPClient::shutdownReceive()
{
  impl_->shutdownReceive();
}

void
  TCPClient::shutdownSend()
{
  impl_->shutdownSend();
}

void
  TCPClient::close()
{
  impl_->close();
}

bool
  TCPClient::connect(uint16_t timeout)
{
  try
  {
    impl_->connect(timeout);
  }
  catch (const Poco::Exception& ex)
  {
    std::cerr << __LINE__ << ":" << ex.displayText() << std::endl;
    return false;
  }

  return true;
}

void
  TCPClient::connectNB()
{
  impl_->connectNB();
}

bool
  TCPClient::isConnected() const
{
  return impl_->isConnected();
}

std::string
  TCPClient::peerAddress() const
{
  if (!isConnected())
    return std::string{""};

  return impl_->socket.peerAddress().toString();
}

void
  TCPClient::sendMessage(byte* buffer, int size, int offset)
{
  assert(size - offset > 0);
  return impl_->sendMessage(buffer + offset, size - offset);
}
}
