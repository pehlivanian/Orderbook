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
#include <unordered_map>
#include <vector>

#define sync_cout std::osyncstream(std::cout)

#define DEBUG
#undef DEBUG

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
    
    // Pad to cache line boundary to prevent false sharing
    char padding[CACHE_LINE_SIZE - sizeof(EventType) - 2 * sizeof(std::atomic<bool>)];
  };


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

    // sync_cout << "Enqueued event with seqNum_: " << node.event.seqNum_ << std::endl;

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
    auto thread_id = std::this_thread::get_id();

    Node& node = buffer_[idx];
    bool nodeReady = node.ready.load(std::memory_order_acquire);
    bool nodeProcessed = node.processed.load(std::memory_order_acquire);
    
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

#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] CLAIMED: seq " << currentReadSeqNum << ", nextToConsume_ now " << (currentReadSeqNum + 1) << std::endl;
#endif

    // We've claimed this sequence number, now we can safely process it
    // Wait for the node to be ready and contain the correct event
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Starting retry loop to wait for correct event" << std::endl;
#endif
    for (int retry = 0; retry < NUM_RETRY_DEQUEUE; ++retry) {
      bool ready = node.ready.load(std::memory_order_acquire);
      
#ifdef DEBUG
      if (retry % 100 == 0 || retry < 10 || ready) {
        sync_cout << "[DEBUG][T" << thread_id << "] Retry " << retry << ": ready=" << ready;
        if (ready) {
          sync_cout << " (seqNum=" << node.event.seqNum_ << ")";
        }
        sync_cout << std::endl;
      }
#endif
      
      if (ready) {
        if (node.event.seqNum_ == currentReadSeqNum) {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Found correct event at retry " << retry << ", proceeding to process" << std::endl;
#endif
          // Found the correct event, record dequeue order and proceed
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
          {
            std::lock_guard<std::mutex> lock(dequeue_mut);
            dequeue_order_.push_back(currentReadSeqNum);
#ifdef DEBUG
            sync_cout << "[DEBUG][T" << thread_id << "] Added " << currentReadSeqNum << " to dequeue_order" << std::endl;
#endif
          }
#endif
          goto process_event;
        } else {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Event exists but wrong sequence: got " << node.event.seqNum_ 
                    << ", expected " << currentReadSeqNum << std::endl;
#endif
        }
      }
      
      if (retry == -1 + NUM_RETRY_DEQUEUE) {
#ifdef DEBUG
        sync_cout << "[DEBUG][T" << thread_id << "] Failed to get event after 10000 retries - attempting to restore nextToConsume_ to " << currentReadSeqNum << std::endl;
#endif
        // Failed to get the event - need to restore nextToConsume since we claimed it
        // Restore to the minimum of current value and our timeout sequence
        size_t current = nextToConsume_.load(std::memory_order_acquire);
        while (currentReadSeqNum < current) {
          if (nextToConsume_.compare_exchange_weak(current, currentReadSeqNum, 
                                                   std::memory_order_acq_rel, std::memory_order_acquire)) {
#ifdef DEBUG
            sync_cout << "[DEBUG][T" << thread_id << "] Successfully restored nextToConsume_ from " << current << " to " << currentReadSeqNum << std::endl;
#endif
            break;
          }
          // compare_exchange_weak updates current with the actual value, retry
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Restore retry: current=" << current << ", target=" << currentReadSeqNum << std::endl;
#endif
        }
#ifdef DEBUG
        if (currentReadSeqNum >= current) {
          sync_cout << "[DEBUG][T" << thread_id << "] No restore needed: currentReadSeqNum=" << currentReadSeqNum << " >= current=" << current << std::endl;
        }
#endif
        return std::nullopt;
      }
      std::this_thread::yield();
    }
    
  process_event:
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Processing event: seqNum=" << node.event.seqNum_ << ", external_ack=" << external_ack << std::endl;
#endif

    // Move the event data
    EventType result = node.event;  // Copy the event
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Copied event data" << std::endl;
#endif
    
    if (!external_ack) {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] No external ack - marking processed immediately" << std::endl;
#endif
      // Mark processed immediately
      node.ready.store(false, std::memory_order_release);
      mark_processed(currentReadSeqNum);
    } else {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] External ack mode - only setting ready=false, will mark processed later" << std::endl;
#endif
      // Don't mark processed yet - mark_processed will do it
      node.ready.store(false, std::memory_order_release);
    }

#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] try_dequeue() returning event with seqNum=" << result.seqNum_ << std::endl;
#endif
    return result;
  }

  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];

    // Mark this node as processed
    node.processed.store(true, std::memory_order_release);

#ifdef DEBUG
    sync_cout << "Marked processed event " << seqNum << std::endl;
#endif
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
