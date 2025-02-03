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

#define DEBUG
#undef DEBUG

#define sync_cout std::osyncstream(std::cout)

using namespace Numerics;
using namespace Utils;

template<typename EventType, size_t RequestCapacity=2<<20>
class OrderedMPMCQueue {
private:

  // Ring buffer on std::array for incoming queue implementation.
  // Size is 2**n to allow for bitmasking in getIndex

  static constexpr std::size_t Capacity = isPowerOfTwo(RequestCapacity)  ?
  RequestCapacity : nextPowerOfTwo(RequestCapacity);
  
  static constexpr std::size_t MASK = Capacity - 1;
  static constexpr size_t CACHE_LINE_SIZE = 64;

  struct Node {
    std::atomic<EventType*> event{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<bool> consumed{false};
  };
    
  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

  size_t getIndex(size_t seqNum) const {
    return seqNum & MASK;
  }

public:
  OrderedMPMCQueue() = default;
  ~OrderedMPMCQueue() {
    // Cleanup any remaining events
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
    const size_t targetSeqNum = event.seqNum_;
    const size_t idx = getIndex(targetSeqNum);
        
    size_t expectedCount = writeCount_.load(std::memory_order_relaxed);
    if (expectedCount >= targetSeqNum + Capacity) {
      return false;  // Queue is full
    }

    Node& node = buffer_[idx];
        
    if (node.ready.load(std::memory_order_acquire)) {
      EventType* oldEvent = node.event.load(std::memory_order_relaxed);
      if (oldEvent && oldEvent->seqNum_ >= targetSeqNum) {
	return false;  // Slot still in use
      }
    }

    // Heap construction - we can do better in future 
    EventType* newEvent = new EventType(std::move(event));
        
    EventType* expected = nullptr;
    if (!node.event.compare_exchange_strong(expected, newEvent,
					    std::memory_order_release,
					    std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    node.ready.store(true, std::memory_order_release);
    node.consumed.store(false, std::memory_order_release);
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


  std::optional<EventType> try_dequeue() {
    size_t currentSeq = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentSeq);
        
    Node& node = buffer_[idx];
        
    if (!node.ready.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    EventType* event = node.event.load(std::memory_order_acquire);
    if (!event || event->seqNum_ != currentSeq) {
      return std::nullopt;
    }

    bool expected = false;
    if (!node.consumed.compare_exchange_strong(expected, true,
					       std::memory_order_acq_rel)) {
      return std::nullopt;
    }

    nextToConsume_.fetch_add(1, std::memory_order_release);
        
    node.ready.store(false, std::memory_order_release);
        
    EventType result = std::move(*event);
    delete event;
    node.event.store(nullptr, std::memory_order_release);

#ifdef DEBUG
    sync_cout << result.seqNum_ << std::endl;
#endif
        
    return result;
  }

  bool empty() const {
    size_t current = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(current);
    return !buffer_[idx].ready.load(std::memory_order_relaxed);
  }

  bool full() const {
    return writeCount_.load(std::memory_order_relaxed) >= 
      nextToConsume_.load(std::memory_order_relaxed) + Capacity;
  }

  size_t size() const {
    return writeCount_.load(std::memory_order_relaxed) - 
      nextToConsume_.load(std::memory_order_relaxed);
  }
};


#endif
