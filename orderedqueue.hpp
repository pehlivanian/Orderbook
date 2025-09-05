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

const int NUM_RETRY_DEQUEUE = 2;  // Reduced retry count
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

  // Highly optimized Node with combined atomic state
  struct alignas(CACHE_LINE_SIZE) Node {
    EventType event;
    std::atomic<uint32_t> state{0};  // Combines ready/processed flags

    // State bit layout: [31:2] reserved, [1] processed, [0] ready
    static constexpr uint32_t READY_BIT = 1;
    static constexpr uint32_t PROCESSED_BIT = 2;
    
    // Pad to cache line size
    char padding[CACHE_LINE_SIZE - sizeof(EventType) - sizeof(std::atomic<uint32_t>)];
    
    Node() : event{}, state{0} {}
    
    // Hot path functions with always_inline optimization
    __attribute__((always_inline)) inline bool is_ready() const noexcept {
      return state.load(std::memory_order_acquire) & READY_BIT;
    }
    
    __attribute__((always_inline)) inline bool is_processed() const noexcept {
      return state.load(std::memory_order_acquire) & PROCESSED_BIT;
    }
    
    __attribute__((always_inline)) inline void set_ready() noexcept {
      state.fetch_or(READY_BIT, std::memory_order_release);
    }
    
    __attribute__((always_inline)) inline void clear_ready() noexcept {
      state.fetch_and(~READY_BIT, std::memory_order_release);
    }
    
    __attribute__((always_inline)) inline void set_processed() noexcept {
      state.fetch_or(PROCESSED_BIT, std::memory_order_release);
    }
    
    __attribute__((always_inline)) inline void reset() noexcept {
      state.store(0, std::memory_order_release);
    }
  };
  
  static_assert(sizeof(Node) == CACHE_LINE_SIZE, "Node must be exactly one cache line");

  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  
  // Separate hot counters to different cache lines
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};

#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
  // Track dequeue order separately from processing order
  mutable std::mutex dequeue_mut;
  std::vector<std::size_t> dequeue_order_;
#endif

  // Fast indexing with compile-time optimization
  __attribute__((always_inline, const)) inline size_t getIndex(size_t seqNum) const noexcept {
    return seqNum & MASK;
  }

 public:
  OrderedMPMCQueue() = default;

  ~OrderedMPMCQueue() = default;

  OrderedMPMCQueue(const OrderedMPMCQueue&) = delete;
  OrderedMPMCQueue& operator=(const OrderedMPMCQueue&) = delete;
  OrderedMPMCQueue(OrderedMPMCQueue&&) = delete;
  OrderedMPMCQueue& operator=(OrderedMPMCQueue&&) = delete;

  __attribute__((hot)) void enqueue(EventType event) {
    // Optimized spin with hardware pause
    int spin_count = 0;
    while (__builtin_expect(!try_enqueue(std::move(event)), 0)) {
      if (spin_count < 32) {
        _mm_pause();
      } else {
        std::this_thread::yield();
        spin_count = 0;
        continue;
      }
      ++spin_count;
    }
  }

  __attribute__((hot, flatten)) bool try_enqueue(EventType event) noexcept {
    const size_t seqNum = event.seqNum_;
    
    // Fast capacity check with branch prediction hints
    if (__builtin_expect(full(), 0)) [[unlikely]] {
      return false;
    }

    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    
    // Prefetch for better cache behavior
    _mm_prefetch(&buffer_[getIndex(seqNum + 4)], _MM_HINT_T0);
    
    // Fast path: check if slot is available
    uint32_t currentState = node.state.load(std::memory_order_acquire);
    if (__builtin_expect(currentState & Node::READY_BIT, 0)) [[unlikely]] {
      if (node.event.seqNum_ >= seqNum) {
        return false;  // Slot still in use
      }
    }

    // Store event and mark ready atomically
    node.event = std::move(event);
    node.state.store(Node::READY_BIT, std::memory_order_release);
    
    // Update write counter
    writeCount_.fetch_add(1, std::memory_order_relaxed);

    return true;
  }

  __attribute__((hot)) EventType dequeue(bool external_ack = false) {
    std::optional<EventType> opt;
    int spin_count = 0;
    
    while (__builtin_expect(!(opt = try_dequeue(external_ack)), 0)) {
      if (spin_count < 16) {
        _mm_pause();
      } else {
        std::this_thread::yield();
        spin_count = 0;
        continue;
      }
      ++spin_count;
    }
    
    return std::move(*opt);
  }

  bool try_dequeue(EventType& e, bool external_ack=false) {
    std::optional<EventType> opt = try_dequeue(external_ack);
    if (opt.has_value()) {
      e = std::move(*opt);
      return true;
    }
    return false;
  }

  __attribute__((hot, flatten)) std::optional<EventType> try_dequeue(bool external_ack = false) noexcept {
    // Load current sequence with acquire ordering
    size_t currentReadSeqNum = nextToConsume_.load(std::memory_order_acquire);
    const size_t idx = getIndex(currentReadSeqNum);
    Node& node = buffer_[idx];
    
    // Prefetch for sequential access
    _mm_prefetch(&buffer_[getIndex(currentReadSeqNum + 1)], _MM_HINT_T0);
    _mm_prefetch(&buffer_[getIndex(currentReadSeqNum + 2)], _MM_HINT_T1);
    
    // Fast ordering constraint check
    if (__builtin_expect(currentReadSeqNum > 0, 1)) [[likely]] {
      const Node& prevNode = buffer_[getIndex(currentReadSeqNum - 1)];
      uint32_t prevState = prevNode.state.load(std::memory_order_acquire);
      
      // if (__builtin_expect(!(prevState & Node::PROCESSED_BIT), 0)) [[unlikely]] {
      if (!(prevState & Node::PROCESSED_BIT)) {
        // Check if previous sequence is missing or not processed
        if (!(prevState & Node::READY_BIT) || 
            prevNode.event.seqNum_ == currentReadSeqNum - 1) {
          return std::nullopt;
        }
      }
    }

    // Try to claim this sequence number
    size_t expected = currentReadSeqNum;
    // if (__builtin_expect(!nextToConsume_.compare_exchange_strong(
    //     expected, currentReadSeqNum + 1,
    //     std::memory_order_acq_rel, std::memory_order_acquire), 0)) [[unlikely]] {
	if (!nextToConsume_.compare_exchange_strong(
						    expected, currentReadSeqNum + 1,
						    std::memory_order_acq_rel,
						    std::memory_order_acquire)) {
	  return std::nullopt;
    }

    // Optimized spin-wait for the event
    uint32_t state;
    for (int retry = 0; retry < NUM_RETRY_DEQUEUE; ++retry) {
      state = node.state.load(std::memory_order_acquire);
      
      if (__builtin_expect(state & Node::READY_BIT, 1)) [[likely]] {
        if (__builtin_expect(node.event.seqNum_ == currentReadSeqNum, 1)) [[likely]] {
          goto process_event;
        }
      }
      
      if (__builtin_expect(retry == NUM_RETRY_DEQUEUE - 1, 0)) [[unlikely]] {
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
      
      // Hardware pause for better spin performance
      _mm_pause();
    }
    
  process_event:
    // Record dequeue order if needed
#ifdef MAINTAIN_INTERNAL_DEQUEUE_ORDER
    {
      std::lock_guard<std::mutex> lock(dequeue_mut);
      dequeue_order_.push_back(currentReadSeqNum);
    }
#endif

    // Copy the event
    EventType result = node.event;
    
    if (!external_ack) {
      // Mark processed immediately
      node.state.store(Node::PROCESSED_BIT, std::memory_order_release);
    } else {
      // Just clear ready bit
      node.clear_ready();
    }

    return result;
  }

  __attribute__((always_inline)) inline void mark_processed(size_t seqNum) noexcept {
    const size_t idx = getIndex(seqNum);
    Node& node = buffer_[idx];
    node.set_processed();
  }

  __attribute__((always_inline, pure)) inline bool empty() const noexcept {
    size_t current = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(current);
    return !buffer_[idx].is_ready();
  }

  bool full() const {
    return writeCount_.load(std::memory_order_relaxed) >=
           nextToConsume_.load(std::memory_order_relaxed) + Capacity;
  }

  __attribute__((always_inline, pure)) inline size_t size() const noexcept {
    size_t writeCount = writeCount_.load(std::memory_order_relaxed);
    size_t nextToConsume = nextToConsume_.load(std::memory_order_relaxed);
    return writeCount >= nextToConsume ? writeCount - nextToConsume : 0;
  }

  __attribute__((always_inline, pure)) inline size_t getNextToConsume() const noexcept {
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
