#pragma once
/*!
 * \file WeakCallback.h
 *
 * \author ZMing
 * \date °ËÔÂ 2018
 *
 * 
 */
#include <memory>
#include <functional>

namespace sduept
{
template <typename CLASS, typename ... ARGS>
class WeakCallback
{
public:
  WeakCallback(std::weak_ptr<CLASS> object, std::function<void(CLASS*, ARGS ...)> func)
    : object_(object)
    , func_(func)
  { }

  void
    operator()(ARGS&&... args)
  {
    if (auto object = object_.lock())
    {
      func_(object.get(), std::forward<ARGS>(args)...);
    }
  }

private:
  std::weak_ptr<CLASS> object_;
  std::function<void(CLASS*, ARGS ...)> func_;
};

template <typename CLASS, typename ... ARGS>
WeakCallback<CLASS, ARGS...>
  makeWeakCallback(std::shared_ptr<CLASS> object, void (CLASS::*function)(ARGS ...))
{
  return WeakCallback<CLASS, ARGS...>(object, function);
}

template <typename CLASS, typename ... ARGS>
WeakCallback<CLASS, ARGS...>
  makeWeakCallback(std::shared_ptr<CLASS> object, void (CLASS::*function)(ARGS ...) const)
{
  return WeakCallback<CLASS, ARGS...>(object, function);
}
}
