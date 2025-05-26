#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <numeric>
#include <random>
#include <syncstream>
#include <thread>
#include <vector>

#include "forwardlistadaptor.hpp"
#include "orderbook.hpp"
#include "orderedqueue.hpp"
#include "tradejournal.hpp"

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

struct TestEvent {
  unsigned long seqNum_;
  int data_;
  std::string metadata_;

  TestEvent(unsigned long seq, int data) : seqNum_(seq), data_(data) {}
  TestEvent(unsigned long seq, int data, std::string metadata)
      : seqNum_(seq), data_(data), metadata_(metadata) {}
  TestEvent() : seqNum_(0), data_(0), metadata_{} {}
};

class OrderedMPMCQueueTest : public ::testing::Test {
 protected:
  static constexpr size_t QUEUE_CAPACITY = 1024;
  OrderedMPMCQueue<TestEvent, QUEUE_CAPACITY> queue;
  
  void SetUp() override {
    queue.reset();
  }
};


// Test 1: Basic single-threaded enqueue and dequeue
TEST_F(OrderedMPMCQueueTest, BasicEnqueueDequeue) {
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 42)));
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);
  EXPECT_EQ(result->data_, 42);
}

// Test 2: Queue handles empty state correctly
TEST_F(OrderedMPMCQueueTest, EmptyQueueBehavior) {
  EXPECT_TRUE(queue.empty());
  auto result = queue.try_dequeue();
  EXPECT_FALSE(result.has_value());
}

// Test 3: Queue respects capacity
TEST_F(OrderedMPMCQueueTest, RespectCapacity) {
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  EXPECT_TRUE(queue.full());
  EXPECT_FALSE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 42)));
}

// Test 4: Test ordered consumption with out-of-order insertion
TEST_F(OrderedMPMCQueueTest, OrderedConsumption) {
  // Insert out of order
  ASSERT_TRUE(queue.try_enqueue(TestEvent(2, 2)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 0)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(1, 1)));

  // Should receive in order
  for (int i = 0; i < 3; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    EXPECT_EQ(result->data_, i);
  }

  ASSERT_TRUE(queue.empty());

  // Insert out of order
  ASSERT_TRUE(queue.try_enqueue(TestEvent(4, 4)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(6, 6)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(8, 8)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(3, 3)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(5, 5)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(7, 7)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(9, 9)));
  
  for (int i=3; i<10; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    EXPECT_EQ(result->data_, i);
  }
	      
}

// Test 5: Multi-threaded enqueue
TEST_F(OrderedMPMCQueueTest, MultiThreadedEnqueue) {
  static constexpr size_t NUM_THREADS = 4;
  static constexpr size_t EVENTS_PER_THREAD = 100;

  std::vector<std::thread> threads;
  for (size_t t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&, t]() {
      for (size_t i = 0; i < EVENTS_PER_THREAD; ++i) {
        size_t seq = t * EVENTS_PER_THREAD + i;
        queue.enqueue(TestEvent(seq, seq));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Verify all events can be dequeued in order
  for (size_t i = 0; i < NUM_THREADS * EVENTS_PER_THREAD; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    EXPECT_EQ(result->data_, i);
  }
}

// Test 6: Multi-threaded random order enqueue
TEST_F(OrderedMPMCQueueTest, RandomOrderEnqueue) {
  static constexpr size_t NUM_EVENTS = 100;

  std::vector<size_t> indices(NUM_EVENTS);
  std::iota(indices.begin(), indices.end(), 0);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(indices.begin(), indices.end(), gen);

  std::vector<std::thread> threads;
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    threads.emplace_back([&, i]() { queue.enqueue(TestEvent(indices[i], indices[i])); });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Verify ordered dequeue
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    EXPECT_EQ(result->data_, i);
  }
}

// Test 7: Multiple consumers
TEST_F(OrderedMPMCQueueTest, MultipleConsumers) {
  static constexpr size_t NUM_EVENTS = 100;
  static constexpr size_t NUM_CONSUMERS = 4;

  // First enqueue all events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  std::atomic<size_t> total_consumed{0};
  std::vector<std::thread> consumers;

  for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
    consumers.emplace_back([&]() {
      while (total_consumed.load() < NUM_EVENTS) {
        if (auto result = queue.try_dequeue()) {
          EXPECT_EQ(result->data_, result->seqNum_);
          total_consumed.fetch_add(1);
        }
      }
    });
  }

  for (auto& consumer : consumers) {
    consumer.join();
  }

  EXPECT_EQ(total_consumed.load(), NUM_EVENTS);
}

// Test 8: Concurrent enqueue and dequeue
TEST_F(OrderedMPMCQueueTest, ConcurrentEnqueueDequeue) {
  static constexpr size_t NUM_PRODUCERS = 4;
  static constexpr size_t NUM_CONSUMERS = 4; 
  static constexpr size_t EVENTS_PER_PRODUCER = 100;

  std::atomic<size_t> total_consumed{0};
  std::atomic<bool> producers_done{false};
  std::atomic<bool> producers_started{false};
  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  // Start producers first
  for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
    producers.emplace_back([&, p]() {
      for (size_t i = 0; i < EVENTS_PER_PRODUCER; ++i) {
        size_t seq = p * EVENTS_PER_PRODUCER + i;
        queue.enqueue(TestEvent(seq, seq));
      }
    });
  }
  producers_started = true;

  // Start consumers after producers have started
  for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
    consumers.emplace_back([&]() {
      // Wait for producers to start
      while (!producers_started.load()) {
        std::this_thread::yield();
      }
      
      auto start_time = std::chrono::steady_clock::now();
      while (total_consumed.load() < NUM_PRODUCERS * EVENTS_PER_PRODUCER) {
        // Add timeout to prevent infinite loops  
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 15) {
          std::cout << "Consumer timeout. Total consumed: " << total_consumed.load() 
                   << "/" << NUM_PRODUCERS * EVENTS_PER_PRODUCER << std::endl;
          break; // Timeout after 15 seconds
        }
        
        if (auto result = queue.try_dequeue()) {
          EXPECT_EQ(result->data_, result->seqNum_);
          total_consumed.fetch_add(1);
        } else {
          // If we can't dequeue and producers are done, we might be stuck
          if (producers_done.load() && queue.empty()) {
            break;
          }
          std::this_thread::yield(); // Give other threads a chance
        }
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  producers_done = true;
  
  for (auto& consumer : consumers) {
    consumer.join();
  }

  EXPECT_EQ(total_consumed.load(), NUM_PRODUCERS * EVENTS_PER_PRODUCER);
}

// Test 9: Queue size tracking
TEST_F(OrderedMPMCQueueTest, SizeTracking) {
  EXPECT_EQ(queue.size(), 0);

  queue.try_enqueue(TestEvent(0, 0));
  EXPECT_EQ(queue.size(), 1);

  queue.try_enqueue(TestEvent(1, 1));
  EXPECT_EQ(queue.size(), 2);

  auto result = queue.try_dequeue();
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(queue.size(), 1);

  result = queue.try_dequeue();
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(queue.size(), 0);
}

// Test 10: Blocking enqueue behavior
TEST_F(OrderedMPMCQueueTest, BlockingEnqueue) {
  // Fill the queue
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  std::atomic<bool> enqueue_completed{false};
  std::thread blocking_thread([&]() {
    queue.enqueue(TestEvent(QUEUE_CAPACITY, 42));
    enqueue_completed.store(true);
  });

  // Give the blocking thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(enqueue_completed.load());

  // Make space in the queue
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());

  // Wait for blocking thread to complete
  blocking_thread.join();
  EXPECT_TRUE(enqueue_completed.load());
}

// Test 1: Sequence Number Gaps
TEST_F(OrderedMPMCQueueTest, SequenceGaps) {
  // Insert events with one gap
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 0)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(2, 2)));

  // Should get first event only
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);

  // Shouldn't get more events yet (sequence 1 is missing)
  result = queue.try_dequeue();
  EXPECT_FALSE(result.has_value());

  // Fill the gap
  ASSERT_TRUE(queue.try_enqueue(TestEvent(1, 1)));

  // Now should get the next event
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 1);
  
  // And then the last one
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 2);
}

// Test 2: Alternating Producer Consumer
TEST_F(OrderedMPMCQueueTest, AlternatingProducerConsumer) {
  static constexpr size_t NUM_ROUNDS = 1000;
  std::atomic<bool> producers_done{false};

  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_ROUNDS; i += 2) {
      queue.enqueue(TestEvent(i, i));
    }
  });

  std::thread producer2([&]() {
    for (size_t i = 1; i < NUM_ROUNDS; i += 2) {
      queue.enqueue(TestEvent(i, i));
    }
  });

  std::vector<TestEvent> received;
  std::thread consumer([&]() {
    size_t count = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (count < NUM_ROUNDS) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 20) {
        std::cout << "Consumer timeout in AlternatingProducerConsumer. Received: " << count << "/" << NUM_ROUNDS << std::endl;
        break;
      }
      
      if (auto result = queue.try_dequeue()) {
        received.push_back(*result);
        count++;
      } else {
        if (producers_done.load() && queue.empty()) {
          break;
        }
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  producer2.join();
  producers_done = true;
  consumer.join();

  ASSERT_EQ(received.size(), NUM_ROUNDS);
  for (size_t i = 0; i < received.size(); ++i) {
    EXPECT_EQ(received[i].seqNum_, i);
  }
}

// Test 3: Rapid Clear and Fill
TEST_F(OrderedMPMCQueueTest, RapidClearAndFill) {
  static constexpr size_t NUM_ROUNDS = 10;
  static constexpr size_t BATCH_SIZE = 100;

  for (size_t round = 0; round < NUM_ROUNDS; ++round) {
    // Fill
    for (size_t i = 0; i < BATCH_SIZE; ++i) {
      ASSERT_TRUE(queue.try_enqueue(TestEvent(round * BATCH_SIZE + i, i)));
    }

    // Clear
    size_t count = 0;
    while (auto result = queue.try_dequeue()) {
      EXPECT_EQ(result->seqNum_, round * BATCH_SIZE + count);
      count++;
    }
    EXPECT_EQ(count, BATCH_SIZE);
  }
}

// Test 4: Late Sequence Numbers
TEST_F(OrderedMPMCQueueTest, LateSequenceNumbers) {
  // Fill with later sequence numbers first
  for (size_t i = 5; i < 10; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  // Shouldn't be able to dequeue yet
  EXPECT_FALSE(queue.try_dequeue().has_value());

  // Fill in the early sequence numbers
  for (size_t i = 0; i < 5; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  // Now should get all numbers in order
  for (size_t i = 0; i < 10; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
}

// Test 5: Producer Contention
TEST_F(OrderedMPMCQueueTest, ProducerContention) {
  static constexpr size_t NUM_PRODUCERS = 8;
  static constexpr size_t EVENTS_PER_PRODUCER = 1000;

  std::vector<std::atomic<size_t>> success_counts(NUM_PRODUCERS);
  std::vector<std::thread> producers;

  for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
    producers.emplace_back([&, p]() {
      for (size_t i = 0; i < EVENTS_PER_PRODUCER; ++i) {
        if (queue.try_enqueue(TestEvent(p * EVENTS_PER_PRODUCER + i, i))) {
          success_counts[p]++;
          std::this_thread::sleep_for(10us);
        }
      }
    });
  }

  for (auto& p : producers)
    p.join();

  // Check fairness between producers
  size_t min_success = EVENTS_PER_PRODUCER;
  size_t max_success = 0;
  for (const auto& count : success_counts) {
    min_success = std::min(min_success, count.load());
    max_success = std::max(max_success, count.load());
  }

  // Allow for some variance but ensure rough fairness
  EXPECT_LT(max_success - min_success, EVENTS_PER_PRODUCER / 4);
}

// Test 6: Consumer Starvation Prevention
TEST_F(OrderedMPMCQueueTest, ConsumerStarvation) {
  static constexpr size_t NUM_CONSUMERS = 4;
  static constexpr size_t NUM_EVENTS = 1000;

  // Pre-fill queue
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  std::vector<std::atomic<size_t>> consumer_counts(NUM_CONSUMERS);
  std::vector<std::thread> consumers;

  for (size_t c = 0; c < NUM_CONSUMERS; ++c) {
    consumers.emplace_back([&, c]() {
      while (true) {
        if (auto result = queue.try_dequeue()) {
          consumer_counts[c]++;
          std::this_thread::sleep_for(100us);
        } else if (queue.empty()) {
          break;
        }
      }
    });
  }

  for (auto& c : consumers)
    c.join();

  // Verify all events were consumed
  size_t total = 0;
  for (const auto& count : consumer_counts) {
    total += count.load();
  }
  EXPECT_EQ(total, NUM_EVENTS);

  // Check consumer fairness
  size_t min_count = NUM_EVENTS;
  size_t max_count = 0;
  for (const auto& count : consumer_counts) {
    min_count = std::min(min_count, count.load());
    max_count = std::max(max_count, count.load());
  }

  // Allow for some variance but ensure rough fairness
  EXPECT_LT(max_count - min_count, NUM_EVENTS / (.5 * NUM_CONSUMERS));
}


// Test 6a: Produce in reverse order, consume in order
TEST_F(OrderedMPMCQueueTest, QueueProduceInReverseOrder) {
  // Fill queue
  for (size_t i=0; i< QUEUE_CAPACITY/2; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY/2-i-1, i)));
  }

  for (size_t i=0; i<QUEUE_CAPACITY/2; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
  }

}

// Test 7: Queue Wraparound
TEST_F(OrderedMPMCQueueTest, QueueWraparound) {
  // Fill queue
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  // Remove half
  for (size_t i = 0; i < QUEUE_CAPACITY / 2; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }

  // Add new elements (should wrap around internal buffer)
  for (size_t i = QUEUE_CAPACITY; i < QUEUE_CAPACITY + QUEUE_CAPACITY / 2; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  // Verify all elements
  for (size_t i = QUEUE_CAPACITY / 2; i < QUEUE_CAPACITY + QUEUE_CAPACITY / 2; ++i) {
    auto result = queue.try_dequeue();
    // sync_cout << "result->seqNum_: " << result->seqNum_ << " i: " << i << std::endl;
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
}

// Test 8: Mixed Event Sizes
TEST_F(OrderedMPMCQueueTest, MixedEventSizes) {
  static const std::string SMALL_STR = "small";
  static const std::string LARGE_STR(1000, 'x');  // 1KB string

  // Alternate between small and large events
  for (size_t i = 0; i < 100; ++i) {
    if (i % 2 == 0) {
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i, SMALL_STR)));
    } else {
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i, LARGE_STR)));
    }
  }

  // Verify all events
  for (size_t i = 0; i < 100; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    if (i % 2 == 0) {
      EXPECT_EQ(result->metadata_, SMALL_STR);
    } else {
      EXPECT_EQ(result->metadata_, LARGE_STR);
    }
  }
}

// Test 10: Bulk Operations
TEST_F(OrderedMPMCQueueTest, BulkOperations) {
  static constexpr size_t BULK_SIZE = 100;

  // Bulk enqueue
  auto start = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < BULK_SIZE; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  auto enqueue_time = std::chrono::high_resolution_clock::now() - start;

  // Bulk dequeue
  start = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < BULK_SIZE; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
  auto dequeue_time = std::chrono::high_resolution_clock::now() - start;

  // Ensure operations completed in reasonable time
  // Note: These are very loose bounds for CI environments
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(enqueue_time).count(), 1000);
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dequeue_time).count(), 1000);
}


// Test 1: Very Large Sequence Number Jumps
TEST_F(OrderedMPMCQueueTest, VeryLargeSequenceJumps) {
  std::vector<unsigned long> seqNumbers = {
      0, 1000000, 2000000,
      500000,  // Out of order but between others
      1500000  // Out of order but between others
  };

  // Enqueue events with very large sequence number gaps
  for (auto seq : seqNumbers) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(seq, seq)));
  }

  // Should only get the first event
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);

  // No more events should be available due to gaps
  result = queue.try_dequeue();
  EXPECT_FALSE(result.has_value());
}

// Test 2: Producer Back-pressure
TEST_F(OrderedMPMCQueueTest, ProducerBackpressure) {
  std::atomic<size_t> fast_producer_count{0};
  std::atomic<size_t> slow_producer_count{0};

  // Fast producer thread
  std::thread fast_producer([&]() {
    for (size_t i = 0; i < QUEUE_CAPACITY * 2; ++i) {
      if (queue.try_enqueue(TestEvent(i * 2, i))) {
        fast_producer_count++;
      }
    }
  });

  // Slow producer thread
  std::thread slow_producer([&]() {
    for (size_t i = 0; i < QUEUE_CAPACITY * 2; ++i) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
      if (queue.try_enqueue(TestEvent(i * 2 + 1, i))) {
        slow_producer_count++;
      }
    }
  });

  fast_producer.join();
  slow_producer.join();

  // Slow producer should still get fair chance
  EXPECT_GT(slow_producer_count.load(), QUEUE_CAPACITY / 4);
}

// Test 3: Consumer Priority
TEST_F(OrderedMPMCQueueTest, ConsumerPriority) {
  static constexpr size_t NUM_EVENTS = 1000;

  // Pre-fill queue
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  std::atomic<size_t> high_priority_count{0};
  std::atomic<size_t> low_priority_count{0};

  // High-priority consumer (continuous attempts)
  std::thread high_priority([&]() {
    while (high_priority_count + low_priority_count < NUM_EVENTS) {
      if (auto result = queue.try_dequeue()) {
        high_priority_count++;
      }
    }
  });

  // Low-priority consumer (with delays)
  std::thread low_priority([&]() {
    while (high_priority_count + low_priority_count < NUM_EVENTS) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      if (auto result = queue.try_dequeue()) {
        low_priority_count++;
      }
    }
  });

  high_priority.join();
  low_priority.join();

  // High priority consumer should get more events
  EXPECT_GT(high_priority_count.load(), low_priority_count.load());
}

// Test 5: Interleaved Small/Large Events with Move Semantics
TEST_F(OrderedMPMCQueueTest, InterleavedEventSizes) {
  static constexpr size_t NUM_EVENTS = 100;
  std::string large_data(10000, 'x');  // 10KB string

  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    if (i % 2 == 0) {
      // Small event
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    } else {
      // Large event with move semantics
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i, std::move(large_data))));
      // Restore large_data for next iteration
      large_data = std::string(10000, 'x');
    }
  }

  // Verify all events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
    if (i % 2 == 1) {
      EXPECT_EQ(result->metadata_.size(), 10000);
    }
  }
}

// Test 6: Random Sequence Number Distribution
TEST_F(OrderedMPMCQueueTest, RandomSequenceDistribution) {
  static constexpr size_t NUM_EVENTS = 1000;
  std::random_device rd;
  std::mt19937_64 gen(rd());

  std::set<unsigned long> used_sequences;
  std::vector<unsigned long> sequences;

  sequences.resize(NUM_EVENTS);
  std::iota(sequences.begin(), sequences.end(), 0);

  std::random_shuffle(sequences.begin(), sequences.end());

  // Enqueue events with random sequence numbers
  for (auto seq : sequences) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(seq, seq)));
  }

  // Should get events in order
  auto expected_sequences = sequences;
  std::sort(expected_sequences.begin(), expected_sequences.end());

  for (auto expected_seq : expected_sequences) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, expected_seq);
  }
}

// Test 7: Producer Cancellation
TEST_F(OrderedMPMCQueueTest, ProducerCancellation) {
  std::atomic<bool> cancel_flag{false};
  std::atomic<size_t> enqueued_count{0};

  auto producer = std::async(std::launch::async, [&]() {
    size_t i = 0;
    while (!cancel_flag.load()) {
      if (queue.try_enqueue(TestEvent(i++, 0))) {
        enqueued_count++;
      }
    }
    return i;
  });

  // Let producer run for a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Signal cancellation
  cancel_flag.store(true);

  size_t attempted_count = producer.get();
  size_t actual_enqueued = enqueued_count.load();

  EXPECT_GT(attempted_count, actual_enqueued);
  EXPECT_GT(actual_enqueued, 0);
}

// Test 8: Queue Recovery After Full
TEST_F(OrderedMPMCQueueTest, QueueRecoveryAfterFull) {
  // Fill queue to capacity
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  EXPECT_TRUE(queue.full());

  // Try to enqueue when full
  EXPECT_FALSE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 0)));

  // Remove one item
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(queue.full());
  EXPECT_EQ(result->seqNum_, 0);

  // Should be able to enqueue again
  EXPECT_TRUE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 0)));
  ASSERT_TRUE(queue.full());


  // Queue should maintain correct size
  EXPECT_EQ(queue.size(), QUEUE_CAPACITY);
}

// Test 9: Multiple Consumer Race for Last Item
TEST_F(OrderedMPMCQueueTest, MultipleConsumerRaceForLast) {
  static constexpr size_t NUM_CONSUMERS = 8;

  // Add just one item
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 42)));

  std::atomic<size_t> successful_dequeues{0};
  std::vector<std::thread> consumers;

  // Create multiple consumers racing for the same item
  for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
    consumers.emplace_back([&]() {
      if (auto result = queue.try_dequeue()) {
        successful_dequeues++;
        EXPECT_EQ(result->seqNum_, 0);
        EXPECT_EQ(result->data_, 42);
      }
    });
  }

  for (auto& consumer : consumers) {
    consumer.join();
  }

  // Only one consumer should have gotten the item
  EXPECT_EQ(successful_dequeues.load(), 1);
}

// Test 10: Sparse Sequence Distribution
TEST_F(OrderedMPMCQueueTest, SparseSequenceDistribution) {
  static constexpr size_t STRIDE = 100;  // Large gaps between sequence numbers
  static constexpr size_t NUM_EVENTS = 10;

  // Enqueue events with large gaps between sequence numbers
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i * STRIDE, i)));
  }

  // Fill in all gaps
  for (size_t i = 0; i < NUM_EVENTS - 1; ++i) {
    for (size_t j = 1; j < STRIDE; j += 1) {
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i * STRIDE + j, i)));
    }
  }

  // Verify events come out in sequence order
  size_t last_seq = 0;
  while (auto result = queue.try_dequeue()) {
    EXPECT_EQ(result->seqNum_, last_seq);
    last_seq++;
  }
}

// Test 11: Verify Dequeue Ordering with Multiple Workers
TEST_F(OrderedMPMCQueueTest, MultiWorkerDequeueOrdering) {
  static constexpr size_t NUM_EVENTS = 1000;
  static constexpr size_t NUM_WORKERS = 4;

  // First enqueue all events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }

  std::atomic<bool> start{false};
  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;

  // Create multiple worker threads
  for (size_t i = 0; i < NUM_WORKERS; ++i) {
    workers.emplace_back([this, &start, &stop]() {
      // Wait for all threads to be ready
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      while (!stop.load(std::memory_order_acquire)) {
        TestEvent event;
        if (this->queue.try_dequeue(event, true)) {
          // Process event
          std::this_thread::sleep_for(std::chrono::microseconds(1));
          this->queue.mark_processed(event.seqNum_);

          // If we processed the last event, signal all threads to stop
          if (event.seqNum_ == NUM_EVENTS - 1) {
            stop.store(true, std::memory_order_release);
            break;
          }
        }
        std::this_thread::yield();  // Add yield to prevent tight spinning
      }
    });
  }

  // Start all workers simultaneously
  start.store(true, std::memory_order_release);

  // Wait for all workers to complete
  for (auto& worker : workers) {
    worker.join();
  }

  // Verify dequeue order
  auto dequeue_order = queue.get_dequeue_order();
  ASSERT_EQ(dequeue_order.size(), NUM_EVENTS);

  // Check that events were dequeued in sequence
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    EXPECT_EQ(dequeue_order[i], i) << "Event out of order at position " << i << ": expected " << i
                                   << " but got " << dequeue_order[i];
  }

}

// Test 13: Empty Queue Operations
TEST_F(OrderedMPMCQueueTest, EmptyQueueOperations) {
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  
  // Multiple empty dequeue attempts
  for (int i = 0; i < 10; ++i) {
    auto result = queue.try_dequeue();
    EXPECT_FALSE(result.has_value());
  }
  
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

// Test 14: Single Element Operations
TEST_F(OrderedMPMCQueueTest, SingleElementOperations) {
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 42)));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1);
  
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);
  EXPECT_EQ(result->data_, 42);
  
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

// Test 15: Maximum Capacity Test
TEST_F(OrderedMPMCQueueTest, MaximumCapacityTest) {
  // Fill queue to maximum capacity
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  EXPECT_EQ(queue.size(), QUEUE_CAPACITY);
  EXPECT_TRUE(queue.full());
  
  // Try to add one more - should fail
  EXPECT_FALSE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 999)));
  
  // Drain the queue
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
  
  EXPECT_TRUE(queue.empty());
}

// Test 16: Large Sequence Number Jumps
TEST_F(OrderedMPMCQueueTest, LargeSequenceJumps) {
  // Test that the queue can handle large sequence numbers correctly
  // but still enforce ordering constraints
  
  // Enqueue sequences 0, 1, 2 first to establish ordering
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 100)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(1, 101)));  
  ASSERT_TRUE(queue.try_enqueue(TestEvent(2, 102)));
  
  // Now try to enqueue a much larger sequence number
  size_t large_seq = 1000000;
  ASSERT_TRUE(queue.try_enqueue(TestEvent(large_seq, 999)));
  
  // Should only be able to consume in order starting from 0
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);
  EXPECT_EQ(result->data_, 100);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 1);
  EXPECT_EQ(result->data_, 101);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 2);
  EXPECT_EQ(result->data_, 102);
  
  // The large sequence number should not be available yet
  // because sequences 3 through 999999 haven't been enqueued
  result = queue.try_dequeue();
  EXPECT_FALSE(result.has_value());
}

// Test 17: Interleaved Enqueue/Dequeue
TEST_F(OrderedMPMCQueueTest, InterleavedEnqueueDequeue) {
  size_t dequeue_count = 0;
  
  for (size_t i = 0; i < 100; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    
    // Every 3rd iteration (after enqueuing 3, 6, 9, etc.), dequeue the next in sequence
    if (i % 3 == 0 && i > 0) {
      auto result = queue.try_dequeue();
      ASSERT_TRUE(result.has_value());
      // Should get the next sequence number in order, not the one we just enqueued
      EXPECT_EQ(result->seqNum_, dequeue_count);
      dequeue_count++;
    }
  }
  
  // Drain remaining events in sequence order
  while (true) {
    auto result = queue.try_dequeue();
    if (!result.has_value()) break;
    EXPECT_EQ(result->seqNum_, dequeue_count);
    dequeue_count++;
  }
  
  // Should have consumed all 100 events
  EXPECT_EQ(dequeue_count, 100);
}

// Test 18: Thread Safety with Rapid Operations
TEST_F(OrderedMPMCQueueTest, ThreadSafetyRapidOps) {
  static constexpr size_t NUM_THREADS = 8;
  static constexpr size_t OPS_PER_THREAD = 50;
  
  std::vector<std::thread> threads;
  std::atomic<size_t> seq_counter{0};
  std::atomic<size_t> consumed_count{0};
  
  // Producer threads
  for (size_t t = 0; t < NUM_THREADS / 2; ++t) {
    threads.emplace_back([&]() {
      for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
        size_t seq = seq_counter.fetch_add(1);
        while (!queue.try_enqueue(TestEvent(seq, seq))) {
          std::this_thread::yield();
        }
      }
    });
  }
  
  // Consumer threads
  for (size_t t = 0; t < NUM_THREADS / 2; ++t) {
    threads.emplace_back([&]() {
      while (consumed_count.load() < NUM_THREADS / 2 * OPS_PER_THREAD) {
        auto result = queue.try_dequeue();
        if (result.has_value()) {
          consumed_count.fetch_add(1);
        } else {
          std::this_thread::yield();
        }
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  EXPECT_EQ(consumed_count.load(), NUM_THREADS / 2 * OPS_PER_THREAD);
}


// Test 19: External Ack Stress Test
TEST_F(OrderedMPMCQueueTest, ExternalAckStressTest) {
  static constexpr size_t NUM_EVENTS = 200;
  
  // Enqueue events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  std::vector<TestEvent> dequeued_events;
  
  // Dequeue with external ack
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    TestEvent event;
    ASSERT_TRUE(queue.try_dequeue(event));
    EXPECT_EQ(event.seqNum_, i);
    dequeued_events.push_back(event);
  }
  
  // Process in random order
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(dequeued_events.begin(), dequeued_events.end(), g);
  
  for (const auto& event : dequeued_events) {
    queue.mark_processed(event.seqNum_);
  }
  
  EXPECT_TRUE(queue.empty());
}

// Test 20: Wraparound Stress Test
TEST_F(OrderedMPMCQueueTest, WraparoundStressTest) {
  static constexpr size_t WRAPAROUND_CYCLES = 3;
  static constexpr size_t EVENTS_PER_CYCLE = QUEUE_CAPACITY / 2;
  
  size_t total_produced = 0;
  size_t total_consumed = 0;
  
  for (size_t cycle = 0; cycle < WRAPAROUND_CYCLES; ++cycle) {
    // Fill partially
    for (size_t i = 0; i < EVENTS_PER_CYCLE; ++i) {
      size_t seq = total_produced++;
      ASSERT_TRUE(queue.try_enqueue(TestEvent(seq, seq)));
    }
    
    // Consume partially
    for (size_t i = 0; i < EVENTS_PER_CYCLE; ++i) {
      auto result = queue.try_dequeue();
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->seqNum_, total_consumed++);
    }
  }
  
  EXPECT_TRUE(queue.empty());
}

// Test 21: Metadata Preservation
TEST_F(OrderedMPMCQueueTest, MetadataPreservation) {
  std::string metadata1 = "important_data_1";
  std::string metadata2 = "critical_info_2";
  std::string metadata3 = "user_context_3";
  
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 100, metadata1)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(1, 200, metadata2)));
  ASSERT_TRUE(queue.try_enqueue(TestEvent(2, 300, metadata3)));
  
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);
  EXPECT_EQ(result->data_, 100);
  EXPECT_EQ(result->metadata_, metadata1);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 1);
  EXPECT_EQ(result->data_, 200);
  EXPECT_EQ(result->metadata_, metadata2);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 2);
  EXPECT_EQ(result->data_, 300);
  EXPECT_EQ(result->metadata_, metadata3);
}

// Test 22: Producer Backpressure Handling
TEST_F(OrderedMPMCQueueTest, ProducerBackpressureHandling) {
  // Fill queue completely
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  // Multiple producers should fail to enqueue
  std::atomic<int> failed_enqueues{0};
  std::vector<std::thread> producers;
  
  for (int p = 0; p < 4; ++p) {
    producers.emplace_back([&]() {
      for (int i = 0; i < 10; ++i) {
        if (!queue.try_enqueue(TestEvent(QUEUE_CAPACITY + i, i))) {
          failed_enqueues.fetch_add(1);
        }
      }
    });
  }
  
  for (auto& p : producers) {
    p.join();
  }
  
  EXPECT_GT(failed_enqueues.load(), 0);
  EXPECT_TRUE(queue.full());
}

// Test 23: Consumer Starvation Recovery  
TEST_F(OrderedMPMCQueueTest, ConsumerStarvationRecovery) {
  static constexpr size_t NUM_EVENTS = 50;
  
  // First produce some events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  // Then consume them all
  size_t consumed = 0;
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    auto result = queue.try_dequeue();
    if (result.has_value()) {
      consumed++;
    }
  }
  
  EXPECT_EQ(consumed, NUM_EVENTS);
}

// Test 24: Mixed Event Types
TEST_F(OrderedMPMCQueueTest, MixedEventTypes) {
  // Enqueue events with different data patterns
  ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 0)));          // Zero
  ASSERT_TRUE(queue.try_enqueue(TestEvent(1, -1)));         // Negative
  ASSERT_TRUE(queue.try_enqueue(TestEvent(2, INT_MAX)));    // Max int
  ASSERT_TRUE(queue.try_enqueue(TestEvent(3, INT_MIN)));    // Min int
  ASSERT_TRUE(queue.try_enqueue(TestEvent(4, 42)));         // Regular
  
  // Verify order and data integrity
  auto result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 0);
  EXPECT_EQ(result->data_, 0);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 1);
  EXPECT_EQ(result->data_, -1);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 2);
  EXPECT_EQ(result->data_, INT_MAX);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 3);
  EXPECT_EQ(result->data_, INT_MIN);
  
  result = queue.try_dequeue();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->seqNum_, 4);
  EXPECT_EQ(result->data_, 42);
}

// Test 25: High Frequency Operations
TEST_F(OrderedMPMCQueueTest, HighFrequencyOperations) {
  static constexpr size_t NUM_ITERATIONS = 1000;
  std::atomic<size_t> operations_completed{0};
  
  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
      while (!queue.try_enqueue(TestEvent(i, i))) {
        // Busy wait
      }
      operations_completed.fetch_add(1);
    }
  });
  
  std::thread consumer([&]() {
    size_t consumed = 0;
    while (consumed < NUM_ITERATIONS) {
      auto result = queue.try_dequeue();
      if (result.has_value()) {
        EXPECT_EQ(result->seqNum_, consumed);
        consumed++;
        operations_completed.fetch_add(1);
      }
    }
  });
  
  producer.join();
  consumer.join();
  
  EXPECT_EQ(operations_completed.load(), NUM_ITERATIONS * 2);
}

// Test 26: Queue State Consistency
TEST_F(OrderedMPMCQueueTest, QueueStateConsistency) {
  static constexpr size_t NUM_EVENTS = 50;
  
  // Add events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    EXPECT_EQ(queue.size(), i + 1);
    EXPECT_FALSE(queue.empty());
  }
  
  // Remove events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    EXPECT_EQ(queue.size(), NUM_EVENTS - i);
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
  
  EXPECT_EQ(queue.size(), 0);
  EXPECT_TRUE(queue.empty());
}

// Test 28: Multiple Queue Reset
TEST_F(OrderedMPMCQueueTest, MultipleQueueReset) {
  for (int cycle = 0; cycle < 5; ++cycle) {
    // Fill queue
    for (size_t i = 0; i < 20; ++i) {
      ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    }
    
    // Consume some
    for (size_t i = 0; i < 10; ++i) {
      auto result = queue.try_dequeue();
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->seqNum_, i);
    }
    
    // Reset should clear everything
    queue.reset();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
  }
}

// Test 29: Concurrent External Ack
TEST_F(OrderedMPMCQueueTest, ConcurrentExternalAck) {
  static constexpr size_t NUM_EVENTS = 100;
  
  // Enqueue events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  std::vector<std::thread> workers;
  std::atomic<size_t> processed_count{0};
  
  // Multiple workers dequeue and process
  for (int w = 0; w < 4; ++w) {
    workers.emplace_back([&]() {
      while (processed_count.load() < NUM_EVENTS) {
        TestEvent event;
        if (queue.try_dequeue(event, true)) {
          // Simulate processing time
          std::this_thread::sleep_for(std::chrono::microseconds(10));
          queue.mark_processed(event.seqNum_);
          processed_count.fetch_add(1);
        } else {
          std::this_thread::yield();
        }
      }
    });
  }
  
  for (auto& w : workers) {
    w.join();
  }
  
  EXPECT_EQ(processed_count.load(), NUM_EVENTS);
  EXPECT_TRUE(queue.empty());
}

// Test 30: Performance Stress Test
TEST_F(OrderedMPMCQueueTest, PerformanceStressTest) {
  static constexpr size_t STRESS_EVENTS = 1000;
  
  auto start_time = std::chrono::high_resolution_clock::now();
  
  std::thread producer([&]() {
    for (size_t i = 0; i < STRESS_EVENTS; ++i) {
      while (!queue.try_enqueue(TestEvent(i, i))) {
        std::this_thread::yield();
      }
    }
  });
  
  std::thread consumer([&]() {
    size_t consumed = 0;
    while (consumed < STRESS_EVENTS) {
      auto result = queue.try_dequeue();
      if (result.has_value()) {
        EXPECT_EQ(result->seqNum_, consumed);
        consumed++;
      } else {
        std::this_thread::yield();
      }
    }
  });
  
  producer.join();
  consumer.join();
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  // Should complete within reasonable time (less than 5 seconds)
  EXPECT_LT(duration.count(), 5000);
  EXPECT_TRUE(queue.empty());
}

// Test 31: Queue Recovery After Errors
TEST_F(OrderedMPMCQueueTest, QueueRecoveryAfterErrors) {
  // Fill queue and try to overfill
  for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
  }
  
  // These should fail
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY + i, i)));
  }
  
  // Queue should still be functional
  EXPECT_TRUE(queue.full());
  EXPECT_EQ(queue.size(), QUEUE_CAPACITY);
  
  // Should be able to consume normally
  for (size_t i = 0; i < 10; ++i) {
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, i);
  }
  
  // Should be able to add new items
  for (size_t i = 0; i < 10; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY + i, i)));
  }
}

// Test 12: Multiple Publishers and Consumers with Random Delays
TEST_F(OrderedMPMCQueueTest, MultiPublisherConsumerWithDelays) {
  static constexpr size_t NUM_PUBLISHERS = 4;
  static constexpr size_t NUM_CONSUMERS = 4;
  static constexpr size_t EVENTS_PER_PUBLISHER = 100;
  static constexpr size_t TOTAL_EVENTS = NUM_PUBLISHERS * EVENTS_PER_PUBLISHER;

  std::atomic<bool> start{false};
  std::atomic<bool> stop{false};
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> delay_dist(0, 1);  // 0-1ms delay

  // Start publishers
  std::vector<std::thread> publishers;
  std::atomic<bool> publishers_done{false};
  
  for (size_t p = 0; p < NUM_PUBLISHERS; ++p) {
    publishers.emplace_back([&, p]() {
      // Wait for start signal
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      for (size_t i = 0; i < EVENTS_PER_PUBLISHER; ++i) {
        size_t seq = p * EVENTS_PER_PUBLISHER + i;
        
        // Add timeout protection for enqueue attempts
        auto enqueue_start = std::chrono::steady_clock::now();
        while (!queue.try_enqueue(TestEvent(seq, seq))) {
          auto now = std::chrono::steady_clock::now();
          if (std::chrono::duration_cast<std::chrono::seconds>(now - enqueue_start).count() > 10) {
            std::cout << "Publisher " << p << " timeout trying to enqueue seq " << seq << std::endl;
            stop.store(true, std::memory_order_release);
            return;
          }
          std::this_thread::yield();
        }
        // Very small delay between enqueues
        if (delay_dist(gen) == 1) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Start consumers
  std::vector<std::thread> consumers;
  std::atomic<size_t> total_processed{0};
  
  for (size_t c = 0; c < NUM_CONSUMERS; ++c) {
    consumers.emplace_back([&]() {
      // Wait for start signal
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      auto start_time = std::chrono::steady_clock::now();
      while (!stop.load(std::memory_order_acquire)) {
        // Add timeout protection  
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 60) {
          std::cout << "Consumer timeout in MultiPublisherConsumerWithDelays. Processed: " 
                   << total_processed.load() << "/" << TOTAL_EVENTS << std::endl;
          stop.store(true, std::memory_order_release);
          break;
        }
        
        TestEvent event;
        // sync_cout << "Attempting to dequeue" << std::endl;
        if (queue.try_dequeue(event, true)) {
          // sync_cout << "Dequeued event " << event.seqNum_ << std::endl;
          // Very small delay before processing
          if (delay_dist(gen) == 1) {
            std::this_thread::yield();
          }
          queue.mark_processed(event.seqNum_);
          
          size_t processed_count = total_processed.fetch_add(1) + 1;

          // If we processed all events, signal all threads to stop
          if (processed_count >= TOTAL_EVENTS) {
            stop.store(true, std::memory_order_release);
          }
        }
        std::this_thread::yield();
      }
    });
  }

  // Start all threads simultaneously
  start.store(true, std::memory_order_release);

  // Wait for completion
  for (auto& pub : publishers) {
    pub.join();
  }
  
  publishers_done.store(true, std::memory_order_release);
  
  // Give consumers some time to finish processing after publishers are done
  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop.store(true, std::memory_order_release);
  
  for (auto& con : consumers) {
    con.join();
  }

  // Verify dequeue order
  auto dequeue_order = queue.get_dequeue_order();
  ASSERT_EQ(dequeue_order.size(), TOTAL_EVENTS);

  // Check that events were dequeued in sequence
  for (size_t i = 0; i < TOTAL_EVENTS; ++i) {
    EXPECT_EQ(dequeue_order[i], i) << "Event out of order at position " << i << ": expected " << i
                                   << " but got " << dequeue_order[i];
  }

}

// Test 50: External Acknowledgment Memory Safety
TEST_F(OrderedMPMCQueueTest, ExternalAckMemorySafety) {
  // Test that dequeue and mark_processed work together correctly
  // and handle memory safely when events are processed out of dequeue order
  
  static constexpr size_t NUM_EVENTS = 10;
  std::vector<TestEvent> dequeued_events;
  
  // Enqueue events
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i * 10)));
  }
  
  // Dequeue events normally
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    TestEvent event;
    bool success = queue.try_dequeue(event);
    ASSERT_TRUE(success);
    EXPECT_EQ(event.seqNum_, i);
    EXPECT_EQ(event.data_, i * 10);
    dequeued_events.push_back(event);
  }
  
  // Manually mark some events as processed to test external ack behavior
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    queue.mark_processed(i);
  }
  
  // Queue should be empty after processing
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  
  // Verify we can still use the dequeued events (no use-after-free)
  for (size_t i = 0; i < NUM_EVENTS; ++i) {
    EXPECT_EQ(dequeued_events[i].seqNum_, i);
    EXPECT_EQ(dequeued_events[i].data_, i * 10);
  }
  
  // Test that we can add new events after the processing cycle
  ASSERT_TRUE(queue.try_enqueue(TestEvent(NUM_EVENTS, 999)));
  TestEvent final_event;
  ASSERT_TRUE(queue.try_dequeue(final_event));
  EXPECT_EQ(final_event.seqNum_, NUM_EVENTS);
  EXPECT_EQ(final_event.data_, 999);
}


/*

class OrderBookTest : public ::testing::Test {
 protected:
  static constexpr int N = 5;
  static unsigned long seqNum;
  using BidContainer = ForwardListAdaptor<std::greater<Message::order>, N>;
  using AskContainer = ForwardListAdaptor<std::less<Message::order>, N>;
  using InnerType = OrderBook<BidContainer, AskContainer>;
  using OrderBookType = TradeJournalerOrderBook<BidContainer, AskContainer>;

  void SetUp() override {
    // orderbook = std::make_unique<OrderBook<BidContainer, AskContainer>>();
    orderbook = std::make_unique<OrderBookType>(InnerType{});
  }

  std::unique_ptr<TradeJournalerOrderBook<BidContainer, AskContainer>> orderbook;
  // std::unique_ptr<OrderBook<BidContainer, AskContainer>> orderbook;

  Message::eventLOBSTER createEvent(double time, short type, unsigned long orderId, unsigned size,
                                    long price, char direction) {
    return Message::eventLOBSTER{seqNum++, time, type, orderId, size, price, direction};
  }
};

unsigned long OrderBookTest::seqNum = 0;

TEST_F(OrderBookTest, EmptyBookHasNoLevels) {
  auto book = orderbook->getBook();
  EXPECT_TRUE(book.bids.empty());
  EXPECT_TRUE(book.asks.empty());
}

TEST_F(OrderBookTest, SingleBidOrder) {
  auto event = createEvent(1.0, 1, 1001, 100, 10000, 'B');
  orderbook->processEvent(event);

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 1);
  EXPECT_EQ(book.bids[0].price_, 10000);
  EXPECT_EQ(book.bids[0].size_, 100);
}

TEST_F(OrderBookTest, PriceTimePriorityForBids) {
  // Add orders at same price level
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 50, 10000, 'B'));
  orderbook->processEvent(createEvent(1.2, 1, 1003, 23, 10011, 'B'));
  orderbook->processEvent(createEvent(1.3, 1, 1004, 1181, 10010, 'B'));
  orderbook->processEvent(createEvent(1.4, 1, 1005, 4411, 11012, 'S'));
  orderbook->processEvent(createEvent(1.5, 1, 1006, 12, 9999, 'B'));

  // Execute partial
  orderbook->processEvent(createEvent(1.6, 4, 1007, 23, 10011, 'B'));
  orderbook->processEvent(createEvent(1.7, 4, 1008, 1181, 10010, 'B'));
  orderbook->processEvent(createEvent(1.8, 4, 1009, 75, 10000, 'B'));

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 2);
  ASSERT_EQ(book.asks.size(), 1);
  EXPECT_EQ(book.bids[0].price_, 10000);
  EXPECT_EQ(book.bids[0].size_, 75);  // First order partially executed (25 left)
  EXPECT_EQ(book.bids[0].orderCount_, 2);

  // Execute partial again consuming 2 orders
  orderbook->processEvent(createEvent(1.8, 4, 1009, 50, 10000, 'B'));

  book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 2);
  ASSERT_EQ(book.bids[0].price_, 10000);
  ASSERT_EQ(book.bids[0].size_, 25);
  ASSERT_EQ(book.bids[0].orderCount_, 1);
}

TEST_F(OrderBookTest, OrderCancellation) {
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 2, 1001, 0, 0, 'B'));  // Cancel

  auto book = orderbook->getBook();
  EXPECT_TRUE(book.bids.empty());
}

TEST_F(OrderBookTest, MultipleBookLevels) {
  // Add bids at different prices
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 50, 9900, 'B'));
  orderbook->processEvent(createEvent(1.2, 1, 1003, 75, 10100, 'B'));

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 3);
  EXPECT_EQ(book.bids[0].price_, 10100);  // Best bid first
  EXPECT_EQ(book.bids[1].price_, 10000);
  EXPECT_EQ(book.bids[2].price_, 9900);
}

TEST_F(OrderBookTest, OrderUpdate) {
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 5, 1001, 150, 10000, 'B'));  // Update size

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 1);
  EXPECT_EQ(book.bids[0].size_, 150);
}

TEST_F(OrderBookTest, CrossedBook) {
  // Add bid and ask that could match
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 2001, 100, 9900, 'S'));

  // Book is crossed, then empty

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 0);
  ASSERT_EQ(book.asks.size(), 0);
  auto journal = orderbook->get_journal().get_store();
  for (auto it = journal.begin(); it != journal.end(); ++it) {
    auto seqNum = it->first;
    auto trade = it->second;

    // Price improved!!
    EXPECT_EQ(trade.price_, 10000);
    EXPECT_EQ(trade.size_, 100);
  }
}

TEST_F(OrderBookTest, LargeBookSnapshot) {
  // Create baseline book with multiple levels
  for (int i = 0; i < 10; ++i) {
    orderbook->processEvent(createEvent(1.0 + i, 1, 1001 + i, 100, 10000 - i * 100, 'B'));
    orderbook->processEvent(createEvent(1.0 + i, 1, 2001 + i, 100, 10100 + i * 100, 'S'));
  }

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 5);  // Should only show N levels
  ASSERT_EQ(book.asks.size(), 5);

  // Verify price ordering
  for (size_t i = 1; i < book.bids.size(); ++i) {
    EXPECT_GT(book.bids[i - 1].price_, book.bids[i].price_);
    EXPECT_LT(book.asks[i - 1].price_, book.asks[i].price_);
  }
}

TEST_F(OrderBookTest, FullOrderExecution) {
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 50, 10000, 'B'));
  orderbook->processEvent(createEvent(1.2, 4, 0, 100, 10000, 'B'));  // Execute first order fully

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 1);
  EXPECT_EQ(book.bids[0].price_, 10000);
  EXPECT_EQ(book.bids[0].size_, 50);  // Only second order remains
}

TEST_F(OrderBookTest, MultipleExecutionsAtSamePrice) {
  // Add three orders at same price
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 50, 10000, 'B'));
  orderbook->processEvent(createEvent(1.2, 1, 1003, 75, 10000, 'B'));

  // Execute more than first order
  orderbook->processEvent(createEvent(1.3, 4, 0, 120, 10000, 'B'));

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 1);
  EXPECT_EQ(book.bids[0].size_, 105);  // Only part of second order and full third order remain
}

TEST_F(OrderBookTest, BaseLine1_SimpleBidExecution) {
  // Submit orders
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));  // Bid 100@100.00
  orderbook->processEvent(createEvent(1.1, 1, 1002, 150, 10000, 'B'));  // Bid 150@100.00
  orderbook->processEvent(createEvent(1.2, 1, 1003, 200, 10001, 'B'));  // Bid 200@100.01

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 2);
  EXPECT_EQ(book.bids[0].price_, 10001);
  EXPECT_EQ(book.bids[0].size_, 200);
  EXPECT_EQ(book.bids[1].price_, 10000);
  EXPECT_EQ(book.bids[1].size_, 250);  // Aggregated size at 100.00

  // Execute against best bid
  orderbook->processEvent(createEvent(1.3, 4, 0, 150, 10001, 'B'));

  book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 2);
  EXPECT_EQ(book.bids[0].price_, 10001);
  EXPECT_EQ(book.bids[0].size_, 50);  // 200 - 150 = 50
  EXPECT_EQ(book.bids[1].price_, 10000);
  EXPECT_EQ(book.bids[1].size_, 250);  // Untouched
}

TEST_F(OrderBookTest, BaseLine2_CrossedBook) {
  // Build bid side
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 150, 10001, 'B'));
  orderbook->processEvent(createEvent(1.2, 1, 1003, 200, 10002, 'B'));

  // Build ask side
  orderbook->processEvent(createEvent(1.3, 1, 2001, 120, 10003, 'S'));
  orderbook->processEvent(createEvent(1.4, 1, 2002, 180, 10004, 'S'));
  orderbook->processEvent(createEvent(1.5, 1, 2003, 140, 10005, 'S'));

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 3);
  ASSERT_EQ(book.asks.size(), 3);

  // Execute multiple orders
  orderbook->processEvent(
      createEvent(1.6, 4, 0, 175, 10002, 'B'));  // Partial execution at best bid
  orderbook->processEvent(
      createEvent(1.7, 4, 0, 100, 10004, 'S'));  // Partial execution at ask level

  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10002);
  EXPECT_EQ(book.bids[0].size_, 25);
  EXPECT_EQ(book.asks[0].price_, 10003);
  EXPECT_EQ(book.asks[0].size_, 20);
}

TEST_F(OrderBookTest, BaseLine3_MultiLevelExecution) {
  // Build deep book on bid side
  for (int i = 0; i < 5; i++) {
    orderbook->processEvent(createEvent(1.0 + i, 1, 1001 + i, 100, 10000 + i, 'B'));
    orderbook->processEvent(
        createEvent(1.5 + i, 1, 1501 + i, 100, 10000 + i, 'B'));  // Same price levels
  }

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 5);
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(book.bids[i].price_, 10004 - i);
    EXPECT_EQ(book.bids[i].size_, 200);  // Both orders at each level
  }

  book = orderbook->getBook();

  // Execute at multiple levels
  orderbook->processEvent(createEvent(2.0, 4, 0, 150, 10004, 'B'));  // Best bid
  orderbook->processEvent(createEvent(2.1, 4, 0, 250, 10003, 'B'));  // Second level
  orderbook->processEvent(createEvent(2.2, 4, 0, 100, 10002, 'B'));  // Third level

  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10002);
  EXPECT_EQ(book.bids[0].size_, 100);
  EXPECT_EQ(book.bids[1].price_, 10001);
  EXPECT_EQ(book.bids[1].size_, 200);
  EXPECT_EQ(book.bids[2].price_, 10000);
  EXPECT_EQ(book.bids[2].size_, 200);
}

TEST_F(OrderBookTest, BaseLine4_InterleavedOrdersAndExecutions) {
  // Initial orders
  orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
  orderbook->processEvent(createEvent(1.1, 1, 1002, 150, 10001, 'B'));

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 2);

  // Execute first order
  orderbook->processEvent(createEvent(1.2, 4, 0, 50, 10001, 'B'));

  // Add more orders
  orderbook->processEvent(createEvent(1.3, 1, 1003, 200, 10002, 'B'));
  orderbook->processEvent(createEvent(1.4, 1, 1004, 175, 10001, 'B'));

  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10002);
  EXPECT_EQ(book.bids[0].size_, 200);
  EXPECT_EQ(book.bids[1].price_, 10001);
  EXPECT_EQ(book.bids[1].size_, 275);

  // More executions
  orderbook->processEvent(createEvent(1.5, 4, 0, 150, 10002, 'B'));
  orderbook->processEvent(createEvent(1.6, 4, 0, 100, 10001, 'B'));

  book = orderbook->getBook();
  EXPECT_EQ(book.bids.size(), 2);
  EXPECT_EQ(book.bids[0].price_, 10001);
  EXPECT_EQ(book.bids[0].size_, 225);
  EXPECT_EQ(book.bids[1].price_, 10000);
  EXPECT_EQ(book.bids[1].size_, 100);
}

TEST_F(OrderBookTest, BaseLine5_FullBookOperations) {
  // Build full book both sides
  for (int i = 0; i < 5; i++) {
    // Bids
    orderbook->processEvent(createEvent(1.0 + i, 1, 1001 + i, 100, 10000 + i, 'B'));
    orderbook->processEvent(createEvent(1.5 + i, 1, 1501 + i, 100, 10000 + i, 'B'));
    // Asks
    orderbook->processEvent(createEvent(2.0 + i, 1, 2001 + i, 100, 10010 + i, 'S'));
    orderbook->processEvent(createEvent(2.5 + i, 1, 2501 + i, 100, 10010 + i, 'S'));
  }

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 5);
  ASSERT_EQ(book.asks.size(), 5);

  // Interleaved executions on both sides
  orderbook->processEvent(createEvent(3.0, 4, 0, 100, 10004, 'B'));  // Best bid
  orderbook->processEvent(createEvent(3.1, 4, 0, 144, 10010, 'S'));  // Best ask
  book = orderbook->getBook();
  orderbook->processEvent(createEvent(3.2, 4, 0, 100, 10003, 'B'));  // Second best bid
  orderbook->processEvent(createEvent(3.3, 4, 0, 186, 10011, 'S'));  // Second best ask

  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10003);
  EXPECT_EQ(book.bids[0].size_, 200);
  EXPECT_EQ(book.asks[0].price_, 10011);
  EXPECT_EQ(book.asks[0].size_, 70);
  EXPECT_EQ(book.asks[1].price_, 10012);
  EXPECT_EQ(book.asks[1].size_, 200);
}

TEST_F(OrderBookTest, InterleavedOrdersMany1) {
  // Initial batch of 10 orders at different price levels
  for (int i = 0; i < 10; i++) {
    orderbook->processEvent(
        createEvent(1.0 + i * 0.1, 1, 1001 + i, 100UL, static_cast<long>(10000 + i * 2), 'B'));
  }
  // After initial 10 orders, book looks like (showing all orders):
  // 100@100.18 (id: 1010)
  // 100@100.16 (id: 1009)
  // 100@100.14 (id: 1008)
  // 100@100.12 (id: 1007)
  // 100@100.10 (id: 1006)
  // 100@100.08 (id: 1005)
  // 100@100.06 (id: 1004)
  // 100@100.04 (id: 1003)
  // 100@100.02 (id: 1002)
  // 100@100.00 (id: 1001)

  // Execute 3 trades
  orderbook->processEvent(createEvent(2.0, 4, 0, 50UL, static_cast<long>(10018), 'B'));
  // After first execution:
  // 50@100.18  (id: 1010) <- partially executed
  // 100@100.16 (id: 1009)
  // 100@100.14 (id: 1008)
  // ...

  orderbook->processEvent(createEvent(2.1, 4, 0, 100UL, static_cast<long>(10016), 'B'));
  // After second execution:
  // 50@100.16   (id: 1009) <- partially executed
  // 100@100.14 (id: 1008)
  // ...

  orderbook->processEvent(createEvent(2.2, 4, 0, 75UL, static_cast<long>(10014), 'B'));
  // After third execution:
  // 100@100.12 (id: 1007)
  // 100@100.10 (id: 1006)
  // ...

  // Add 5 more orders at higher prices
  for (int i = 0; i < 5; i++) {
    orderbook->processEvent(
        createEvent(2.5 + i * 0.1, 1, 1101 + i, 150UL, static_cast<long>(10019 + i * 2), 'B'));
  }
  // After adding 5 more orders:
  // 150@100.27 (id: 1105)
  // 150@100.25 (id: 1104)
  // 150@100.23 (id: 1103)
  // 150@100.21 (id: 1102)
  // 150@100.19 (id: 1101)
  // 100@100.12  (id: 1007)
  // 100@100.10  (id: 1006)
  // ...

  // Execute 4 more trades
  orderbook->processEvent(createEvent(3.0, 4, 0, 150UL, static_cast<long>(10027), 'B'));
  orderbook->processEvent(createEvent(3.1, 4, 0, 100UL, static_cast<long>(10025), 'B'));
  orderbook->processEvent(createEvent(3.2, 4, 0, 75UL, static_cast<long>(10023), 'B'));
  orderbook->processEvent(createEvent(3.3, 4, 0, 50UL, static_cast<long>(10021), 'B'));
  // After these 4 executions:
  // 75@100.21 (id: 1102) <- partially executed
  // 150@100.19 (id: 1101)
  // 100@100.12  (id: 1007)
  // 100@100.10  (id: 1006)
  // ...

  // Add 5 more orders at even higher prices
  for (int i = 0; i < 5; i++) {
    orderbook->processEvent(
        createEvent(3.5 + i * 0.1, 1, 1201 + i, 200UL, static_cast<long>(10028 + i * 2), 'B'));
  }
  // After adding these orders:
  // 200@100.36 (id: 1205)
  // 200@100.34 (id: 1204)
  // 200@100.32 (id: 1203)
  // 200@100.30 (id: 1202)
  // 200@100.28 (id: 1201)
  // 50@100.25  (id: 1104)
  // 75@100.21 (id: 1102)
  // 150@100.19 (id: 1101)
  // 100@100.12  (id: 1007)
  // 100@100.10  (id: 1006)
  // ...

  // Final batch of orders
  for (int i = 0; i < 5; i++) {
    orderbook->processEvent(
        createEvent(4.0 + i * 0.1, 1, 1301 + i, 125UL, static_cast<long>(10037 + i * 2), 'B'));
  }
  // After final orders:
  // 125@100.45 (id: 1305)
  // 125@100.43 (id: 1304)
  // 125@100.41 (id: 1303)
  // 125@100.39 (id: 1302)
  // 125@100.37 (id: 1301)
  // 200@100.36 (id: 1205)
  // 200@100.34 (id: 1204)
  // 200@100.32 (id: 1203)
  // 200@100.30 (id: 1202)
  // 200@100.28 (id: 1201)
  // 50@100.25  (id: 1104)
  // 75@100.21 (id: 1102)
  // 150@100.19 (id: 1101)
  // 100@100.12  (id: 1007)
  // 100@100.10  (id: 1006)
  // ...

  // Final executions
  orderbook->processEvent(createEvent(4.5, 4, 0, 100UL, static_cast<long>(10036), 'B'));
  orderbook->processEvent(createEvent(4.6, 4, 0, 200UL, static_cast<long>(10034), 'B'));
  orderbook->processEvent(createEvent(4.7, 4, 0, 125UL, static_cast<long>(10032), 'B'));

  // Final state:
  // 75@100.39 (id: 1302)
  // 125@100.37 (id: 1301)
  // 200@100.36 (id: 1205)
  // 200@100.34 (id: 1204)
  // 200@100.32 (id: 1203)
  // 200@100.30 (id: 1202)
  // 200@100.28 (id: 1201)
  // 50@100.25  (id: 1104)
  // 75@100.21 (id: 1102)
  // 150@100.19 (id: 1101)
  // 100@100.12  (id: 1007)
  // 100@100.10  (id: 1006)
  // ...

  auto book = orderbook->getBook();
  ASSERT_EQ(book.bids.size(), 5);
  EXPECT_EQ(book.bids[0].price_, 10039);
  EXPECT_EQ(book.bids[0].size_, 75);
  EXPECT_EQ(book.bids[4].price_, 10032);
  EXPECT_EQ(book.bids[4].size_, 200);
  EXPECT_EQ(orderbook->getBestBidPrice(), 10039);
}

TEST_F(OrderBookTest, InterleavedOrdersManyCrossedBook1) {
  // Initial batch of 10 orders at different price levels
  const int num_bids = 10;
  const int num_asks = 10;
  for (int i = 0; i < num_bids; i++) {
    orderbook->processEvent(createEvent(1.0 + i * 0.1, 1, 1001 + num_bids + i, 100UL,
                                        static_cast<long>(10000 + i * 2), 'B'));
  }
  // After initial 10 orders, book looks like (showing all orders):
  // BidSide
  // =======
  // 100@100.18 (id: 1010)
  // 100@100.16 (id: 1009)
  // 100@100.14 (id: 1008)
  // 100@100.12 (id: 1007)
  // 100@100.10 (id: 1006)
  // 100@100.08 (id: 1005)
  // 100@100.06 (id: 1004)
  // 100@100.04 (id: 1003)
  // 100@100.02 (id: 1002)
  // 100@100.00 (id: 1001)
  // AskSide
  // =======
  // 100@100.20 (id: 1020)
  // 100@100.22 (id: 1019)
  // 100@100.24 (id: 1018)
  // 100@100.26 (id: 1017)
  // 100@100.28 (id: 1016)
  // 100@100.30 (id: 1015)
  // 100@100.32 (id: 1014)
  // 100@100.34 (id: 1013)
  // 100@100.36 (id: 1012)
  // 100@100.38 (id: 1011)

  for (int i = 0; i < 10; ++i) {
    orderbook->processEvent(createEvent(num_bids + 1.0 + i * .01, 1, 1001 + num_bids + i, 100UL,
                                        static_cast<long>(10038 - i * 2), 'S'));
  }

  EXPECT_EQ(orderbook->sizeAtPrice(10018, true), 100);
  EXPECT_EQ(orderbook->sizeAtPrice(10016, true), 200);
  EXPECT_EQ(orderbook->sizeAtPrice(10020, false), 100);
  EXPECT_EQ(orderbook->sizeAtPrice(10022, false), 200);

  EXPECT_EQ(orderbook->sizeAtPrice(10002, true), 900);
  EXPECT_EQ(orderbook->sizeAtPrice(10032, false), 700);

  EXPECT_EQ(orderbook->sizeAtPrice(10020, true), 0);
  EXPECT_EQ(orderbook->sizeAtPrice(10018, false), 0);

  // Now enter some locking orders, test book status after each one

  // Only touches inside level
  orderbook->processEvent(createEvent(4.5, 1, 0, 40UL, static_cast<long>(10020), 'B'));
  auto book = orderbook->getBook();
  EXPECT_EQ(book.asks[0].price_, 10020);
  EXPECT_EQ(book.asks[0].size_, 60);

  // Only touches inside level
  orderbook->processEvent(createEvent(4.6, 1, 0, 55UL, static_cast<long>(10020), 'B'));
  book = orderbook->getBook();
  EXPECT_EQ(book.asks[0].price_, 10020);
  EXPECT_EQ(book.asks[0].size_, 5);

  // Touches 2 levels
  orderbook->processEvent(createEvent(4.7, 1, 0, 25UL, static_cast<long>(10022), 'B'));
  book = orderbook->getBook();
  EXPECT_EQ(book.asks[0].price_, 10022);
  EXPECT_EQ(book.asks[0].size_, 80);

  // Touches 1 level
  orderbook->processEvent(createEvent(4.8, 1, 0, 90, static_cast<long>(10018), 'S'));
  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10018);
  EXPECT_EQ(book.bids[0].size_, 10);

  // Touches 2 levels
  orderbook->processEvent(createEvent(4.9, 1, 0, 40, static_cast<long>(10016), 'S'));
  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10016);
  EXPECT_EQ(book.bids[0].size_, 70);

  // Touches 3 level
  orderbook->processEvent(createEvent(5.0, 1, 0, 196, static_cast<long>(10012), 'S'));
  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10012);
  EXPECT_EQ(book.bids[0].size_, 74);
}

TEST_F(OrderBookTest, InterleavedOrdersManyCrossedBook2) {
  // Initial batch of orders with varying sizes at different price levels
  const int num_levels =
      8;  // This represents the number of levels we'll add, not the book display limit
  const std::array<unsigned long, 8> sizes = {150, 200, 100, 300, 250, 175, 225, 125};

  // Build initial bid book with varying sizes
  for (int i = 0; i < num_levels; i++) {
    orderbook->processEvent(
        createEvent(1.0 + i * 0.1, 1, 2001 + i, sizes[i], static_cast<long>(10000 + i * 3), 'B'));
  }

  // Build initial ask book with varying sizes, slightly offset prices
  for (int i = 0; i < num_levels; i++) {
    orderbook->processEvent(createEvent(2.0 + i * 0.1, 1, 3001 + i, sizes[num_levels - 1 - i],
                                        static_cast<long>(10024 + i * 3), 'S'));
  }

  // Verify initial book state - focus on best levels
  auto book = orderbook->getBook();

  // Verify best bid/ask
  EXPECT_EQ(book.bids[0].price_, 10021);  // Best bid should be at highest price
  EXPECT_EQ(book.bids[0].size_, 125);
  EXPECT_EQ(book.asks[0].price_, 10024);  // Best ask should be at lowest price
  EXPECT_EQ(book.asks[0].size_, 125);

  // Test scenario 1: Large aggressive buy order that crosses multiple levels
  orderbook->processEvent(createEvent(3.0, 1, 0, 400, static_cast<long>(10030), 'B'));
  book = orderbook->getBook();
  EXPECT_EQ(book.asks[0].price_, 10030);
  EXPECT_EQ(book.asks[0].size_, 125);  // Original 175 - 50 executed

  // Test scenario 2: Series of small orders that gradually cross the book
  for (int i = 0; i < 3; i++) {
    orderbook->processEvent(
        createEvent(4.0 + i * 0.1, 1, 4001 + i, 50, static_cast<long>(10027 + i * 3), 'B'));
  }
  book = orderbook->getBook();
  EXPECT_EQ(book.bids[0].price_, 10027);
  EXPECT_EQ(book.bids[0].size_, 50);
  EXPECT_EQ(book.asks[0].price_, 10030);
  EXPECT_EQ(book.asks[0].size_, 25);

  // Test scenario 3: Alternating buys and sells that create crossed conditions
  orderbook->processEvent(createEvent(5.0, 1, 5001, 200, static_cast<long>(10036), 'B'));
  orderbook->processEvent(createEvent(5.1, 1, 5002, 150, static_cast<long>(10018), 'S'));
  orderbook->processEvent(createEvent(5.2, 1, 5003, 175, static_cast<long>(10033), 'B'));
  orderbook->processEvent(createEvent(5.3, 1, 5004, 125, static_cast<long>(10021), 'S'));

  book = orderbook->getBook();

  // Verify state after complex crossing
  EXPECT_EQ(book.bids[0].price_, 10018);
  EXPECT_EQ(book.bids[0].size_, 225);  // Remaining after crosses
  EXPECT_EQ(book.asks[0].price_, 10036);
  EXPECT_EQ(book.asks[0].size_, 300);  // Remaining after crosses

  // Test scenario 4: Large market sweep that crosses multiple levels
  orderbook->processEvent(createEvent(6.0, 1, 0, 500, static_cast<long>(10036), 'B'));
  book = orderbook->getBook();

  // Verify state after market sweep - focus on best ask properties
  EXPECT_EQ(book.asks[0].price_, 10039);  // Best ask should have moved up after sweep
  EXPECT_EQ(orderbook->getBestAskPrice().value_or(0),
            10039);  // Verify through public interface as well
}

*/

auto main(int argc, char** argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
