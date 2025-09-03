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
#include <immintrin.h>  // For prefetch intrinsics
#include <x86intrin.h>  // For CPU-specific optimizations

const int NUM_RETRY_DEQUEUE = 3;  // Reduced from 10
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

  // Optimized cache-line sized Node with combined atomic state
  struct alignas(CACHE_LINE_SIZE) Node {
    EventType event;
    std::atomic<uint32_t> state{0};  // Combines ready/processed into single atomic
    
    // State bit layout: [31:2] reserved, [1] processed, [0] ready
    static constexpr uint32_t READY_BIT = 1;
    static constexpr uint32_t PROCESSED_BIT = 2;
    
    // Pad to cache line size
    char padding[CACHE_LINE_SIZE - sizeof(EventType) - sizeof(std::atomic<uint32_t>)];
    
    Node() : event{}, state{0} {}
    
    inline bool is_ready() const noexcept {
      return state.load(std::memory_order_acquire) & READY_BIT;
    }
    
    inline bool is_processed() const noexcept {
      return state.load(std::memory_order_acquire) & PROCESSED_BIT;
    }
    
    inline void set_ready() noexcept {
      state.fetch_or(READY_BIT, std::memory_order_release);
    }
    
    inline void clear_ready() noexcept {
      state.fetch_and(~READY_BIT, std::memory_order_release);
    }
    
    inline void set_processed() noexcept {
      state.fetch_or(PROCESSED_BIT, std::memory_order_release);
    }
    
    inline void reset() noexcept {
      state.store(0, std::memory_order_release);
    }
  };
  
  static_assert(sizeof(Node) == CACHE_LINE_SIZE, "Node must be exactly one cache line");


  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  
  // Separate hot data to different cache lines to reduce false sharing
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
  // Track dequeue order separately from processing order
  mutable std::mutex dequeue_mut;
  std::vector<std::size_t> dequeue_order_;
#endif

  inline size_t getIndex(size_t seqNum) const noexcept {
    return seqNum & MASK;
  }

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
    
    // Fast capacity check with relaxed ordering
    if (__builtin_expect(full(), 0)) {
      return false;
    }

    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    
    // Prefetch next cache line for sequential access pattern
    _mm_prefetch(&buffer_[getIndex(seqNum + 4)], _MM_HINT_T0);
    
    // Check if slot is available using single atomic load
    uint32_t currentState = node.state.load(std::memory_order_acquire);
    if (__builtin_expect(currentState & Node::READY_BIT, 0)) {
      if (node.event.seqNum_ >= seqNum) {
        return false;  // Slot still in use
      }
    }

    // Reset state and set event in one go to reduce cache line bouncing
    node.event = std::move(event);
    node.state.store(Node::READY_BIT, std::memory_order_release);
    
    // Update write counter with relaxed ordering
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
    // Load current sequence with acquire ordering
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_acquire);
    const size_t idx = getIndex(currentReadSeqNum);
    Node& node = buffer_[idx];
    
    // Prefetch next nodes for sequential access
    _mm_prefetch(&buffer_[getIndex(currentReadSeqNum + 1)], _MM_HINT_T0);
    _mm_prefetch(&buffer_[getIndex(currentReadSeqNum + 2)], _MM_HINT_T1);
    
    // Fast path: Check ordering constraint with minimal atomic ops
    if (__builtin_expect(currentReadSeqNum > 0, 1)) {
      const Node& prevNode = buffer_[getIndex(currentReadSeqNum - 1)];
      uint32_t prevState = prevNode.state.load(std::memory_order_acquire);
      
      if (__builtin_expect(!(prevState & Node::PROCESSED_BIT), 0)) {
        // Check if previous sequence is missing or not processed
        if (!(prevState & Node::READY_BIT) || 
            prevNode.event.seqNum_ == currentReadSeqNum - 1) {
          return std::nullopt;
        }
      }
    }

    // Try to claim this sequence number with compare-and-swap
    size_t expected = currentReadSeqNum;
    if (__builtin_expect(!nextToConsume_.compare_exchange_strong(
        expected, currentReadSeqNum + 1,
        std::memory_order_acq_rel, std::memory_order_acquire), 0)) {
      return std::nullopt;
    }

    // We've claimed the sequence number, now wait for the event
    uint32_t state;
    for (int retry = 0; retry < NUM_RETRY_DEQUEUE; ++retry) {
      state = node.state.load(std::memory_order_acquire);
      
      if (__builtin_expect(state & Node::READY_BIT, 1)) {
        if (__builtin_expect(node.event.seqNum_ == currentReadSeqNum, 1)) {
          goto process_event;
        }
      }
      
      if (__builtin_expect(retry == NUM_RETRY_DEQUEUE - 1, 0)) {
        // Cleanup: restore the sequence number
        size_t current = nextToConsume_.load(std::memory_order_acquire);
        while (currentReadSeqNum < current) {
          if (nextToConsume_.compare_exchange_weak(
              current, currentReadSeqNum,
              std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
          }
        }
        return std::nullopt;
      }
      
      // Exponential backoff with CPU pause
      for (int i = 0; i < (1 << retry); ++i) {
        _mm_pause();  // x86 PAUSE instruction for better spin-wait
      }
    }
    
  process_event:
    // Record dequeue order if needed
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
    {
      std::lock_guard<std::mutex> lock(dequeue_mut);
      dequeue_order_.push_back(currentReadSeqNum);
    }
#endif

    // Copy the event (compiler will optimize this)
    EventType result = node.event;
    
    if (!external_ack) {
      // Mark processed immediately with single atomic operation
      node.state.store(Node::PROCESSED_BIT, std::memory_order_release);
    } else {
      // Just clear ready bit
      node.clear_ready();
    }

    return result;
  }

  void mark_processed(size_t seqNum) {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    // Set processed bit while preserving other state
    node.set_processed();
  }

  bool empty() const {
    size_t current = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(current);
    return !buffer_[idx].is_ready();
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
    // Clear all nodes efficiently
    for (auto& node : buffer_) {
      node.reset();
    }
    
    // Reset counters with sequential consistency
    writeCount_.store(0, std::memory_order_seq_cst);
    nextToConsume_.store(0, std::memory_order_seq_cst);
    
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
    {
      std::lock_guard<std::mutex> lock(dequeue_mut);
      dequeue_order_.clear();
    }
#endif
    
    // Full memory barrier
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

};

#endif
