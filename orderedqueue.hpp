#ifndef __ORDEREDQUEUE_HPP__
#define __ORDEREDQUEUE_HPP__

#include "orderedqueue.hpp"
#include "utils.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <syncstream>
#include <thread>
#include <vector>

#define sync_cout std::osyncstream(std::cout)

#define DEBUG
#undef DEBUG

using namespace Numerics;
using namespace Utils;
using namespace std::chrono_literals;

template <typename EventType, size_t RequestCapacity = 2 << 20>
class OrderedMPMCQueue {
 private:
  static constexpr std::size_t Capacity =
      isPowerOfTwo(RequestCapacity) ? RequestCapacity : nextPowerOfTwo(RequestCapacity);

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

  size_t getIndex(size_t seqNum) const { return seqNum & MASK; }

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
    if (!node.event.compare_exchange_strong(expected, newEvent, std::memory_order_release,
                                            std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    node.ready.store(true, std::memory_order_release);
    node.processed.store(false, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_release);

    // sync_cout << "Enqueued event with seqNum_: " << event.seqNum_ << std::endl;

    return true;
  }

  bool try_dequeue(EventType& e, bool external_ack=false) {
    std::optional<EventType> opt = try_dequeue(external_ack);
    if (opt.has_value()) {
      e = std::move(*opt);
      return true;
    }
    return false;
  }

  std::optional<EventType> try_dequeue(bool external_ack=false) {
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_acquire);
    const size_t idx = getIndex(currentReadSeqNum);

    // sync_cout << "Trying to dequeue sequence number " << currentReadSeqNum << std::endl;

    Node& node = buffer_[idx];

    // Previous node must be processed, but only if we're not processing seqNum 0
    if (currentReadSeqNum > 0) {
      // Only need to check the immediately previous node
      size_t prevIdx = getIndex(currentReadSeqNum - 1);
      Node& prevNode = buffer_[prevIdx];
      if (!prevNode.processed.load(std::memory_order_acquire)) {
	
	// XXX
	// Maybe we can preload buffer_ with bare events but the correct sequence numbers.
	// Otherwise the following line throws if prevNode.event is nullptr 
	// if (prevNode.event.load(std::memory_order_acquire)->seqNum_ <= currentReadSeqNum) {
	if (true) {
	  // sync_cout << "Previous node { index: " << prevIdx << " } is not processed" << std::endl;
	  return std::nullopt;
	}
      
      }
    }

    // Try to claim this sequence number
    size_t expected = currentReadSeqNum;
    if (!nextToConsume_.compare_exchange_strong(expected, currentReadSeqNum + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
      // sync_cout << "Failed to claim sequence number " << currentReadSeqNum << std::endl;
      return std::nullopt;
    }

    // sync_cout << "Set nextToConsume_ to " << currentReadSeqNum + 1 << std::endl;

    // If we fail after this point, we need to roll back nextToConsume_
    // I think this works; any other thread that comes in and is looking
    // to dequeue the next node will preNode as not processed in the
    // previous check.
    bool success = false;
    auto rollback = [&](void*) {
      // sync_cout << "RAII guard destructor called for seqNum " << currentReadSeqNum << std::endl;
      if (!success) {
        // sync_cout << "Rolling back nextToConsume_ to " << currentReadSeqNum << std::endl;
        nextToConsume_.store(currentReadSeqNum, std::memory_order_release);
      } else {
        // sync_cout << "No rollback needed - operation was successful" << std::endl;
      }
    };
    auto guard = std::unique_ptr<void, decltype(rollback)>(nullptr, rollback);

    // We've claimed this sequence number, now we can safely process it
    if (!node.ready.load(std::memory_order_acquire)) {
      // sync_cout << "Node is not ready" << std::endl;
      // sync_cout << "Rolling back nextToConsume_ to " << currentReadSeqNum << std::endl;
      nextToConsume_.store(currentReadSeqNum, std::memory_order_release);

      return std::nullopt;
    }

    EventType* evt = node.event.load(std::memory_order_acquire);
    if (!evt || evt->seqNum_ != currentReadSeqNum) {
      // sync_cout << "Event is not the correct sequence number" << std::endl;
      // sync_cout << "Rolling back nextToConsume_ to " << currentReadSeqNum << std::endl;
      nextToConsume_.store(currentReadSeqNum, std::memory_order_release);

      return std::nullopt;
    }

    // sync_cout << "Event is the correct sequence number" << std::endl;

    // Move the event data
    node.event.store(nullptr, std::memory_order_release);
    node.ready.store(false, std::memory_order_release);

    if (!external_ack) {
      mark_processed(currentReadSeqNum);
    }

    return *evt;
  }

  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];

    // Record processing completion order
    {
      std::lock_guard<std::mutex> lock(process_mut);
      dequeue_order_.push_back(seqNum);
    }

    // Mark this node as processed
    node.processed.store(true, std::memory_order_release);

    // sync_cout << "Marked processed event " << seqNum << std::endl;
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

    std::cout << std::endl;
  }

  // Get the order in which events were dequeued
  std::vector<std::size_t> get_dequeue_order() const {
    std::lock_guard<std::mutex> lock(dequeue_mut);
    return dequeue_order_;
  }

};

#endif
