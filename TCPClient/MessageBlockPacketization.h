#pragma once
#include <vector>
#include <iostream>
#include <mutex>

#include "WeakCallback.h"
#include "Common.h"

namespace zm
{

template<typename T>
struct identity { typedef T type; };

template <typename CLASS, typename DELEGATE = void>
class MessageBlockPacketization
{
public:
  using Packet = std::vector<unsigned char>;

  MessageBlockPacketization(WeakCallback<CLASS, std::vector<unsigned char>&&> callback);

  ~MessageBlockPacketization() = default;

  typename std::enable_if<!std::is_same<DELEGATE, void>::value, void>::type
    delegate(WeakCallback<DELEGATE, const std::vector<unsigned char>&, std::vector<Packet>&>&& callback)
  {
    appendDelegate_ = make_unique<WeakCallback<DELEGATE, const std::vector<unsigned char>&, std::vector<Packet>&>>(std::move(callback));
  }

  // �������е������Ӱ��̣߳�����Ҫ�̼߳�ͬ����ʹ�������汾
  // �����ְ�ʧ�� ����flase һ���ȡ�Ͽ����ӵĲ���
  bool appendBlock(const std::vector<unsigned char>& block) noexcept;
  bool appendBlockMutex(const std::vector<unsigned char>& block) noexcept;

private:
  // ����� TCP �ֽ������зְ����� ����˳�� ���ز������� ��������������� ���ؿ�

  template <typename T>
  std::vector<Packet>
    appendBlockImpl(const std::vector<unsigned char>& block, identity<T>)
  {
    if (!appendDelegate_)
      throw std::logic_error("NO delegate.");
    std::vector<Packet> ret;
    (*appendDelegate_)(block, ret);
    return ret;
  }

  std::vector<Packet>
    appendBlockImpl(const std::vector<unsigned char>& block, identity<void>)
  {
    std::vector<Packet> ret;
    ret.emplace_back(block);
    return ret;
  }

  std::mutex mutex_;
  WeakCallback<CLASS, std::vector<unsigned char>&&> messageCallback_;
  std::unique_ptr<WeakCallback<DELEGATE, const std::vector<unsigned char>&, std::vector<Packet>&>> appendDelegate_{};
};

template <typename CLASS, typename DELEGATE>
MessageBlockPacketization<CLASS, DELEGATE>::MessageBlockPacketization(WeakCallback<CLASS, std::vector<unsigned char>&&> callback)
  : messageCallback_(std::move(callback))
{ }

template <typename CLASS, typename DELEGATE>
bool
  MessageBlockPacketization<CLASS, DELEGATE>::appendBlock(const std::vector<unsigned char>& block) noexcept
{
  try
  {
    auto packets = appendBlockImpl(block, identity<DELEGATE>());
    for (auto& packet : packets)
    {
      messageCallback_(std::move(packet));
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    return false;
  }
  return true;
}

template <typename CLASS, typename DELEGATE>
bool
  MessageBlockPacketization<CLASS, DELEGATE>::appendBlockMutex(const std::vector<unsigned char>& block) noexcept
{
  std::lock_guard<std::mutex> lock(mutex_);
  return appendBlock(block);
}
}
