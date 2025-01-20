#ifndef __CONSUMER_HPP__
#define __CONSUMER_HPP__

#include "eventstream.hpp"
#include "concurrentqueue.h"
#include "utils.hpp"

using namespace Utils;
using namespace Message;

#include <memory>

template<typename EventType>
class Consumer {
public:

  using SPMCq = moodycamel::ConcurrentQueue<EventType>;

  Consumer(std::shared_ptr<SPMCq> q_source, std::shared_ptr<SPMCq> q_target) :
    SPMCqueue_source_{q_source},
    SPMCqueue_target_{q_target}
  {}

  void consume() {
    eventLOBSTER e;
    while (true) {
      bool found = SPMCqueue_source_->try_dequeue(e);
      if (found) {
	std::cout << e << std::endl;
	order o = eventLOBSTERToOrder(e);
      }
    }
  }
private:
  
  std::shared_ptr<SPMCq> SPMCqueue_source_;
  std::shared_ptr<SPMCq> SPMCqueue_target_;
};

#endif
