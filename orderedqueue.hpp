#include <atomic>
#include <memory>
#include <optional>
#include <array>
#include <cstddef>
#include <thread>


namespace numerics {
  
  constexpr bool isPowerOfTwo(std::size_t x) {
    return x && !(x & (x - 1));
  }

  constexpr std::size_t nextPowerOfTwo(std::size_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x++;
    return x;
  }
}


template<typename EventType, size_t RequestCapacity>
class OrderedMPMCQueue {
private:

  static constexpr std::size_t Capacity = numerics::isPowerOfTwo(RequestCapacity)  ?
    RequestCapacity : numerics::nextPowerOfTwo(RequestCapacity);
  
  static constexpr std::size_t MASK = Capacity - 1;

  struct Node {
    std::atomic<EventType*> event{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<bool> consumed{false};
  };

  static constexpr size_t CACHE_LINE_SIZE = 64;
    
  // Avoid false sharing
  alignas(CACHE_LINE_SIZE) std::array<Node, Capacity> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> writeCount_{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextToConsume_{0};
  
  // Get buffer index from sequence number
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
    const size_t seqNum = event.seqNumber_;
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
      if (oldEvent && oldEvent->seqNumber_ >= seqNum) {
	return false;  // Slot still in use
      }
    }

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
    writeCount_.fetch_add(1, std::memory_order_release);
        
    return true;
  }

  std::optional<EventType> try_dequeue() {
    size_t currentSeq = nextToConsume_.load(std::memory_order_relaxed);
    const size_t idx = getIndex(currentSeq);
        
    Node& node = buffer_[idx];
        
    // Check if the next event is ready
    if (!node.ready.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    // Get the event and verify sequence number
    EventType* event = node.event.load(std::memory_order_acquire);
    if (!event || event->seqNumber_ != currentSeq) {
      return std::nullopt;
    }

    // Try to mark as consumed
    bool expected = false;
    if (!node.consumed.compare_exchange_strong(expected, true,
					       std::memory_order_acq_rel)) {
      return std::nullopt;
    }

    // Successfully dequeued, increment next to consume
    nextToConsume_.fetch_add(1, std::memory_order_release);
        
    // Reset node state
    node.ready.store(false, std::memory_order_release);
        
    // Move event data and cleanup
    EventType result = std::move(*event);
    delete event;
    node.event.store(nullptr, std::memory_order_release);
        
    return result;
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
