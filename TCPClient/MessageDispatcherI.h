#pragma once
#include <vector>
#include <functional>
#include <memory>

#include "Common.h"

/*!
 * \file MessageDispatcherI.h
 *
 * \author ZMing
 * \date °ËÔÂ 2018
 *
 * 
 */
namespace sduept
{
class MessageDispatcherI
{
public:
  MessageDispatcherI() = default;
  virtual ~MessageDispatcherI() = default;
  virtual void onMessage(std::vector<unsigned char>&& message) const = 0;
};
}
