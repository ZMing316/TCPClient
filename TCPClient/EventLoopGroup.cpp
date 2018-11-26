#include "EventLoopGroup.h"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <vector>

#include <Poco/ThreadPool.h>
#include <Poco/Net/SocketReactor.h>

#include "Common.h"

namespace sduept
{
struct EventLoopGroup::Impl
{
  explicit Impl(int size);

  Poco::ThreadPool pool;
  std::vector<std::pair<Poco::Net::SocketReactor, std::atomic<uint16_t>>> reactors;
};

EventLoopGroup::Impl::Impl(int size)
  : pool("EventLoopGroup", 1, size)
  , reactors(size)
{
  assert(size > 0);
  for (auto& reactor : reactors)
  {
    reactor.second.store(0);
  } 
}

EventLoopGroup::Loop::Loop(Poco::Net::SocketReactor& r, std::atomic<uint16_t>& c)
  : reactor{r}
  , count{c}
{
  count.fetch_add(1);
}

EventLoopGroup::Loop::~Loop()
{
  count.fetch_sub(1);
}

EventLoopGroup::EventLoopGroup(int size)
  : impl_(new Impl(size))
{}

EventLoopGroup::~EventLoopGroup()
{
  quit();
  delete impl_;
}

std::unique_ptr<EventLoopGroup::Loop>
  EventLoopGroup::reactor() const
{
  using type = decltype(impl_->reactors)::value_type;

  const auto iterator = std::min_element(std::begin(impl_->reactors),
                                         std::end(impl_->reactors),
                                         [](const type& l, const type& r)
                                         {
                                           return l.second.load() < r.second.load();
                                         });

  return make_unique<Loop>(iterator->first, iterator->second);
}

void
  EventLoopGroup::loop() 
{
  for (auto& reactor : impl_->reactors)
  {
    impl_->pool.start(reactor.first);
  }
}

void
  EventLoopGroup::quit() 
{
  for (auto& reactor : impl_->reactors)
  {
    reactor.first.stop();
  }

  impl_->pool.joinAll();
}
}
