#ifndef __CONSUMER_HPP__
#define __CONSUMER_HPP__

#include "eventstream.hpp"
#include "concurrentqueue.h"
#include "publisher.hpp"
#include "utils.hpp"

using namespace Utils;
using namespace Message;

#include <iostream>
#include <memory>
#include <syncstream>
#include <thread>
#include <array>
#include <atomic>
#include <mutex>
#include <chrono>

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

template<typename OrderType>
class ActivatedQueue {
public:
  ActivatedQueue() :
    nextSeqNum_{-1}
  {
    for (auto& el : statusBit_) {
      el.clear();
    }
  }

  void enqueue(OrderType& el) {
    std::cout << "ENQUEUED: " << el.seqNum_ << " : " << el << std::endl;
    cargo_[el.seqNum_] = el;
    statusBit_[el.seqNum_].test_and_set();
  }

  bool try_dequeue(OrderType& el) {
    if (statusBit_[nextSeqNum_ + 1].test()) {
      std::lock_guard<std::mutex> lock(mut_);
      el = std::move(cargo_[nextSeqNum_ + 1]);
      statusBit_[nextSeqNum_ + 1].clear();
      nextSeqNum_++;

    }
  }

private:
  
  int nextSeqNum_;
  std::array<std::atomic_flag, 1 << 24> statusBit_;
  std::array<OrderType, 1 << 24> cargo_;

  mutable std::mutex mut_;  

};

template<typename OrderType>
class ActivatedQueue2 {
public:
  
  ActivatedQueue2() :
    nextSeqNum_{-1}
  {
    for (auto& el : statusBit_) {
      el.clear();
    }
  }

  void enqueue(OrderType& el) {
    std::cout << "ENQUEUED: " << el.seqNum_ << " : " << el << std::endl;
    cargo_[el.seqNum_] = el;
    statusBit_[el.seqNum_].test_and_set();
  }

  bool try_dequeue(OrderType& el) {
    if (statusBit_[nextSeqNum_ + 1].test()) {

      std::lock_guard<std::mutex> lock(mut_);
      el = std::move(cargo_[nextSeqNum_ + 1]);
      statusBit_[nextSeqNum_ + 1].clear();
      nextSeqNum_++;

    }
  }

private:
  
  int nextSeqNum_;
  std::array<std::atomic_flag, 1 << 12> statusBit_;
  std::array<OrderType, 1 << 12> cargo_;

  mutable std::mutex mut_;
};

template<typename EventType, typename OrderType>
class Consumer {

  using SPMCInnerQ = moodycamel::ConcurrentQueue<EventType>;
  using MPMCq = ActivatedQueue<OrderType>;

  const int num_workers = std::thread::hardware_concurrency() - 1;

  struct worker {
    
    // the worker will take the event from the SPMC queue
    // and publish to the MPMC queue

    Consumer<EventType, OrderType>* outer_;

    worker(Consumer<EventType, OrderType>* outer, int id) : 
      outer_{outer},
      id_{id}
    {}

    void operator()() {
      eventLOBSTER e;
      while (true) {
	bool found = outer_->SPMCqueue_source_->try_dequeue(e);
	if (found) {
	  std::cout << "ID: " << id_ << " : " << e << std::endl;
	  auto o = eventLOBSTERToOrder(e);
	  outer_->MPMCqueue_target_->enqueue(o);
	}
      }
    }

    int id_;
    
  };

public:
  Consumer(std::shared_ptr<SPMCInnerQ> q_source, std::shared_ptr<MPMCq> q_target) :
    SPMCqueue_source_{q_source},
    MPMCqueue_target_{q_target}
  {}
					
  void consume() {

    // workers
    std::vector<worker> workers;
    for (std::size_t i=0; i<num_workers; ++i) {
      workers.emplace_back(this, i);
    }

    // tasks
    std::vector<std::function<void(void)>> tasks;
    for (std::size_t i=0; i<workers.size(); ++i) {
      tasks.emplace_back(workers[i]);
    }

    // threads
    std::vector<std::thread> threads;
    for (std::size_t i=0; i<workers.size(); ++i) {
      threads.emplace_back(tasks[i]);
    }

    for (std::size_t i=0; i<threads.size(); ++i) {
      threads[i].join();
    }
  }


private:
  std::shared_ptr<SPMCInnerQ> SPMCqueue_source_;
  std::shared_ptr<MPMCq> MPMCqueue_target_;
};

#endif
