#pragma once
/*!
 * \file Noncopyable.h
 *
 * \author ZMing
 * \date ���� 2018
 *
 * 
 */
namespace zm
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
