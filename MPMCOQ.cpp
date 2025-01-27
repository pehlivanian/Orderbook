#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <random>
#include <chrono>
#include <future>
#include "ordered_mpmc_queue.hpp"

struct TestEvent {
    unsigned long seqNumber_;
    int data_;
    std::string metadata_;

    TestEvent(unsigned long seq, int data) : seqNumber_(seq), data_(data) {}
    TestEvent(unsigned long seq, int data, std::string metadata) 
        : seqNumber_(seq), data_(data), metadata_(metadata) {}
    TestEvent() : seqNumber_(0), data_(0) {}
};

class OrderedMPMCQueueExtraTest : public ::testing::Test {
protected:
    static constexpr size_t QUEUE_CAPACITY = 1024;
    OrderedMPMCQueue<TestEvent, QUEUE_CAPACITY> queue;

    void SetUp() override {
        while (queue.try_dequeue()) {}
    }
};

// Test 1: Very Large Sequence Number Jumps
TEST_F(OrderedMPMCQueueExtraTest, VeryLargeSequenceJumps) {
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
    EXPECT_EQ(result->seqNumber_, 0);

    // No more events should be available due to gaps
    result = queue.try_dequeue();
    EXPECT_FALSE(result.has_value());
}

// Test 2: Producer Back-pressure
TEST_F(OrderedMPMCQueueExtraTest, ProducerBackpressure) {
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
TEST_F(OrderedMPMCQueueExtraTest, ConsumerPriority) {
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

// Test 4: Sequence Number Overflow
TEST_F(OrderedMPMCQueueExtraTest, SequenceNumberOverflow) {
    // Test near ULONG_MAX boundary
    std::vector<unsigned long> seqNumbers = {
        ULONG_MAX - 2,
        ULONG_MAX - 1,
        ULONG_MAX
    };

    for (auto seq : seqNumbers) {
        ASSERT_TRUE(queue.try_enqueue(TestEvent(seq, 0)));
    }

    // Verify we can dequeue them in order
    for (auto expected_seq : seqNumbers) {
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->seqNumber_, expected_seq);
    }
}

// Test 5: Interleaved Small/Large Events with Move Semantics
TEST_F(OrderedMPMCQueueExtraTest, InterleavedEventSizes) {
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
        EXPECT_EQ(result->seqNumber_, i);
        if (i % 2 == 1) {
            EXPECT_EQ(result->metadata_.size(), 10000);
        }
    }
}

// Test 6: Random Sequence Number Distribution
TEST_F(OrderedMPMCQueueExtraTest, RandomSequenceDistribution) {
    static constexpr size_t NUM_EVENTS = 1000;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long> dis(0, NUM_EVENTS * 10);

    std::set<unsigned long> used_sequences;
    std::vector<unsigned long> sequences;

    // Generate random unique sequence numbers
    while (sequences.size() < NUM_EVENTS) {
        unsigned long seq = dis(gen);
        if (used_sequences.insert(seq).second) {
            sequences.push_back(seq);
        }
    }

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
        EXPECT_EQ(result->seqNumber_, expected_seq);
    }
}

// Test 7: Producer Cancellation
TEST_F(OrderedMPMCQueueExtraTest, ProducerCancellation) {
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
TEST_F(OrderedMPMCQueueExtraTest, QueueRecoveryAfterFull) {
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
    EXPECT_EQ(result->seqNumber_, 0);

    // Should be able to enqueue again
    EXPECT_TRUE(queue.try_enqueue(TestEvent(QUEUE_CAPACITY, 0)));

    // Queue should maintain correct size
    EXPECT_EQ(queue.size(), QUEUE_CAPACITY);
}

// Test 9: Multiple Consumer Race for Last Item
TEST_F(OrderedMPMCQueueExtraTest, MultipleConsumerRaceForLast) {
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
                EXPECT_EQ(result->seqNumber_, 0);
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
TEST_F(OrderedMPMCQueueExtraTest, SparseSequenceDistribution) {
    static constexpr size_t STRIDE = 1000;  // Large gaps between sequence numbers
    static constexpr size_t NUM_EVENTS = 100;

    // Enqueue events with large gaps between sequence numbers
    for (size_t i = 0; i < NUM_EVENTS; ++i) {
        ASSERT_TRUE(queue.try_enqueue(TestEvent(i * STRIDE, i)));
    }

    // Fill in some gaps
    for (size_t i = 0; i < NUM_EVENTS - 1; ++i) {
        for (size_t j = 1; j < STRIDE; j += STRIDE/2) {
            ASSERT_TRUE(queue.try_enqueue(TestEvent(i * STRIDE + j, i)));
        }
    }

    // Verify events come out in sequence order
    size_t last_seq = 0;
    while (auto result = queue.try_dequeue()) {
        EXPECT_EQ(result->seqNumber_, last_seq);
        last_seq++;
        while (last_seq % (STRIDE/2) != 0 && last_seq % STRIDE != 0) {
            last_seq++;
        }
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}