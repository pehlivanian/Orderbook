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

  size_t getIndex(size_t seqNum) const { return seqNum & MASK; }

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

    // Simple capacity check - don't allow more than Capacity pending events
    size_t writeCount = writeCount_.load(std::memory_order_relaxed);
    size_t nextToConsume = nextToConsume_.load(std::memory_order_relaxed);
    
    if (writeCount >= nextToConsume + Capacity) {
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
    
    // First, reset the node state
    node.ready.store(false, std::memory_order_release);
    node.processed.store(false, std::memory_order_release);
    
    // Then set the event
    EventType* expected = nullptr;
    if (!node.event.compare_exchange_strong(expected, newEvent, std::memory_order_release,
                                            std::memory_order_relaxed)) {
      delete newEvent;
      return false;
    }

    // Finally, mark as ready (this is the signal that the event is available)
    node.ready.store(true, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_release);

    // sync_cout << "Enqueued event with seqNum_: " << newEvent->seqNum_ << std::endl;

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

#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] try_dequeue() called with external_ack=" << external_ack << std::endl;
    sync_cout << "[DEBUG][T" << thread_id << "] currentReadSeqNum=" << currentReadSeqNum << ", idx=" << idx << std::endl;
    sync_cout << "[DEBUG][T" << thread_id << "] writeCount_=" << writeCount_.load() << ", nextToConsume_=" << nextToConsume_.load() << std::endl;
#endif

    Node& node = buffer_[idx];
    bool nodeReady = node.ready.load(std::memory_order_acquire);
    bool nodeProcessed = node.processed.load(std::memory_order_acquire);
    EventType* nodeEvent = node.event.load(std::memory_order_acquire);
    
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Current node[" << idx << "]: ready=" << nodeReady 
              << ", processed=" << nodeProcessed 
              << ", event=" << (nodeEvent ? "exists" : "null");
    if (nodeEvent) {
      sync_cout << " (seqNum=" << nodeEvent->seqNum_ << ")";
    }
    sync_cout << std::endl;
#endif

    // Check if previous sequence was processed (ordering constraint)
    if (currentReadSeqNum > 0) {
      size_t prevSeqNum = currentReadSeqNum - 1;
      size_t prevIdx = getIndex(prevSeqNum);
      Node& prevNode = buffer_[prevIdx];
      
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] Checking previous sequence: prevSeqNum=" << prevSeqNum << ", prevIdx=" << prevIdx << std::endl;
#endif
      
      // Check if the previous sequence was processed
      bool prevProcessed = prevNode.processed.load(std::memory_order_acquire);
      EventType* prevEvent = prevNode.event.load(std::memory_order_acquire);
      bool prevReady = prevNode.ready.load(std::memory_order_acquire);
      
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] Previous node[" << prevIdx << "]: ready=" << prevReady 
                << ", processed=" << prevProcessed 
                << ", event=" << (prevEvent ? "exists" : "null");
      if (prevEvent) {
        sync_cout << " (seqNum=" << prevEvent->seqNum_ << ")";
      }
      sync_cout << std::endl;
#endif
      
      if (!prevProcessed) {
#ifdef DEBUG
        sync_cout << "[DEBUG][T" << thread_id << "] Previous sequence not processed yet" << std::endl;
#endif
        
        // If the previous slot is empty, the sequence was processed and cleared
        if (!prevEvent) {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Previous slot is empty - sequence was processed and cleared, continuing" << std::endl;
#endif
        } else if (prevEvent->seqNum_ == prevSeqNum) {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Waiting for exact previous sequence " << prevSeqNum << " to be processed - blocking" << std::endl;
#endif
          return std::nullopt;
        } else {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Previous slot contains different sequence (" << prevEvent->seqNum_ 
                    << " != " << prevSeqNum << ") - wraparound case, continuing" << std::endl;
#endif
        }
      } else {
#ifdef DEBUG
        sync_cout << "[DEBUG][T" << thread_id << "] Previous sequence was already processed, continuing" << std::endl;
#endif
      }
    } else {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] This is sequence 0, no previous sequence to check" << std::endl;
#endif
    }

    // Try to claim this sequence number
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Attempting to claim sequence " << currentReadSeqNum << std::endl;
#endif
    size_t expected = currentReadSeqNum;
    if (!nextToConsume_.compare_exchange_strong(expected, currentReadSeqNum + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] CLAIM FAILED: Thread failed to claim seq " << currentReadSeqNum 
                << ", expected " << expected << " but nextToConsume_ is " << nextToConsume_.load() << std::endl;
#endif
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
    for (int retry = 0; retry < 1000; ++retry) {
      bool ready = node.ready.load(std::memory_order_acquire);
      EventType* evt = node.event.load(std::memory_order_acquire);
      
#ifdef DEBUG
      if (retry % 100 == 0 || retry < 10 || ready) {
        sync_cout << "[DEBUG][T" << thread_id << "] Retry " << retry << ": ready=" << ready 
                  << ", event=" << (evt ? "exists" : "null");
        if (evt) {
          sync_cout << " (seqNum=" << evt->seqNum_ << ")";
        }
        sync_cout << std::endl;
      }
#endif
      
      if (ready) {
        if (evt && evt->seqNum_ == currentReadSeqNum) {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Found correct event at retry " << retry << ", proceeding to process" << std::endl;
#endif
          // Found the correct event, record dequeue order and proceed
          {
            std::lock_guard<std::mutex> lock(dequeue_mut);
            dequeue_order_.push_back(currentReadSeqNum);
#ifdef DEBUG
            sync_cout << "[DEBUG][T" << thread_id << "] Added " << currentReadSeqNum << " to dequeue_order" << std::endl;
#endif
          }
          goto process_event;
        } else if (evt) {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Event exists but wrong sequence: got " << evt->seqNum_ 
                    << ", expected " << currentReadSeqNum << std::endl;
#endif
        } else {
#ifdef DEBUG
          sync_cout << "[DEBUG][T" << thread_id << "] Node is ready but event is null" << std::endl;
#endif
        }
      }
      
      if (retry == 999) {
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
    EventType* evt = node.event.load(std::memory_order_acquire);
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Processing event: seqNum=" << evt->seqNum_ << ", external_ack=" << external_ack << std::endl;
#endif

    // Move the event data
    EventType result = *evt;  // Copy the event before deleting
#ifdef DEBUG
    sync_cout << "[DEBUG][T" << thread_id << "] Copied event data" << std::endl;
#endif
    
    if (!external_ack) {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] No external ack - deleting event and marking processed immediately" << std::endl;
#endif
      // Delete immediately and mark processed
      delete evt;
      node.event.store(nullptr, std::memory_order_release);
      node.ready.store(false, std::memory_order_release);
      mark_processed(currentReadSeqNum);
    } else {
#ifdef DEBUG
      sync_cout << "[DEBUG][T" << thread_id << "] External ack mode - only setting ready=false, will delete in mark_processed" << std::endl;
#endif
      // Don't delete yet - mark_processed will do it
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

    // Delete the event if it's still there (external_ack case)
    EventType* evt = node.event.load(std::memory_order_acquire);
    if (evt) {
      delete evt;
      node.event.store(nullptr, std::memory_order_release);
    }

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

  void reset() {
    // Clear all nodes completely
    for (auto& node : buffer_) {
      EventType* event = node.event.load(std::memory_order_relaxed);
      if (event) {
        delete event;
      }
      node.event.store(nullptr, std::memory_order_release);
      node.ready.store(false, std::memory_order_release);
      node.processed.store(false, std::memory_order_release);
    }
    
    // Reset counters with full memory barriers
    writeCount_.store(0, std::memory_order_seq_cst);
    nextToConsume_.store(0, std::memory_order_seq_cst);
    
    // Clear tracking vectors
    {
      std::lock_guard<std::mutex> lock(dequeue_mut);
      dequeue_order_.clear();
    }
    
    // Additional synchronization to ensure all threads see the reset
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

};

#endif
