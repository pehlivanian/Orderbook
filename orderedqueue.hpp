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
    std::mutex mutex;
    std::condition_variable cv;
  };

  static constexpr size_t CACHE_LINE_SIZE = 64;

  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

  size_t getIndex(size_t seqNum) const {
    return seqNum & MASK;
  }

  std::mutex vector_mut;
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
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentReadSeqNum);
    
    Node& node = buffer_[idx];

    // Wait for previous node to be processed if it exists
    if (currentReadSeqNum > 0) {
      size_t prevIdx = getIndex(currentReadSeqNum - 1);
      Node& prevNode = buffer_[prevIdx];
      
      {
	std::unique_lock<std::mutex> lock(prevNode.mutex);
	prevNode.cv.wait(lock, [&prevNode] { 
			   return prevNode.processed.load(std::memory_order_acquire); 
			 });
      }
    }

    if (!node.ready.load(std::memory_order_acquire)) {
      return false;
    }

    EventType* evt = node.event.load(std::memory_order_acquire);
    if (!evt || evt->seqNum_ != currentReadSeqNum) {
      return false;
    }

    bool expected = false;
    if (!node.processed.compare_exchange_strong(expected, true,
						std::memory_order_acq_rel)) {
      return false;
    }

    nextToConsume_.fetch_add(1, std::memory_order_release);        
    node.ready.store(false, std::memory_order_release);
    
    event = std::move(*evt);
    delete evt;
    node.event.store(nullptr, std::memory_order_release);

#ifdef DEBUG
    sync_cout << "DEQUEUED: " << currentReadSeqNum << std::endl;
#endif

    return true;
  }

  // Called by the task processor when it has finished processing the task
  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    
    // Notify waiting consumers that this task is complete
    std::lock_guard<std::mutex> lock(vector_mut);
    local_processed_.push_back(seqNum);
    node.cv.notify_all();

    // sync_cout << "MARKED: " << seqNum << std::endl;
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
    std::copy(local_processed_.begin(), local_processed_.end(), std::ostream_iterator<std::size_t>(std::cout, "\n"));
    std::cout << std::endl;
  }
};

#endif
