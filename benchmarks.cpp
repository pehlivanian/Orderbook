#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <cmath>

// MoodyCamel ConcurrentQueue
#include "include/concurrentqueue.h"

// Disruptor
#include "Disruptor/Disruptor.h"
#include "Disruptor/ThreadPerTaskScheduler.h"
#include "Disruptor/BusySpinWaitStrategy.h"
#include "Disruptor/WorkerPool.h"
#include "Disruptor/IWorkHandler.h"
#include "Disruptor/IgnoreExceptionHandler.h"
#include "Disruptor/YieldingWaitStrategy.h"
#include "Disruptor/BasicExecutor.h"

// OrderedMPMCQueue
#include "orderedqueue.hpp"

// ConcurrentQueue
const std::size_t cc_block_size = moodycamel::ConcurrentQueueDefaultTraits::BLOCK_SIZE;

std::size_t cc_qsize(std::size_t num_its, std::size_t num_producers) {
  return   (ceil(num_its / cc_block_size) - 1 + 2 *  num_producers) * cc_block_size;
}
  

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

//////////////////////////////////////////////////
// GENERAL QUEUES; SETUP TIMES
//////////////////////////////////////////////////

static void BM_SPSC_OrderedQueueSetupQueueTime(benchmark::State& state) {
  const int num_consumers = 12;
  const int num_producers = 4;
  
  for (auto _ : state) {
    OrderedMPMCQueue<EventType, 8192> queue;
    benchmark::DoNotOptimize(queue);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_SPSC_OrderedQueueSetupQueueTime)->UseRealTime();

static void BM_SPSC_ConcurrentQueueSetupQueueTime(benchmark::State& state) {
  const int num_consumers = 12;
  const int num_producers = 4;
  const size_t total_events = 8000;
  std::size_t sz = cc_qsize(total_events, num_producers);
    
  for (auto _ : state) {
    moodycamel::ConcurrentQueue<EventType> queue(sz);
    benchmark::DoNotOptimize(queue);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_SPSC_ConcurrentQueueSetupQueueTime)->UseRealTime();

static void BM_SPSC_DisruptorQueueSetupQueueTime(benchmark::State& state) {
  const int num_consumers = 12;
  const int num_producers = 4;
  const size_t total_events = 8000;
  const size_t events_per_producer = total_events / num_producers;
  
  auto config = DisruptorQueueConfig<EventType>{};
  config.put_taskScheduler(std::make_shared<Disruptor::ThreadPerTaskScheduler>());

  for (auto _ : state) {
    DisruptorQueue<EventType, 8192> queue(config);
    benchmark::DoNotOptimize(queue);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_SPSC_DisruptorQueueSetupQueueTime)->UseRealTime();

//////////////////////////////////////////
// SINGLE PRODUCER SINGLE CONSUMER (SPSC)
//////////////////////////////////////////

static void BM_SPSC_OrderedQueueEnqueue(benchmark::State& state) {
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
BENCHMARK(BM_SPSC_OrderedQueueEnqueue)->Iterations(10000);

static void BM_SPSC_OrderedQueueDequeue(benchmark::State& state) {
  OrderedMPMCQueue<EventType, 1<<14> queue;  
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};
  constexpr int total_events = 10000;

  // Producer
  for (size_t i=0; i<total_events; ++i) {
    auto event = createEvent(i);
    queue.enqueue(event);
  }
  
  EventType event;
  for (auto _ : state) {
    event = queue.dequeue();
  }

}
BENCHMARK(BM_SPSC_OrderedQueueDequeue)->Iterations(10000);

static void BM_SPSC_OrderedQueueRandomOrderEnqueue(benchmark::State& state) {
    OrderedMPMCQueue<EventType, 1<<14> queue;
    std::atomic<bool> done{false};
    std::atomic<size_t> consumed{0};
    constexpr int total_events = 10000;

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

    std::vector<int> indices(total_events);
    std::iota(indices.begin(), indices.end(), 0);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(indices.begin(), indices.end(), gen);

    size_t seq_num = 0;
    for (auto _ : state) {
        auto event = createEvent(indices[seq_num++]);
	// std::cout << "enqueuing: " << event.seqNum_ << std::endl;
        queue.enqueue(std::move(event));
    }

    done.store(true);
    consumer.join();
}
BENCHMARK(BM_SPSC_OrderedQueueRandomOrderEnqueue)->Iterations(10000);

static void BM_SPSC_OrderedQueueRandomOrderDequeue(benchmark::State& state) {
  OrderedMPMCQueue<EventType, 1<<14> queue;
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};
  constexpr int total_events = 10000;

  std::vector<int> indices(total_events);
  std::iota(indices.begin(), indices.end(), 0);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(indices.begin(), indices.end(), gen);

  // Producer
  for (size_t i=0; i<total_events; ++i) {
    auto event = createEvent(indices[i]);
    queue.enqueue(event);
    // std::cout << "event: " << event.seqNum_ << " enqueued\n";
  }
  
  EventType event;
  for (auto _ : state) {
    event = queue.dequeue();
    // std::cout << "event: " << event.seqNum_ << " dequeued\n";
  }
    
}
BENCHMARK(BM_SPSC_OrderedQueueRandomOrderDequeue)->Iterations(10000);

/*
static void BM_SPSC_OrderedQueueTryEnqueue(benchmark::State& state) {
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
    queue.try_enqueue(std::move(event));
  }

  done.store(true);
  consumer.join();

}
BENCHMARK(BM_SPSC_OrderedQueueTryEnqueue)->Iterations(10000);
*/


/*
static void BM_SPSC_OrderedQueueTryDequeue(benchmark::State& state) {
  OrderedMPMCQueue<EventType, 1<<14> queue;
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};\
  constexpr int total_events = 10000;

  // Producer
  for (size_t i=0; i<total_events; ++i) {
    auto event = createEvent(i);
    queue.enqueue(event);
  }

  EventType event;
  for (auto _ : state) {
    while (!queue.try_dequeue(event));
  }

}
BENCHMARK(BM_SPSC_OrderedQueueTryDequeue)->Iterations(10000);
*/


static void BM_SPSC_ConcurrentQueueEnqueue(benchmark::State& state) {

  std::size_t num_its = state.max_iterations;
  std::size_t num_producers = 1;
  std::size_t sz = cc_qsize(num_its, num_producers);
   
  moodycamel::ConcurrentQueue<EventType> queue(sz);
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
BENCHMARK(BM_SPSC_ConcurrentQueueEnqueue)->Iterations(10000);

static void BM_SPSC_ConcurrentQueueDequeue(benchmark::State& state) {

  std::size_t num_its = state.max_iterations;
  std::size_t num_producers = 1;
  std::size_t sz = cc_qsize(num_its, num_producers);

  moodycamel::ConcurrentQueue<EventType> queue(sz);
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};
  const int total_events = 10000;

  // Producer
  for (size_t i=0; i<total_events; ++i) {
    auto event = createEvent(i);
    queue.enqueue(event);
  }

  EventType event;
  for (auto _ : state) {
    while (!queue.try_dequeue(event));
  }
 

}
BENCHMARK(BM_SPSC_ConcurrentQueueDequeue)->Iterations(10000);

static void BM_SPSC_DisruptorQueueEnqueue(benchmark::State& state) {
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
BENCHMARK(BM_SPSC_DisruptorQueueEnqueue)->Iterations(10000);

static void BM_SPSC_DisruptorQueueDequeue(benchmark::State& state) {
  DisruptorQueue<EventType , 1<<14> queue;
  std::atomic<bool> done{false};
  std::atomic<size_t> consumed{0};
  const int total_events = 10000;

  // Single consumed
  queue.addConsumer([&](EventType& event) {
		      consumed.fetch_add(1);
		    });

  queue.start();

  // Single producer
  size_t seq_num = 0;
  for (size_t i=0; i<total_events; ++i) {
    auto event = createEvent(seq_num++);
    queue.enqueue(event);
  }

  for (auto _ : state) {
    while (consumed.load() < static_cast<size_t>(state.iterations())) {
      std::this_thread::yield();
    }
  }

}
BENCHMARK(BM_SPSC_DisruptorQueueDequeue)->Iterations(10000);


static void BM_SPSC_OrderedQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 1;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
      
      state.PauseTiming();
      
      OrderedMPMCQueue<EventType, 8192> queue;
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
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
BENCHMARK(BM_SPSC_OrderedQueueProducerConsumer)->UseRealTime();

static void BM_SPSC_ConcurrentQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 1;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    std::size_t num_its = state.max_iterations;
    std::size_t sz = cc_qsize(total_events, num_producers);
    
    for (auto _ : state) {
      
      state.PauseTiming();

      moodycamel::ConcurrentQueue<EventType> queue(sz);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
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
BENCHMARK(BM_SPSC_ConcurrentQueueProducerConsumer)->UseRealTime();

static void BM_SPSC_DisruptorQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 1;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;

    auto config = DisruptorQueueConfig<EventType>{};
    config.put_waitStrategy(std::make_shared<Disruptor::YieldingWaitStrategy>());
    
    for (auto _ : state) {
      state.PauseTiming();

      DisruptorQueue<EventType, 8192> queue(config);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
        // Single consumer
        queue.addConsumer([&](EventType& event) {
            consumed.fetch_add(1);
        });
        
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
BENCHMARK(BM_SPSC_DisruptorQueueProducerConsumer)->UseRealTime();


//////////////////////////////////////////////////
// MULTIPLE PRODUCER SINGLE CONSUMER (MPSC)
//////////////////////////////////////////////////

static void BM_MPSC_OrderedQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {
      
      state.PauseTiming();
      
      OrderedMPMCQueue<EventType, 8192> queue;
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
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
BENCHMARK(BM_MPSC_OrderedQueueProducerConsumer)->UseRealTime();

static void BM_MPSC_ConcurrentQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    std::size_t num_its = state.max_iterations;
    std::size_t sz = cc_qsize(total_events, num_producers);
    
    for (auto _ : state) {
      
      state.PauseTiming();

      moodycamel::ConcurrentQueue<EventType> queue(sz);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
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
BENCHMARK(BM_MPSC_ConcurrentQueueProducerConsumer)->UseRealTime();

static void BM_MPSC_DisruptorQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;

    auto config = DisruptorQueueConfig<EventType>{};
    config.put_waitStrategy(std::make_shared<Disruptor::YieldingWaitStrategy>());
    
    for (auto _ : state) {
      state.PauseTiming();

      DisruptorQueue<EventType, 8192> queue(config);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};
      
      state.ResumeTiming();
        
        // Single consumer
        queue.addConsumer([&](EventType& event) {
            consumed.fetch_add(1);
        });
        
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
BENCHMARK(BM_MPSC_DisruptorQueueProducerConsumer)->UseRealTime();

//////////////////////////////////////////////////
// SINGLE PRODUCER MULTIPLE CONSUMER (SPMC)
//////////////////////////////////////////////////

static void BM_SPMC_OrderedQueueProducerConsumer(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;
    
    for (auto _ : state) {

      state.PauseTiming();

      OrderedMPMCQueue<EventType, 8192> queue;
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
BENCHMARK(BM_SPMC_OrderedQueueProducerConsumer)->UseRealTime();

static void BM_SPMC_ConcurrentQueueProducerConsumer(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;
    std::size_t num_producers = 1;
    std::size_t sz = cc_qsize(total_events, num_producers);
    

    for (auto _ : state) {

      state.PauseTiming();

      moodycamel::ConcurrentQueue<EventType> queue(sz);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
BENCHMARK(BM_SPMC_ConcurrentQueueProducerConsumer)->UseRealTime();

static void BM_SPMC_DisruptorQueueProducerConsumer(benchmark::State& state) {
    const int num_consumers = 4;
    const size_t total_events = 8000;

    auto config = DisruptorQueueConfig<EventType>{};
    config.put_waitStrategy(std::make_shared<Disruptor::YieldingWaitStrategy>());

    for (auto _ : state) {

      state.PauseTiming();

      DisruptorQueue<EventType, 8192> queue(config);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
BENCHMARK(BM_SPMC_DisruptorQueueProducerConsumer)->UseRealTime();

//////////////////////////////////////////////////
// MULTIPLE PRODUCER MULTIPLE CONSUMER (MPMC)
//////////////////////////////////////////////////

static void BM_MPMC_OrderedQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {

      state.PauseTiming();

      OrderedMPMCQueue<EventType, 8192> queue;
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
BENCHMARK(BM_MPMC_OrderedQueueProducerConsumer)->UseRealTime();

static void BM_MPMC_ConcurrentQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    std::size_t sz = cc_qsize(total_events, num_producers);    

    for (auto _ : state) {

      state.PauseTiming();

      moodycamel::ConcurrentQueue<EventType> queue(sz);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
BENCHMARK(BM_MPMC_ConcurrentQueueProducerConsumer)->UseRealTime();

static void BM_MPMC_DisruptorQueueProducerConsumer(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;

    auto config = DisruptorQueueConfig<EventType>{};
    config.put_waitStrategy(std::make_shared<Disruptor::YieldingWaitStrategy>());

    for (auto _ : state) {

      state.PauseTiming();

      DisruptorQueue<EventType, 8192> queue(config);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();

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
BENCHMARK(BM_MPMC_DisruptorQueueProducerConsumer)->UseRealTime();

//////////////////////////////////////////////////
// HIGH THROUGHPUT STRESS TEST
//////////////////////////////////////////////////

static void BM_HighThroughput_OrderedQueue(benchmark::State& state) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const size_t total_events = 8000;
    const size_t events_per_producer = total_events / num_producers;
    
    for (auto _ : state) {

      state.PauseTiming();

      OrderedMPMCQueue<EventType, 8192> queue;
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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
    std::size_t sz = cc_qsize(total_events, num_producers);

    for (auto _ : state) {

      state.PauseTiming();

      moodycamel::ConcurrentQueue<EventType> queue(sz);
      std::atomic<bool> done{false};
      std::atomic<size_t> consumed{0};

      state.ResumeTiming();
        
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

    auto config = DisruptorQueueConfig<EventType>{};
    config.put_waitStrategy(std::make_shared<Disruptor::YieldingWaitStrategy>());
    
    for (auto _ : state) {
      DisruptorQueue<EventType, 8192> queue(config);
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

    std::size_t num_its = state.max_iterations;
    std::size_t num_producers = 1;
    std::size_t sz = cc_qsize(num_its, num_producers);

    moodycamel::ConcurrentQueue<EventType> queue(sz);
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
