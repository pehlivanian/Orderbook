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
#include <thread>
#include <unordered_map>
#include <vector>


const int NUM_RETRY_DEQUEUE = 10;
const int CACHE_LINE_SIZE = 64;

using namespace Numerics;
using namespace Utils;
using namespace std::chrono_literals;

template <typename EventType, size_t RequestCapacity = 2 << 20>
class OrderedMPMCQueue {
 private:
  static constexpr std::size_t Capacity =
      isPowerOfTwo(RequestCapacity) ? RequestCapacity : nextPowerOfTwo(RequestCapacity);

  static constexpr std::size_t MASK = Capacity - 1;

  struct alignas(CACHE_LINE_SIZE) Node {
    EventType event;
    std::atomic<bool> ready{false};
    std::atomic<bool> processed{false};
    
    Node() : event{}, ready{false}, processed{false} {}
  };
  
  // Verify that the compiler gave us what we wanted
  static_assert(sizeof(Node) >= CACHE_LINE_SIZE, "Node must be at least cache line size");
  static_assert(sizeof(Node) % CACHE_LINE_SIZE == 0, "Node must be multiple of cache line size");


  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
  // Track dequeue order separately from processing order
  mutable std::mutex dequeue_mut;
  std::vector<std::size_t> dequeue_order_;
#endif

  size_t getIndex(size_t seqNum) const { return seqNum & MASK; }

 public:
  OrderedMPMCQueue() = default;

  ~OrderedMPMCQueue() = default;

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

    // Simple capacity check with relaxed memory ordering for better performance
    size_t writeCount = writeCount_.load(std::memory_order_relaxed);
    size_t nextToConsume = nextToConsume_.load(std::memory_order_relaxed);
    
    if (writeCount >= nextToConsume + Capacity) {
      return false;  // Queue is full
    }

    Node& node = buffer_[idx];

    if (node.ready.load(std::memory_order_acquire)) {
      if (node.event.seqNum_ >= seqNum) {
        // Slot still in use
	return false;
      }
    }

    // First, reset the node state
    node.ready.store(false, std::memory_order_release);
    node.processed.store(false, std::memory_order_release);
    
    // Then set the event directly (no allocation needed)
    node.event = std::move(event);

    // Finally, mark as ready (this is the signal that the event is available)
    node.ready.store(true, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_relaxed);


    return true;
  }

  EventType dequeue(bool external_ack=false) {
    EventType e;
    while (!try_dequeue(e, external_ack)) {
      std::this_thread::yield();
    }
    return e;
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

    Node& node = buffer_[idx];
    
    // Check if previous sequence was processed (ordering constraint)
    if (currentReadSeqNum > 0) {
      size_t prevSeqNum = currentReadSeqNum - 1;
      size_t prevIdx = getIndex(prevSeqNum);
      Node& prevNode = buffer_[prevIdx];
      
      // Check if the previous sequence was processed
      bool prevProcessed = prevNode.processed.load(std::memory_order_acquire);
      bool prevReady = prevNode.ready.load(std::memory_order_acquire);
      
      if (!prevProcessed) {
        // If the previous slot is not ready, the sequence wasn't enqueued yet
        if (!prevReady) {
          return std::nullopt;
        } else if (prevNode.event.seqNum_ == prevSeqNum) {
          return std::nullopt;
        } 
      }
    }

    // Try to claim this sequence number
    size_t expected = currentReadSeqNum;
    if (!nextToConsume_.compare_exchange_strong(expected, currentReadSeqNum + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
      return std::nullopt;
    }


    // We've claimed this sequence number, now we can safely process it
    // Wait for the node to be ready and contain the correct event
    for (int retry = 0; retry < NUM_RETRY_DEQUEUE; ++retry) {
      bool ready = node.ready.load(std::memory_order_acquire);
      
      
      if (ready) {
        if (node.event.seqNum_ == currentReadSeqNum) {
          // Found the correct event, record dequeue order and proceed
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
          {
            std::lock_guard<std::mutex> lock(dequeue_mut);
            dequeue_order_.push_back(currentReadSeqNum);
          }
#endif
          goto process_event;
        } else {
        }
      }
      
      if (retry == -1 + NUM_RETRY_DEQUEUE) {
        // Failed to get the event - need to restore nextToConsume since we claimed it
        // Restore to the minimum of current value and our timeout sequence
        size_t current = nextToConsume_.load(std::memory_order_acquire);
        while (currentReadSeqNum < current) {
          if (nextToConsume_.compare_exchange_weak(current, currentReadSeqNum, 
                                                   std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
          }
          // compare_exchange_weak updates current with the actual value, retry
        }
        return std::nullopt;
      }
      std::this_thread::yield();
    }
    
  process_event:

    // Move the event data
    EventType result = node.event;  // Copy the event
    
    if (!external_ack) {
      // Mark processed immediately
      node.ready.store(false, std::memory_order_release);
      mark_processed(currentReadSeqNum);
    } else {
      // Don't mark processed yet - mark_processed will do it
      node.ready.store(false, std::memory_order_release);
    }

    return result;
  }

  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];

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
    size_t writeCount = writeCount_.load(std::memory_order_relaxed);
    size_t nextToConsume = nextToConsume_.load(std::memory_order_relaxed);
    return writeCount >= nextToConsume ? writeCount - nextToConsume : 0;
  }

  size_t getNextToConsume() const {
    return nextToConsume_.load(std::memory_order_relaxed);
  }

#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
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

  void clear_dequeue_order() {
    std::lock_guard<std::mutex> lock(dequeue_mut);
    dequeue_order_.clear();
  }
#endif

  void reset() {
    // Clear all nodes completely
    for (auto& node : buffer_) {
      node.ready.store(false, std::memory_order_release);
      node.processed.store(false, std::memory_order_release);
    }
    
    // Reset counters with full memory barriers
    writeCount_.store(0, std::memory_order_seq_cst);
    nextToConsume_.store(0, std::memory_order_seq_cst);
    
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
    // Clear tracking vectors
    {
      std::lock_guard<std::mutex> lock(dequeue_mut);
      dequeue_order_.clear();
    }
#endif
    
    // Additional synchronization to ensure all threads see the reset
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

};

#endif
