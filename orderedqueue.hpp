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
#include <thread>
#include <syncstream>
#include <chrono>
#include <cstdlib>

#define sync_cout std::osyncstream(std::cout)

#define DEBUG
#undef DEBUG

using namespace Numerics;
using namespace Utils;
using namespace std::chrono_literals;

template<typename EventType, size_t RequestCapacity=2<<20>
class OrderedMPMCQueue {
private:

  static constexpr std::size_t Capacity = isPowerOfTwo(RequestCapacity)  ?
  RequestCapacity : nextPowerOfTwo(RequestCapacity);
  
  static constexpr std::size_t MASK = Capacity - 1;

  struct Node {
    std::atomic<EventType*> event{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<bool> consumed{false};
    std::atomic<bool> acknowledged{true};
    std::shared_ptr<std::future<void>> ack{};
  };

  static constexpr size_t CACHE_LINE_SIZE = 64;
    

  // writeCount_, nextToConsume_ will increase without bound
  // and act like tail, head respectively. 
  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

  std::chrono::milliseconds processing_time_{0ms};

  // Get buffer index from sequence number
  size_t getIndex(size_t seqNum) const {
    return seqNum & MASK;
  }

public:
  OrderedMPMCQueue() = default;
  
  // Additional constructor, will remove, for
  // mocking endpoint for simulation and benchmark
  // purposes
  OrderedMPMCQueue(std::chrono::milliseconds processing_time) :
    processing_time_{processing_time}
  {}

  ~OrderedMPMCQueue() {
    // Cleanup any remaining events
    for (auto& node : buffer_) {
      EventType* event = node.event.load(std::memory_order_relaxed);
      if (event) {
	delete event;
      }
    }
  }

  // Disable copying and moving
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
        

    // Reserve a spot by incrementing write count
    size_t expectedCount = writeCount_.load(std::memory_order_relaxed);
    if (expectedCount >= seqNum + Capacity) {
      return false;  // Queue is full
    }

    Node& node = buffer_[idx];
        
    // Check if the slot is available
    if (node.ready.load(std::memory_order_acquire)) {
      EventType* oldEvent = node.event.load(std::memory_order_relaxed);
      if (oldEvent && oldEvent->seqNum_ >= seqNum) {
	return false;  // Slot still in use
      }
    }

    // sync_cout << "ENQUEUE: SLOT AVAILABLE: " << seqNum << std::endl;

    // Create new event on heap
    EventType* newEvent = new EventType(std::move(event));
        
    // Store the event pointer
    EventType* expected = nullptr;
    if (!node.event.compare_exchange_strong(expected, newEvent,
					    std::memory_order_release,
					    std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    // Mark the node as ready
    node.ready.store(true, std::memory_order_release);
    node.consumed.store(false, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_release);
        
    // sync_cout << "=====> ENQUEUED: " << seqNum << std::endl;
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

  bool try_dequeue_p(EventType& e) {
    std::pair<std::optional<EventType>, std::optional<std::promise<void>>> opt = try_dequeue_p();
    if (opt.first.has_value()) {
      e = *opt.first;
      return true;
    } else {
      return false;
    }
  }

  std::optional<EventType> try_dequeue() {
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentReadSeqNum);

    // sync_cout << currentReadSeqNum << " : " << idx << std::endl;

    // For dequeuing, idx == currentReadSeqNum should hold modulo
    // wraparound at this point
    // Check
    // =====
    // 1. node sourced at idx is ready
    // 2. event at idx location can be loaded and 
    //    the sequence number on that event is equal to currentReadSeqNum
    // 3. node can be marked as consumed
    //
    // After which, if successful
    // ==========================
    // 1. increment nextToConsume_
    // 2. set node.ready to false
    // 3. move out of the node
    // 4. set node.event to nullptr
    
    Node& node = buffer_[idx];

    // Check if the next event is ready
    if (!node.ready.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    EventType* event = node.event.load(std::memory_order_acquire);
    if (!event || event->seqNum_ != currentReadSeqNum) {
      return std::nullopt;
    }

    bool expected = false;
    if (!node.consumed.compare_exchange_strong(expected, true,
					       std::memory_order_acq_rel)) {
      return std::nullopt;
    }

    nextToConsume_.fetch_add(1, std::memory_order_release);        
    node.ready.store(false, std::memory_order_release);
    std::promise<void> prom{};
    node.ack = std::make_shared<std::future<void>>(prom.get_future());
        
    EventType result = std::move(*event);
    delete event;
    node.event.store(nullptr, std::memory_order_release);

#ifdef DEBUG
    sync_cout << "PUBLISHED: " << result.seqNum_ << std::endl;
#endif

    return result;

  }

  std::pair<std::optional<EventType>, std::optional<std::promise<void>>> try_dequeue_p() {
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentReadSeqNum);

    // sync_cout << currentReadSeqNum << " : " << idx << std::endl;

    // For dequeuing, idx == currentReadSeqNum should hold modulo
    // wraparound at this point
    // Check
    // =====
    // 1. node sourced at idx is ready
    // 2. event at idx location can be loaded and 
    //    the sequence number on that event is equal to currentReadSeqNum
    // 3. node can be marked as consumed
    //
    // After which, if successful
    // ==========================
    // 1. increment nextToConsume_
    // 2. set node.ready to false
    // 3. move out of the node
    // 4. set node.event to nullptr
    
    Node& node = buffer_[idx];

    // Check if previous event is acked
    if (currentReadSeqNum > 0) {
      size_t prevIdx = getIndex(currentReadSeqNum - 1);
      Node& prevNode = buffer_[prevIdx];
      // sync_cout << "Trying to dequeue: " << prevIdx << std::endl;
      if (prevNode.ack->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
	volatile auto conf = prevNode.ack.get();
	sync_cout << "=====> DEQUEUED: " << prevIdx << std::endl;
      } else {
	return {std::nullopt, std::nullopt};
      }
    }
        
    // Check if the next event is ready
    if (!node.ready.load(std::memory_order_acquire)) {
      return {std::nullopt, std::nullopt};
    }

    EventType* event = node.event.load(std::memory_order_acquire);
    if (!event || event->seqNum_ != currentReadSeqNum) {
      return {std::nullopt, std::nullopt};
    }

    bool expected = false;
    if (!node.consumed.compare_exchange_strong(expected, true,
					       std::memory_order_acq_rel)) {
      return {std::nullopt, std::nullopt};
    }

    nextToConsume_.fetch_add(1, std::memory_order_release);        
    node.ready.store(false, std::memory_order_release);
    std::promise<void> prom{};
    node.ack = std::make_shared<std::future<void>>(prom.get_future());
        
    EventType result = std::move(*event);
    delete event;
    node.event.store(nullptr, std::memory_order_release);

#ifdef DEBUG
    sync_cout << "PUBLISHED: " << result.seqNum_ << std::endl;
#endif

    return std::make_pair(std::optional<EventType>(std::move(result)),
			  std::optional<std::promise<void>>(std::move(prom)));
  }

  // Helper methods
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
