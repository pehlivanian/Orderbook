#ifndef __CONSUMER_HPP__
#define __CONSUMER_HPP__

#include "eventstream.hpp"
#include "concurrentqueue.h"
#include "orderedqueue.hpp"
#include "publisher.hpp"
#include "utils.hpp"

using namespace Utils;
using namespace Message;

#include <iostream>
#include <memory>
#include <syncstream>
#include <thread>
#include <array>
#include <vector>
#include <optional>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;


template<typename EventType, std::size_t BufferPower = 20>
class LockFreeOrderedMessageQueue {
public:

  LockFreeOrderedMessageQueue() : 
    buffer_{BUFFER_SIZE}
  {}

  bool enqueue(EventType&& e) {
    unsigned long sequenceNumber = e.seqNum_;
    std::size_t currentPublish = publishIndex_.load(std::memory_order_relaxed);
    std::size_t nextPublish = (currentPublish + 1) & mask_;

    // Check that buffer is full; we ignore for the time being and overwrite
    // if (nextPublish == consumeIndex.load(std::memory_order_acquire)) {
    //   return false;
    // }

    // Try to publish message
    EventType& slot = buffer_[currentPublish];
    int expected = -1;
    if (slot.seqNum_.compare_exchange_strong(expected, sequenceNumber,
					     std::memory_order_release, std::memory_order_relaxed)) {
      slot = std::move(e);
      publishIndex_.store(nextPublish, std::memory_order_release);
      return true;
    }
    
    return false;
  }

  std::optional<std::pair<int, std::string>> consume() {
    
  }

private:

  static constexpr std::size_t BUFFER_SIZE = 1ULL << BufferPower;
  static constexpr std::size_t mask_ = BUFFER_SIZE - 1;

  std::vector<EventType> buffer_;
  std::atomic<std::size_t> publishIndex_{0};
  std::atomic<std::size_t> consumeIndex_{0};
  std::atomic<int> expectedSequenceNumber_{0};
};


class AtomicIndex {
  std::atomic<unsigned long> c_;

public:

  AtomicIndex() : c_{0} {}

  unsigned long incr() noexcept {
    return 1 + c_.fetch_add(1, std::memory_order_release);
  }

  unsigned long get() const noexcept {
    return c_.load(std::memory_order_acquire);
  }

};

template<typename T>
class Spinlock {
public:
  explicit Spinlock(T* p) :
    p_{p}
  {}

  Spinlock(Spinlock<T>&& rhs) {
    p_.exchange(rhs.p_, std::memory_order_seq_cst);
    saved_p_ = nullptr;
  }
  Spinlock(const Spinlock<T>&) = delete;
  Spinlock operator=(const Spinlock<T>&) = delete;
  

  T* lock() {
    while (!(saved_p_ = p_.exchange(nullptr, std::memory_order_acquire))) {}
    return p_.load();
  }
  
  void unlock() {
    p_.store(saved_p_ , std::memory_order_release); 
  }

private:
  std::atomic<T*> p_;
  T* saved_p_ = nullptr;

};

template<typename EventType, typename OrderType>
class Consumer {

  using SPMCInnerQ = moodycamel::ConcurrentQueue<EventType>;
  using OrderedMPMCq = OrderedMPMCQueue<OrderType>;

  // const int num_workers = std::thread::hardware_concurrency() - 1;
  const int num_workers = 12;

  struct worker {
    
    // the worker will take the event from the SPMC queue
    // and publish to the MPMC queue

    Consumer<EventType, OrderType>* outer_;

    worker(Consumer<EventType, OrderType>* outer, int id) : 
      outer_{outer},
      id_{id}
    {}

    void operator()() {
      while (true) {
	EventType e;
	// sync_cout << "ATTEMPTING TO DEQUE FROM QUEUE 1" << std::endl;
	bool found = outer_->SPMCqueue_source_->try_dequeue(e);
	if (found) {
	  // std::cout << "ID: " << id_ << " : " << e << std::endl;
	  // sync_cout << "QUEUE 1 DEQUEUED: " << e.seqNum_ << std::endl;
	  auto o = eventLOBSTERToOrder(e);

	  // Artificial delay
	  // int delay = rand()%1000;
	  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	  outer_->MPMCqueue_target_->enqueue(o);
	  // sync_cout << "QUEUE 2: ENQUEUED: " << e.seqNum_ << std::endl;
	} else {
	  // sync_cout << "FAILED TO DEQUEUE FROM QUEUE 1" << std::endl;
	}
      }
    }

    int id_;
    
  };

public:
  Consumer(std::shared_ptr<SPMCInnerQ> q_source, std::shared_ptr<OrderedMPMCq> q_target) :
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

    for (auto& thread : threads)
      thread.join();

  }

  void consume_some(std::size_t num) {
    std::size_t count = 0;
    while(count < num) {
      EventType e;
      // sync_cout << "ATTEMPTING TO DEQUEUE FROM QUEUE 1" << std::endl;
      bool found = SPMCqueue_source_->try_dequeue(e);
      if (found) {
	// sync_cout << "QUEUE 1 DEQUEUED: " << e.seqNum_ << std::endl;
	auto o = eventLOBSTERToOrder(e);
	MPMCqueue_target_->enqueue(o);
	// sync_cout << "QUEUE 2: ENQUEUED: " << e.seqNum_ << std::endl;
	count++;
      } else {
	// sync_cout << "FAILED TO DEQUEUE FROM QUEUE 1"<< std::endl;
      }
    }
  }


private:
  std::shared_ptr<SPMCInnerQ> SPMCqueue_source_;
  std::shared_ptr<OrderedMPMCq> MPMCqueue_target_;
};

#endif
