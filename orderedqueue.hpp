#ifndef __ORDEREDQUEUE_HPP__
#define __ORDEREDQUEUE_HPP__

#include "utils.hpp"
#include "orderedqueue.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <array>
#include <cstddef>
#include <future>
#include <iostream>
#include <iterator>
#include <thread>
#include <syncstream>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <condition_variable>

#define sync_cout std::osyncstream(std::cout)

#define DEBUG
#undef DEBUG

using namespace Numerics;
using namespace Utils;
using namespace std::chrono_literals;

template<typename EventType, size_t RequestCapacity=2<<20>
class OrderedMPMCQueue {
private:
  static constexpr std::size_t Capacity = isPowerOfTwo(RequestCapacity) ?
    RequestCapacity : nextPowerOfTwo(RequestCapacity);
  
  static constexpr std::size_t MASK = Capacity - 1;

  struct Node {
    std::atomic<EventType*> event{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<bool> processed{false};
  };

  static constexpr size_t CACHE_LINE_SIZE = 64;

  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

  // Track dequeue order separately from processing order
  mutable std::mutex dequeue_mut;
  std::vector<std::size_t> dequeue_order_;
  
  // Track processing completion order
  mutable std::mutex process_mut;
  std::vector<std::size_t> process_order_;

  size_t getIndex(size_t seqNum) const {
    return seqNum & MASK;
  }

  mutable std::mutex vector_mut;
  std::vector<std::size_t> local_processed_ = std::vector<std::size_t>{};

public:
  OrderedMPMCQueue() = default;
  
  ~OrderedMPMCQueue() {
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
    const size_t seqNum = event.seqNum_;
    const size_t idx = getIndex(seqNum);

    size_t expectedCount = writeCount_.load(std::memory_order_relaxed);
    if (expectedCount >= seqNum + Capacity) {
      return false;  // Queue is full
    }

    Node& node = buffer_[idx];
        
    if (node.ready.load(std::memory_order_acquire)) {
      EventType* oldEvent = node.event.load(std::memory_order_relaxed);
      if (oldEvent && oldEvent->seqNum_ >= seqNum) {
	      return false;  // Slot still in use
      }
    }

    EventType* newEvent = new EventType(std::move(event));
    EventType* expected = nullptr;
    if (!node.event.compare_exchange_strong(expected, newEvent,
					    std::memory_order_release,
					    std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    node.ready.store(true, std::memory_order_release);
    node.processed.store(false, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_release);
    
    return true;
  }

  bool try_dequeue(EventType& e) {
    std::optional<EventType> opt = try_dequeue();
    if (opt.has_value()) {
      e = std::move(*opt);
      return true;
    }
    return false;
  }

  std::optional<EventType> try_dequeue() {
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentReadSeqNum);
    
    Node& node = buffer_[idx];

    if (!node.ready.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    EventType* event = node.event.load(std::memory_order_acquire);
    if (!event || event->seqNum_ != currentReadSeqNum) {
      return std::nullopt;
    }

    bool expected = false;
    if (!node.processed.compare_exchange_strong(expected, true,
						std::memory_order_acq_rel)) {
      return std::nullopt;
    }

    nextToConsume_.fetch_add(1, std::memory_order_release);        
    node.ready.store(false, std::memory_order_release);
    
    EventType result = std::move(*event);
    delete event;
    node.event.store(nullptr, std::memory_order_release);

    // Note: We don't notify here - that happens when the task is actually processed
    return result;
  }

  bool try_dequeue_p(EventType& event) {
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_acquire);
    const size_t idx = getIndex(currentReadSeqNum);
    
    Node& node = buffer_[idx];

    // First verify all previous nodes are processed
    for (size_t i = 0; i < currentReadSeqNum; ++i) {
        size_t prevIdx = getIndex(i);
        Node& prevNode = buffer_[prevIdx];
        if (!prevNode.processed.load(std::memory_order_acquire)) {
            return false;
        }
    }

    // Try to atomically increment nextToConsume_ to claim this sequence number
    size_t expected = currentReadSeqNum;
    if (!nextToConsume_.compare_exchange_strong(expected, currentReadSeqNum + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
        return false;  // Another thread claimed this sequence number
    }

    // We've claimed this sequence number, now we can safely process it
    if (!node.ready.load(std::memory_order_acquire)) {
        return false;
    }

    EventType* evt = node.event.load(std::memory_order_acquire);
    if (!evt || evt->seqNum_ != currentReadSeqNum) {
        return false;
    }

    // Move the event data
    event = std::move(*evt);
    delete evt;
    node.event.store(nullptr, std::memory_order_release);
    node.ready.store(false, std::memory_order_release);

    // Record dequeue order
    {
        std::lock_guard<std::mutex> lock(dequeue_mut);
        dequeue_order_.push_back(currentReadSeqNum);
    }

    return true;
  }

  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    
    // Record processing completion order
    {
      std::lock_guard<std::mutex> lock(process_mut);
      process_order_.push_back(seqNum);
    }

    // Mark this node as processed
    node.processed.store(true, std::memory_order_release);
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

  void replay() const {
    std::cout << "Dequeue order:\n";
    std::copy(dequeue_order_.begin(), dequeue_order_.end(), 
              std::ostream_iterator<std::size_t>(std::cout, "\n"));
    
    std::cout << "\nProcessing completion order:\n";
    std::copy(process_order_.begin(), process_order_.end(), 
              std::ostream_iterator<std::size_t>(std::cout, "\n"));
    
    std::cout << std::endl;
  }

  // Get the order in which events were dequeued
  std::vector<std::size_t> get_dequeue_order() const {
    std::lock_guard<std::mutex> lock(dequeue_mut);
    return dequeue_order_;
  }

  // Get the order in which events were processed
  std::vector<std::size_t> get_process_order() const {
    std::lock_guard<std::mutex> lock(process_mut);
    return process_order_;
  }
};

#endif
