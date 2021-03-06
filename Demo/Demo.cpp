#include <chrono>
#include <cstdio>
#include <map>
#include <iostream>


#include "EventLoopGroup.h"
#include "Callback.h"
#include "MessageBlockPacketization.h"
#include "WeakCallback.h"
#include "EventWorkerGroup.h"
#include "Common.h"
#include "TCPClient.h"

zm::callback::TcpConnectionPtr client_ptr{nullptr};
bool is_over = false;
bool run = true;

struct Test
{
  void
    fun(std::vector<unsigned char>&& hello) const
  {
    for (auto c : hello)
    {
      std::cout << c << std::endl;
    }
  }
};

auto test = std::make_shared<Test>();
std::map<std::string, std::unique_ptr<zm::MessageBlockPacketization<Test>>> connections;

void
  connection(const zm::callback::TcpConnectionPtr& cl)
{
  std::cout << std::this_thread::get_id() << ":" << std::endl;
  printf(u8"建立连接\n");
  connections[cl->peerAddress()] = make_unique<zm::MessageBlockPacketization<Test>>(zm::makeWeakCallback(test, &Test::fun));
  client_ptr = cl;
}

void
  send_complete(const zm::callback::TcpConnectionPtr& cl)
{
  std::cout << std::this_thread::get_id() << ":" << std::endl;
  printf(u8"发送完毕\n");
  is_over = false;
}

void
  close(const zm::callback::TcpConnectionPtr& cl)
{
  std::cout << std::this_thread::get_id() << ":" << std::endl;
  printf(u8"关闭连接\n");
  run = false;
  connections[cl->peerAddress()].reset(nullptr);
  is_over = false;
}

void
  high_mark(const zm::callback::TcpConnectionPtr& cl, size_t size)
{
  std::cout << std::this_thread::get_id() << ":" << std::endl;
  printf(u8"水位溢出\n");
  is_over = true;
}

void
  message(const zm::callback::TcpConnectionPtr& cl,
          std::vector<unsigned char> message)
{
  std::cout << std::this_thread::get_id() << ":" << std::endl;
  printf(u8"Message\n");
  if (!connections[cl->peerAddress()]->appendBlock(message))
  {
    std::cerr << u8"错误" << std::endl;
  }
}

int
  main(int argc, char* argv[])
{
  try
  {
    std::cout << std::this_thread::get_id() << ":" << std::endl;
    // 事件循环组
    zm::EventLoopGroup loop(1);
    // 事件工作线程组 
    zm::EventWorkerGroup worker(1);

    worker.start();
    loop.loop();
    // 绑定TCPClient到响应的事件循环组和工作组
    auto client = std::make_shared<zm::TCPClient>(loop, worker, "192.168.200.239", 2222);

    zm::callback::TCPClientCallback callback;

    callback.connectionCallback = connection;
    callback.closeCallback = close;
    callback.highWaterMarkCallback = high_mark;
    callback.writeCompleteCallback = send_complete;
    callback.messageCallback = message;

    client->init(callback);

    client->connectNB();

    unsigned char buffer[4096];

    for (auto index = 0; index < 4096; ++index)
    {
      buffer[index] = char('A' + index % 26);
    }

    while (run)
    {
      while (!client_ptr.get())
      {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printf("Wait\n");
      }

      client_ptr->sendMessage(buffer, sizeof(buffer));

      while (is_over)
      {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        printf("Wait send\n");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    client->shutdownSend();
    client->shutdownReceive();
    client->close();
    loop.quit();
    worker.stopAll();
  }
  catch (const Poco::Exception& ex)
  {
    std::cout << ex.what() << std::endl;
  }
}

