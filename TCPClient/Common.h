#pragma once
/*!
 * \file Common.h
 *
 * \author ZMing
 * \date °ËÔÂ 2018
 *
 * 
 */
#include <memory>
#include <iostream>
#include <Poco/Format.h>

#if (__cplusplus > 201103L || defined(_MSC_VER)) && \
    !(defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ == 8)

using std::make_unique;

#else

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(
    Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif

#if defined(_DEBUG) || defined(POCO_LOG_DEBUG)
#include <cassert>
#define DEBUG_ASSERT(x) assert(x)
#else
#define DEBUG_ASSERT(value)
#endif

#define MY_HANDLE(ex) \
   std::cerr << Poco::format("%s:%04d:%s\n", std::string(__func__), __LINE__, std::string(ex)); 