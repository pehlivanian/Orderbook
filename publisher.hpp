#ifndef __PUBLISHER_HPP__
#define __PUBLISHER_HPP__

#include "concurrentqueue.h"
#include "eventstream.hpp"

#include <array>
#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <syncstream>
#include <thread>
#include <utility>

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

template <typename EventType>
class Publisher {
 public:
  using SPMCq = moodycamel::ConcurrentQueue<EventType>;

  Publisher() = delete;
  Publisher(std::string path)
      : path_{path}, eventstream_{EventStream{path_}}, SPMCqueue_{std::make_shared<SPMCq>()} {}

  Publisher(std::string path, std::shared_ptr<SPMCq> q_)
      : path_{path}, eventstream_{EventStream{path_}}, SPMCqueue_{q_} {}
  void publish() {
    for (auto const& e : eventstream_) {
      // sync_cout << "ATTEMPTING TO ENQUEUE TO QUEUE 1" << std::endl;
      SPMCqueue_->enqueue(e);
      // sync_cout << "QUEUE 1: ENQUEUED: " << e.seqNum_ << std::endl;
    }
  }

  void publish_some(std::size_t num) {
    std::size_t count = 0;
    for (auto const& e : eventstream_) {
      // sync_cout << "ATTEMPTING TO ENQUEUE TO QUEUE1" << std::endl;
      SPMCqueue_->enqueue(e);
      // sync_cout << "QUEUE1: ENQUEUED: " << e.seqNum_ << std::endl;
      count++;
      if (count >= num)
        return;
    }
  }

  std::shared_ptr<SPMCq> get_queue() const { return SPMCqueue_; }

 private:
  std::string path_;
  EventStream eventstream_;
  std::shared_ptr<SPMCq> SPMCqueue_;
};

#endif
