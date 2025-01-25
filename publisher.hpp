#ifndef __PUBLISHER_HPP__
#define __PUBLISHER_HPP__

#include "eventstream.hpp"
#include "concurrentqueue.h"

#include <string>
#include <memory>
#include <future>
#include <atomic>
#include <array>
#include <utility>
#include <mutex>


template<typename EventType>
class Publisher {
public:

  using SPMCq = moodycamel::ConcurrentQueue<EventType>;

  Publisher() = delete;
  Publisher(std::string path) :
    path_{path},
    eventstream_{EventStream{path_}},
    SPMCqueue_{std::make_shared<SPMCq>()}
  {}

  Publisher(std::string path, std::shared_ptr<SPMCq> q_) :
    path_{path},
    eventstream_{EventStream{path_}},
    SPMCqueue_{q_}
  {}
  void publish() { 
    for (auto const& e : eventstream_) {
      SPMCqueue_->enqueue(e);
    }
  }
  
  std::shared_ptr<SPMCq> get_queue() const { return SPMCqueue_; }

private:

  std::string path_;
  EventStream eventstream_;
  std::shared_ptr<SPMCq> SPMCqueue_; 
  
};

#endif
