#pragma once
/*!
 * \file DataPacketI.h
 *
 * \author ZMing
 * \date °ËÔÂ 2018
 *
 * 
 */
#include <functional>
#include <memory>

namespace sduept
{
namespace NW103
{
class DataPacketI
{
public:
  using ASDUBufferWrapper = std::pair<int, std::unique_ptr<void*, std::function<void(void*)>>>;
  virtual ~DataPacketI() = default;
  virtual size_t size() const = 0;
};

}
}
