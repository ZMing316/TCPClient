#pragma once
#include <vector>
#include <functional>
#include <memory>

#include "Common.h"

/*!
 * \file MessageDispatcherI.h
 *
 * \author ZMing
 * \date ���� 2018
 *
 * 
 */
namespace zm
{
class MessageDispatcherI
{
public:
  MessageDispatcherI() = default;
  virtual ~MessageDispatcherI() = default;
  virtual void onMessage(std::vector<unsigned char>&& message) const = 0;
};
}
