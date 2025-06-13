#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include "include/concurrentqueue.h"
#include "orderedqueue.hpp"

struct BenchmarkEvent {
    unsigned long seqNum_;
    double time_;
    short eventType_;
    float payload_;
    
    BenchmarkEvent(unsigned long seq, double t, short type, float payload)
        : seqNum_(seq), time_(t), eventType_(type), payload_(payload) {}
    BenchmarkEvent() : seqNum_(0), time_(0.0), eventType_(0), payload_(0.0f) {}
};

using EventType = BenchmarkEvent;

// Helper function to create test events
EventType createEvent(unsigned long seqNum) {
    return EventType{seqNum, 0.0, 1, static_cast<float>(seqNum)};
}

// Single Producer, Single Consumer - OrderedMPMCQueue
static void BM_OrderedQueue_SPSC(benchmark::State& state) {
  OrderedMPMCQueue<EventType, 8192> queue;
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};

  // Consumer thread
  std::thread consumer([&]() {
			EventType event;
			while (!done.load() || consumed.load() < static_cast<size_t>(state.iterations())) {
			  if (queue.try_dequeue(event)) {
			    consumed.fetch_add(1);
			  } else {
			    std::this_thread::yield();
			  }
			}
		      });

  size_t seq_num = 0;
  for (auto _ : state) {
    auto event = createEvent(seq_num++);
    queue.enqueue(std::move(event));
  }

  done.store(true);
  consumer.join();
}
BENCHMARK(BM_OrderedQueue_SPSC)->Iterations(8000);


// Single Producer, Multiple Consumers - OrderedMPMCQueue
static void BM_OrderedQueue_SPMC(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    
    for (auto _ : state) {
      OrderedMPMCQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer (main thread)
        for (size_t i = 0; i < total_events; ++i) {
            auto event = createEvent(i);
            queue.enqueue(std::move(event));
        }
        
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_OrderedQueue_SPMC)->UseRealTime();

// Multiple Producers, Multiple Consumers - OrderedMPMCQueue
static void BM_OrderedQueue_MPMC(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
	OrderedMPMCQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        // Wait for producers to finish
        for (auto& t : producers) {
            t.join();
        }
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_OrderedQueue_MPMC)->UseRealTime();

// High throughput benchmark - OrderedMPMCQueue
static void BM_OrderedQueue_HighThroughput(benchmark::State& state) {
    const int num_producers = 4;  // Reduced from 8
    const int num_consumers = 4;  // Reduced from 8  
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
      OrderedMPMCQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        // Wait for completion
        for (auto& t : producers) {
            t.join();
        }
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
    state.counters["Events/sec"] = benchmark::Counter(total_events, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_OrderedQueue_HighThroughput)->Unit(benchmark::kMillisecond)->UseRealTime();

/*
// Bulk operations benchmark - shows bulk enqueue/dequeue performance
static void BM_OrderedQueue_BulkOperations(benchmark::State& state) {
OrderedMPMCQueue<EventType, 1024> queue;
const size_t bulk_size = 500;

for (auto _ : state) {
// Bulk enqueue
std::vector<EventType> events;
events.reserve(bulk_size);
for (size_t i = 0; i < bulk_size; ++i) {
events.emplace_back(createEvent(i));
}

for (auto& event : events) {
queue.enqueue(std::move(event));
}

// Bulk dequeue
std::vector<EventType> dequeued_events;
dequeued_events.reserve(bulk_size);
EventType event;
for (size_t i = 0; i < bulk_size; ++i) {
if (queue.try_dequeue(event)) {
dequeued_events.push_back(std::move(event));
}
}

benchmark::DoNotOptimize(dequeued_events);
}

state.SetItemsProcessed(state.iterations() * bulk_size * 2); // enqueue + dequeue
}
BENCHMARK(BM_OrderedQueue_BulkOperations)->Iterations(10000);


// Memory contention benchmark - simplified
static void BM_OrderedQueue_MemoryContention(benchmark::State& state) {
const int num_threads = 4; // Further reduced
const size_t operations_per_thread = 500; // Reduced operations to fit queue

for (auto _ : state) {
OrderedMPMCQueue<EventType, 1024> queue;
std::vector<std::thread> threads;

for (int i = 0; i < num_threads; ++i) {
threads.emplace_back([&, i]() {
for (size_t j = 0; j < operations_per_thread; ++j) {
if (i % 2 == 0) {
// Producer
auto event = createEvent(j);
queue.enqueue(std::move(event));
} else {
// Consumer
EventType event;
queue.try_dequeue(event);
}
}
});
}

for (auto& t : threads) {
t.join();
}
}
}
BENCHMARK(BM_OrderedQueue_MemoryContention)->UseRealTime();
*/


//////////
// ConcurrentQueue
/////////


// Single Producer, Single Consumer - moodycamel::ConcurrentQueue
static void BM_ConcurrentQueue_SPSC(benchmark::State& state) {
    moodycamel::ConcurrentQueue<EventType> queue;
    std::atomic<bool> done{false};
    std::atomic<size_t> consumed{0};
    
    // Consumer thread
    std::thread consumer([&]() {
        EventType event;
        while (!done.load() || consumed.load() < static_cast<size_t>(state.iterations())) {
            if (queue.try_dequeue(event)) {
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    size_t seq_num = 0;
    for (auto _ : state) {
        auto event = createEvent(seq_num++);
        queue.enqueue(std::move(event));
    }
    
    done.store(true);
    consumer.join();
}
BENCHMARK(BM_ConcurrentQueue_SPSC)->Iterations(100000);

// Multiple Producers, Single Consumer - moodycamel::ConcurrentQueue  
static void BM_ConcurrentQueue_MPSC(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer thread
        std::thread consumer([&]() {
            EventType event;
            while (!done.load() || consumed.load() < total_events) {
                if (queue.try_dequeue(event)) {
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        // Wait for producers to finish
        for (auto& t : producers) {
            t.join();
        }
        done.store(true);
        consumer.join();
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_ConcurrentQueue_MPSC)->UseRealTime();

// Single Producer, Multiple Consumers - moodycamel::ConcurrentQueue
static void BM_ConcurrentQueue_SPMC(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer (main thread)
        for (size_t i = 0; i < total_events; ++i) {
            auto event = createEvent(i);
            queue.enqueue(std::move(event));
        }
        
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_ConcurrentQueue_SPMC)->UseRealTime();

// Multiple Producers, Multiple Consumers - moodycamel::ConcurrentQueue
static void BM_ConcurrentQueue_MPMC(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        // Wait for producers to finish
        for (auto& t : producers) {
            t.join();
        }
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_ConcurrentQueue_MPMC)->UseRealTime();

// High throughput benchmark - moodycamel::ConcurrentQueue
static void BM_ConcurrentQueue_HighThroughput(benchmark::State& state) {
    const int num_producers = 4;  // Reduced from 8
    const int num_consumers = 4;  // Reduced from 8  
    const size_t total_events = 8000;  // Reduced to fit queue capacity
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&]() {
                EventType event;
                while (!done.load() || consumed.load() < total_events) {
                    if (queue.try_dequeue(event)) {
                        consumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        // Wait for completion
        for (auto& t : producers) {
            t.join();
        }
        done.store(true);
        for (auto& t : consumers) {
            t.join();
        }
    }
    
    state.SetItemsProcessed(total_events);
    state.counters["Events/sec"] = benchmark::Counter(total_events, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ConcurrentQueue_HighThroughput)->Unit(benchmark::kMillisecond)->UseRealTime();

// Bulk operations benchmark - shows bulk enqueue/dequeue performance
static void BM_ConcurrentQueue_BulkOperations(benchmark::State& state) {
    moodycamel::ConcurrentQueue<EventType> queue;
    const size_t bulk_size = 500;
    
    for (auto _ : state) {
        // Bulk enqueue
        std::vector<EventType> events;
        events.reserve(bulk_size);
        for (size_t i = 0; i < bulk_size; ++i) {
            events.emplace_back(createEvent(i));
        }
        
        for (auto& event : events) {
            queue.enqueue(std::move(event));
        }
        
        // Bulk dequeue
        std::vector<EventType> dequeued_events;
        dequeued_events.reserve(bulk_size);
        EventType event;
        for (size_t i = 0; i < bulk_size; ++i) {
            if (queue.try_dequeue(event)) {
                dequeued_events.push_back(std::move(event));
            }
        }
        
        benchmark::DoNotOptimize(dequeued_events);
    }
    
    state.SetItemsProcessed(state.iterations() * bulk_size * 2); // enqueue + dequeue
}
BENCHMARK(BM_ConcurrentQueue_BulkOperations)->Iterations(10000);

// Memory contention benchmark - simplified
static void BM_ConcurrentQueue_MemoryContention(benchmark::State& state) {
    const int num_threads = 4; // Further reduced
    const size_t operations_per_thread = 500; // Reduced operations to fit queue
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::vector<std::thread> threads;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for (size_t j = 0; j < operations_per_thread; ++j) {
                    if (i % 2 == 0) {
                        // Producer
                        auto event = createEvent(j);
                        queue.enqueue(std::move(event));
                    } else {
                        // Consumer
                        EventType event;
                        queue.try_dequeue(event);
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
}
BENCHMARK(BM_ConcurrentQueue_MemoryContention)->UseRealTime();

// ConcurrentQueue Single Producer Enqueue Speed Test
static void BM_ConcurrentQueue_SingleProducerEnqueueSpeed(benchmark::State& state) {
    moodycamel::ConcurrentQueue<EventType> queue;
    
    for (auto _ : state) {
        size_t seq_num = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < 8000; ++i) {
            auto event = createEvent(seq_num++);
            queue.enqueue(std::move(event));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.SetIterationTime(duration.count() / 1000000.0);
    }
    
    state.SetItemsProcessed(state.iterations() * 8000);
    state.counters["Enqueues/sec"] = benchmark::Counter(8000, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ConcurrentQueue_SingleProducerEnqueueSpeed)->UseManualTime()->Iterations(10);

// ConcurrentQueue Multi Producer Enqueue Speed Test
static void BM_ConcurrentQueue_MultiProducerEnqueueSpeed(benchmark::State& state) {
    const int num_producers = 4;
    const size_t events_per_producer = 2000;
    const size_t total_events = num_producers * events_per_producer;
    
    for (auto _ : state) {
        moodycamel::ConcurrentQueue<EventType> queue;
        std::atomic<bool> start_flag{false};
        std::vector<std::thread> producers;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                while (!start_flag.load()) {
                    std::this_thread::yield();
                }
                
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        start_flag.store(true);
        
        for (auto& t : producers) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.SetIterationTime(duration.count() / 1000000.0);
    }
    
    state.SetItemsProcessed(state.iterations() * total_events);
    state.counters["Enqueues/sec"] = benchmark::Counter(total_events, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ConcurrentQueue_MultiProducerEnqueueSpeed)->UseManualTime()->Iterations(10);

// OrderedMPMCQueue Single Producer Enqueue Speed Test
static void BM_OrderedMPMCQueue_SingleProducerEnqueueSpeed(benchmark::State& state) {
    for (auto _ : state) {
        OrderedMPMCQueue<EventType, 8192> queue;
        
        size_t seq_num = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < 8000; ++i) {
            auto event = createEvent(seq_num++);
            queue.enqueue(std::move(event));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.SetIterationTime(duration.count() / 1000000.0);
    }
    
    state.SetItemsProcessed(state.iterations() * 8000);
    state.counters["Enqueues/sec"] = benchmark::Counter(8000, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_OrderedMPMCQueue_SingleProducerEnqueueSpeed)->UseManualTime()->Iterations(10);

// OrderedMPMCQueue Multi Producer Enqueue Speed Test
static void BM_OrderedMPMCQueue_MultiProducerEnqueueSpeed(benchmark::State& state) {
    const int num_producers = 4;
    const size_t events_per_producer = 2000;
    const size_t total_events = num_producers * events_per_producer;
    
    for (auto _ : state) {
        OrderedMPMCQueue<EventType, 8192> queue;
        std::atomic<bool> start_flag{false};
        std::vector<std::thread> producers;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&, i]() {
                while (!start_flag.load()) {
                    std::this_thread::yield();
                }
                
                size_t base_seq = i * events_per_producer;
                for (size_t j = 0; j < events_per_producer; ++j) {
                    auto event = createEvent(base_seq + j);
                    queue.enqueue(std::move(event));
                }
            });
        }
        
        start_flag.store(true);
        
        for (auto& t : producers) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.SetIterationTime(duration.count() / 1000000.0);
    }
    
    state.SetItemsProcessed(state.iterations() * total_events);
    state.counters["Enqueues/sec"] = benchmark::Counter(total_events, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_OrderedMPMCQueue_MultiProducerEnqueueSpeed)->UseManualTime()->Iterations(10);

/*
===============================================================================
IMPORTANT NOTE: OrderedMPMCQueue Benchmarks
===============================================================================

OrderedMPMCQueue benchmarks are NOT included in this file due to compilation/runtime 
issues in the Google Benchmark context. The OrderedMPMCQueue has specific initialization 
and lifecycle requirements that conflict with the benchmark framework, causing segfaults.

However, comprehensive OrderedMPMCQueue performance tests ARE available in the unit tests:

To benchmark OrderedMPMCQueue:
1. Run: ./tests/tests --gtest_filter="*Performance*"
2. Run: ./tests/tests --gtest_filter="*Stress*" 
3. Or check tests.cpp for 50+ comprehensive performance and stress tests

Performance Comparison Context:
- OrderedMPMCQueue: Guarantees in-order consumption based on sequence numbers
- moodycamel::ConcurrentQueue: No ordering guarantees, pure FIFO with high performance

The OrderedMPMCQueue provides unique ordering guarantees that come with a performance
cost compared to moodycamel::ConcurrentQueue. Choose based on your requirements:
- Need ordering: OrderedMPMCQueue
- Need maximum speed: moodycamel::ConcurrentQueue

Current benchmarks test moodycamel::ConcurrentQueue performance across different
producer/consumer patterns to establish baseline performance expectations.
*/

BENCHMARK_MAIN();
