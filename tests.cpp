#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <random>
#include <chrono>
#include <future>
#include <syncstream>

#include "orderedqueue.hpp"
#include "orderbook.hpp"
#include "forwardlistadaptor.hpp"

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

struct TestEvent {
  unsigned long seqNum_;
  int data_;
  std::string metadata_;

  TestEvent(unsigned long seq, int data) : seqNum_(seq), data_(data) {}
  TestEvent(unsigned long seq, int data, std::string metadata) : 
    seqNum_(seq), data_(data), metadata_(metadata) {}
  TestEvent() : seqNum_(0), data_(0), metadata_{} {}
};

/*
class OrderedMPMCQueueTest : public ::testing::Test {
protected:
    static constexpr size_t QUEUE_CAPACITY = 1024;
    OrderedMPMCQueue<TestEvent, QUEUE_CAPACITY> queue;
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
        threads.emplace_back([&, i]() {
            queue.enqueue(TestEvent(indices[i], indices[i]));
        });
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
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Start consumers
    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            while (total_consumed.load() < NUM_PRODUCERS * EVENTS_PER_PRODUCER) {
                if (auto result = queue.try_dequeue()) {
                    EXPECT_EQ(result->data_, result->seqNum_);
                    total_consumed.fetch_add(1);
                }
            }
        });
    }

    // Start producers
    for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            for (size_t i = 0; i < EVENTS_PER_PRODUCER; ++i) {
                size_t seq = p * EVENTS_PER_PRODUCER + i;
                queue.enqueue(TestEvent(seq, seq));
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
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
    // Insert events with gaps
    ASSERT_TRUE(queue.try_enqueue(TestEvent(0, 0)));
    ASSERT_TRUE(queue.try_enqueue(TestEvent(2, 2)));
    ASSERT_TRUE(queue.try_enqueue(TestEvent(5, 5)));

    // Should get first event only
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, 0);

    // Shouldn't get more events yet
    result = queue.try_dequeue();
    EXPECT_FALSE(result.has_value());

    // Fill the gap
    ASSERT_TRUE(queue.try_enqueue(TestEvent(1, 1)));

    // Now should get the next event
    result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seqNum_, 1);
}

// Test 2: Alternating Producer Consumer
TEST_F(OrderedMPMCQueueTest, AlternatingProducerConsumer) {
    static constexpr size_t NUM_ROUNDS = 1000;
    
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
        while (count < NUM_ROUNDS) {
            if (auto result = queue.try_dequeue()) {
                received.push_back(*result);
                count++;
            }
        }
    });

    producer.join();
    producer2.join();
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

    for (auto& p : producers) p.join();

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

    for (auto& c : consumers) c.join();

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

// Test 7: Queue Wraparound
TEST_F(OrderedMPMCQueueTest, QueueWraparound) {
    // Fill queue
    for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
        ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    }

    // Remove half
    for (size_t i = 0; i < QUEUE_CAPACITY/2; ++i) {
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->seqNum_, i);
    }

    // Add new elements (should wrap around internal buffer)
    for (size_t i = QUEUE_CAPACITY; i < QUEUE_CAPACITY + QUEUE_CAPACITY/2; ++i) {
        ASSERT_TRUE(queue.try_enqueue(TestEvent(i, i)));
    }

    // Verify all elements
    for (size_t i = QUEUE_CAPACITY/2; i < QUEUE_CAPACITY + QUEUE_CAPACITY/2; ++i) {
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
        0,
        1000000,
        2000000,
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
    EXPECT_EQ(result->seqNum_, 0);

    // Should be able to enqueue again
    EXPECT_TRUE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 0)));

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

*/

class OrderBookTest : public ::testing::Test {
protected:
    static constexpr int N = 5;
    using BidContainer = ForwardListAdaptor<std::greater<Message::order>, N>;
    using AskContainer = ForwardListAdaptor<std::less<Message::order>, N>;
  // using DecType = OrderBook<BidContainer, AskContainer>;
  // using OrderBookType = TradeJournalerOrderBook<BidContainer, AskContainer>;
    
    void SetUp() override {
      orderbook = std::make_unique<OrderBook<BidContainer, AskContainer>>();
      // orderbook = std::make_unique<OrderBookType>(std::make_unique<DecType>());
    }

  // std::unique_ptr<TradeJournalerOrderBook<BidContainer, AskContainer>> orderbook;
  std::unique_ptr<OrderBook<BidContainer, AskContainer>> orderbook;

    Message::eventLOBSTER createEvent(double time, short type, unsigned long orderId, 
                                    unsigned size, long price, char direction) {
        return Message::eventLOBSTER{
            0,        // seqNum gets set by EventStream
            time,
            type,
            orderId,
            size,
            price,
            direction
        };
    }
};

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
    EXPECT_EQ(book.bids[0].size_, 75); // First order partially executed (25 left)
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
    orderbook->processEvent(createEvent(1.1, 2, 1001, 0, 0, 'B')); // Cancel
    
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
    EXPECT_EQ(book.bids[0].price_, 10100); // Best bid first
    EXPECT_EQ(book.bids[1].price_, 10000);
    EXPECT_EQ(book.bids[2].price_, 9900);
}

TEST_F(OrderBookTest, OrderUpdate) {
    orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
    orderbook->processEvent(createEvent(1.1, 5, 1001, 150, 10000, 'B')); // Update size
    
    auto book = orderbook->getBook();
    ASSERT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.bids[0].size_, 150);
}

TEST_F(OrderBookTest, CrossedBook) {
    // Add bid and ask that could match
    orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
    orderbook->processEvent(createEvent(1.1, 1, 2001, 100, 9900, 'S'));
    
    auto book = orderbook->getBook();
    ASSERT_EQ(book.bids.size(), 1);
    ASSERT_EQ(book.asks.size(), 1);
    EXPECT_GT(book.bids[0].price_, book.asks[0].price_);
}

TEST_F(OrderBookTest, LargeBookSnapshot) {
    // Create baseline book with multiple levels
    for (int i = 0; i < 10; ++i) {
        orderbook->processEvent(createEvent(1.0 + i, 1, 1001 + i, 100, 10000 + i * 100, 'B'));
        orderbook->processEvent(createEvent(1.0 + i, 1, 2001 + i, 100, 10100 + i * 100, 'S'));
    }
    
    auto book = orderbook->getBook();
    ASSERT_EQ(book.bids.size(), 5); // Should only show N levels
    ASSERT_EQ(book.asks.size(), 5);
    
    // Verify price ordering
    for (size_t i = 1; i < book.bids.size(); ++i) {
        EXPECT_GT(book.bids[i-1].price_, book.bids[i].price_);
        EXPECT_LT(book.asks[i-1].price_, book.asks[i].price_);
    }
}

TEST_F(OrderBookTest, FullOrderExecution) {
    orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B'));
    orderbook->processEvent(createEvent(1.1, 1, 1002, 50, 10000, 'B'));
    orderbook->processEvent(createEvent(1.2, 4, 0, 100, 10000, 'B')); // Execute first order fully
    
    auto book = orderbook->getBook();
    ASSERT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.bids[0].price_, 10000);
    EXPECT_EQ(book.bids[0].size_, 50); // Only second order remains
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
    EXPECT_EQ(book.bids[0].size_, 105); // Only part of second order and full third order remain
}

TEST_F(OrderBookTest, BaseLine1_SimpleBidExecution) {
    // Submit orders
    orderbook->processEvent(createEvent(1.0, 1, 1001, 100, 10000, 'B')); // Bid 100@100.00
    orderbook->processEvent(createEvent(1.1, 1, 1002, 150, 10000, 'B')); // Bid 150@100.00
    orderbook->processEvent(createEvent(1.2, 1, 1003, 200, 10001, 'B')); // Bid 200@100.01
    
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
    EXPECT_EQ(book.bids[0].size_, 50);   // 200 - 150 = 50
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
    orderbook->processEvent(createEvent(1.6, 4, 0, 175, 10002, 'B')); // Partial execution at best bid
    orderbook->processEvent(createEvent(1.7, 4, 0, 100, 10004, 'S')); // Partial execution at ask level

    book = orderbook->getBook();
    EXPECT_EQ(book.bids[0].price_, 10002);
    EXPECT_EQ(book.bids[0].size_, 25);
    EXPECT_EQ(book.asks[0].price_, 10003);
    EXPECT_EQ(book.asks[0].size_, 20);
}

TEST_F(OrderBookTest, BaseLine3_MultiLevelExecution) {
    // Build deep book on bid side
    for(int i = 0; i < 5; i++) {
        orderbook->processEvent(createEvent(1.0 + i, 1, 1001 + i, 100, 10000 + i, 'B'));
        orderbook->processEvent(createEvent(1.5 + i, 1, 1501 + i, 100, 10000 + i, 'B')); // Same price levels
    }

    auto book = orderbook->getBook();
    ASSERT_EQ(book.bids.size(), 5);
    for(int i = 0; i < 5; i++) {
        EXPECT_EQ(book.bids[i].price_, 10004 - i);
        EXPECT_EQ(book.bids[i].size_, 200);  // Both orders at each level
    }

    book = orderbook->getBook();

    // Execute at multiple levels
    orderbook->processEvent(createEvent(2.0, 4, 0, 150, 10004, 'B')); // Best bid
    orderbook->processEvent(createEvent(2.1, 4, 0, 250, 10003, 'B')); // Second level
    orderbook->processEvent(createEvent(2.2, 4, 0, 100, 10002, 'B')); // Third level

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
    for(int i = 0; i < 5; i++) {
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
    orderbook->processEvent(createEvent(3.0, 4, 0, 100, 10004, 'B')); // Best bid
    orderbook->processEvent(createEvent(3.1, 4, 0, 144, 10010, 'S')); // Best ask
    book = orderbook->getBook();
    orderbook->processEvent(createEvent(3.2, 4, 0, 100, 10003, 'B')); // Second best bid
    orderbook->processEvent(createEvent(3.3, 4, 0, 186, 10011, 'S'));  // Second best ask

    book = orderbook->getBook();
    EXPECT_EQ(book.bids[0].price_, 10003);
    EXPECT_EQ(book.bids[0].size_, 200);
    EXPECT_EQ(book.bids[1].price_, 10002);
    EXPECT_EQ(book.bids[1].size_, 200);
    EXPECT_EQ(book.asks[0].price_, 10011);
    EXPECT_EQ(book.asks[0].size_, 70); 
    EXPECT_EQ(book.asks[1].price_, 10012);
    EXPECT_EQ(book.asks[1].size_, 200);
}

TEST_F(OrderBookTest, InterleavedOrdersMany1) {
    // Initial batch of 10 orders at different price levels
    for(int i = 0; i < 10; i++) {
        orderbook->processEvent(createEvent(1.0 + i*0.1, 1, 1001 + i, 100UL, 
                              static_cast<long>(10000 + i*2), 'B'));
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
    for(int i = 0; i < 5; i++) {
        orderbook->processEvent(createEvent(2.5 + i*0.1, 1, 1101 + i, 150UL, 
                              static_cast<long>(10019 + i*2), 'B'));
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
    for(int i = 0; i < 5; i++) {
        orderbook->processEvent(createEvent(3.5 + i*0.1, 1, 1201 + i, 200UL, 
                              static_cast<long>(10028 + i*2), 'B'));
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
    for(int i = 0; i < 5; i++) {
        orderbook->processEvent(createEvent(4.0 + i*0.1, 1, 1301 + i, 125UL, 
                              static_cast<long>(10037 + i*2), 'B'));
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
    EXPECT_EQ(book.bids[1].price_, 10037);
    EXPECT_EQ(book.bids[1].size_, 125);
    EXPECT_EQ(book.bids[4].price_, 10032);
    EXPECT_EQ(book.bids[4].size_, 200);
    EXPECT_EQ(orderbook->getBestBidPrice(), 10039);
}

auto main(int argc, char** argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
