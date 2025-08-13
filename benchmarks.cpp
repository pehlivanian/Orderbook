#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

// MoodyCamel ConcurrentQueue
#include "include/concurrentqueue.h"

// Disruptor
#include "Disruptor/Disruptor.h"
#include "Disruptor/ThreadPerTaskScheduler.h"
#include "Disruptor/BusySpinWaitStrategy.h"
#include "Disruptor/WorkerPool.h"
#include "Disruptor/IWorkHandler.h"
#include "Disruptor/IgnoreExceptionHandler.h"
#include "Disruptor/BasicExecutor.h"

// OrderedMPMCQueue
#include "orderedqueue.hpp"

// DisruptorQueue
#include "disruptorqueue.hpp"

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

//////////////////////////////////////////
// SINGLE PRODUCER SINGLE CONSUMER (SPSC)
//////////////////////////////////////////

static void BM_SPSC_OrderedQueue(benchmark::State& state) {
    OrderedMPMCQueue<EventType, 1<<14> queue;
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
BENCHMARK(BM_SPSC_OrderedQueue)->Iterations(10000);

static void BM_SPSC_ConcurrentQueue(benchmark::State& state) {
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
BENCHMARK(BM_SPSC_ConcurrentQueue)->Iterations(10000);

static void BM_SPSC_DisruptorQueue(benchmark::State& state) {
    DisruptorQueue<EventType, 1<<14> queue;
    std::atomic<bool> done{false};
    std::atomic<size_t> consumed{0};

    queue.addConsumer([&](EventType& event) {
        consumed.fetch_add(1);
    });

    queue.start();

    size_t seq_num = 0;
    for (auto _ : state) {
        auto event = createEvent(seq_num++);
        queue.enqueue(std::move(event));
    }

    // Wait for all events to be consumed
    while (consumed.load() < static_cast<size_t>(state.iterations())) {
        std::this_thread::yield();
    }

    done.store(true);
    queue.shutdown();
}
BENCHMARK(BM_SPSC_DisruptorQueue)->Iterations(10000);

//////////////////////////////////////////////////
// MULTIPLE PRODUCER SINGLE CONSUMER (MPSC)
//////////////////////////////////////////////////

static void BM_MPSC_OrderedQueue(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
        OrderedMPMCQueue<EventType, 8192> queue;
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
BENCHMARK(BM_MPSC_OrderedQueue)->UseRealTime();

static void BM_MPSC_ConcurrentQueue(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;
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
BENCHMARK(BM_MPSC_ConcurrentQueue)->UseRealTime();

//////////////////////////////////////////////////
// SINGLE PRODUCER MULTIPLE CONSUMER (SPMC)
//////////////////////////////////////////////////

static void BM_SPMC_OrderedQueue(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;
    
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
BENCHMARK(BM_SPMC_OrderedQueue)->UseRealTime();

static void BM_SPMC_ConcurrentQueue(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;
    
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
BENCHMARK(BM_SPMC_ConcurrentQueue)->UseRealTime();

static void BM_SPMC_DisruptorQueue(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;

    for (auto _ : state) {
        DisruptorQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        for (int i = 0; i < num_consumers; ++i) {
            queue.addConsumer([&](EventType& event) {
                consumed.fetch_add(1);
            });
        }

        queue.start();

        // Producer (main thread)
        for (size_t i = 0; i < total_events; ++i) {
            auto event = createEvent(i);
            queue.enqueue(std::move(event));
        }
        
        // Wait for all events to be consumed
        while (consumed.load() < total_events) {
            std::this_thread::yield();
        }
        
        done.store(true);
        queue.shutdown();
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_SPMC_DisruptorQueue)->UseRealTime();

//////////////////////////////////////////////////
// MULTIPLE PRODUCER MULTIPLE CONSUMER (MPMC)
//////////////////////////////////////////////////

static void BM_MPMC_OrderedQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
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
BENCHMARK(BM_MPMC_OrderedQueue)->UseRealTime();

static void BM_MPMC_ConcurrentQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
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
BENCHMARK(BM_MPMC_ConcurrentQueue)->UseRealTime();

static void BM_MPMC_DisruptorQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;

    for (auto _ : state) {
        DisruptorQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};

        // Consumer threads
        for (int i = 0; i < num_consumers; ++i) {
            queue.addConsumer([&](EventType& event) {
                consumed.fetch_add(1);
            });
        }

        queue.start();

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
        
        // Wait for all events to be consumed
        while (consumed.load() < total_events) {
            std::this_thread::yield();
        }

        done.store(true);
        queue.shutdown();
    }
    
    state.SetItemsProcessed(total_events);
}
BENCHMARK(BM_MPMC_DisruptorQueue)->UseRealTime();

//////////////////////////////////////////////////
// HIGH THROUGHPUT STRESS TEST
//////////////////////////////////////////////////

static void BM_HighThroughput_OrderedQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
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
BENCHMARK(BM_HighThroughput_OrderedQueue)->Unit(benchmark::kMillisecond)->UseRealTime();

static void BM_HighThroughput_ConcurrentQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
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
BENCHMARK(BM_HighThroughput_ConcurrentQueue)->Unit(benchmark::kMillisecond)->UseRealTime();

static void BM_HighThroughput_DisruptorQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
        DisruptorQueue<EventType, 8192> queue;
        std::atomic<bool> done{false};
        std::atomic<size_t> consumed{0};
        
        // Consumer threads
        for (int i = 0; i < num_consumers; ++i) {
            queue.addConsumer([&](EventType& event) {
                consumed.fetch_add(1);
            });
        }

        queue.start();

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
        
        // Wait for all events to be consumed
        while (consumed.load() < total_events) {
            std::this_thread::yield();
        }

        done.store(true);
        queue.shutdown();
    }
    
    state.SetItemsProcessed(total_events);
    state.counters["Events/sec"] = benchmark::Counter(total_events, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_HighThroughput_DisruptorQueue)->Unit(benchmark::kMillisecond)->UseRealTime();

//////////////////////////////////////////////////
// BULK OPERATIONS TEST
//////////////////////////////////////////////////

static void BM_BulkOperations_ConcurrentQueue(benchmark::State& state) {
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
BENCHMARK(BM_BulkOperations_ConcurrentQueue)->Iterations(10000);

BENCHMARK_MAIN();