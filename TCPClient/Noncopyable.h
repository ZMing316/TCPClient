#pragma once
/*!
 * \file Noncopyable.h
 *
 * \author ZMing
 * \date °ËÔÂ 2018
 *
 * 
 */
namespace sduept
{

class Noncopyable
{
public:
  Noncopyable() = default;
  ~Noncopyable() = default;
  Noncopyable(const Noncopyable&) = delete;
  Noncopyable(Noncopyable&&) = delete;

  Noncopyable& operator==(const Noncopyable&) = delete;
  Noncopyable& operator==(Noncopyable&&) = delete;
};
}
