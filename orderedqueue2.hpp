#ifndef __ORDEREDQUEUE_HPP__
#define __ORDEREDQUEUE_HPP__

#include "utils.hpp"
#include "orderedqueue.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <array>
#include <cstddef>
#include <iostream>
#include <thread>
#include <syncstream>
#include <cstdlib>

#define sync_cout std::osyncstream(std::cout)

using namespace Numerics;
using namespace Utils;

template<typename EventType, std::size_t RequestCapacity=2<<20>
class OrderedMPMCQueue {
  
  static constexpr std::size_t Capacity = isPowerOfTwo(RequestCapacity) ? 
    RequestCapacity : nextPowerOfTwo(RequestCapacity);

  static constexpr std::size_t MASK = Capacity - 1;
  static constexpr std::size_t CACHE_LINE_SIZE = 64;

  struct Node {
    std::atomic<EventType*> event{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<bool> consumed{false};
  };

  std::size_t getIndex(std::size_t seqNum) const {
    return seqNum & MASK;
  }

  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> nextToConsume_{0};

public:
  OrderedMPMCQueue() = default;
  ~OrderedMPMCQueue() {
    
    // Clear out the buffer_
    for (auto& node : buffer_) {
      EventType* event = node.event.load(std::memory_order_relaxed);
      if (event) {
	delete event;
      }
    }
  }

  OrderedMPMCQueue(const OrderedMPMCQueue&) = delete;
  OrderedMPMCQueue& operator=(const OrderedMPMCQueue&) = delete;
  OrderedMPMCQueue(OrderedMPMCQueue&&) = delete;
  OrderedMPMCQueue& operator=(OrderedMPMCQueue&&) = delete;

  void enqueue(EventType event) {
    while (!try_enqueue(std::move(event))) {
      std::this_thread::yield();
    }
  }

  bool try_enqueue(EventType event) {
    const std::size_t seqNum = event.seqNum_;
    const std::size_t idx = getIndex(seqNum);

    std::size_t expectedCount = writeCount_.load(std::memory_order_relaxed);
    if (expectedCount >= seqNum + Capacity) {

      // XXX To do; queue is full, probably should 
      // resize, reallocate to next power of 2 
      // and continue. Under a lock? 
      return false;
    }

    Node& node = buffer_[idx];

    if (node.ready.load(std::memory_order_acquire)) {
      EventType* oldEvent = node.event.load(std::memory_order_relaxed);
      if (oldEvent && oldEvent->seqNum_ >= seqNum) {

	// node still not published, wait
	return false;
      }
    }

    // XXX To do; memory arena
    EventType* newEvent = new EventType(std::move(event));

    EventType* expected = nullptr;
    if (!node.event.compare_exchange_strong(expected, newEvent,
					    std::memory_order_release,
					    std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    // Node stored need to indicate ready
    node.ready.store(true, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_release);

    return true;

  }

  bool try_dequeue(EventType& e) {
    std::optional<EventType> opt = try_dequeue();
    if (opt.has_value()) {
      e = *opt;
      return true;
    } else {
      return false;
    }
  }


};





#endif
