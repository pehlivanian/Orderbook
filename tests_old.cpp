#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <random>
#include "orderedqueue.hpp"

struct TestEvent {
    unsigned long seqNumber_;
    int data_;

    TestEvent(unsigned long seq, int data) : seqNumber_(seq), data_(data) {}
    TestEvent() : seqNumber_(0), data_(0) {}
};

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
    EXPECT_EQ(result->seqNumber_, 0);
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
        EXPECT_EQ(result->seqNumber_, i);
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
        EXPECT_EQ(result->seqNumber_, i);
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
        EXPECT_EQ(result->seqNumber_, i);
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
                    EXPECT_EQ(result->data_, result->seqNumber_);
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
                    EXPECT_EQ(result->data_, result->seqNumber_);
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

auto main(int argc, char** argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
