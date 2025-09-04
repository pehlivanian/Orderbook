#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include "orderedqueue.hpp"
#include "utils.hpp"

using namespace std::chrono;

// High-performance throughput test
struct Event {
    size_t seqNum_;
    uint64_t data;
    Event(size_t seq = 0, uint64_t d = 0) : seqNum_(seq), data(d) {}
};

template<typename Queue>
class HighThroughputBench {
public:

    static void run_test(const std::string& name, int num_producers, int num_consumers, 
                        size_t events_per_producer, size_t duration_seconds = 5) {
        
        auto queue = std::make_unique<Queue>();
        
        std::atomic<size_t> total_enqueued{0};
        std::atomic<size_t> total_dequeued{0};
        std::atomic<bool> stop_flag{false};
        
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        // Start consumers first
        for (int i = 0; i < num_consumers; ++i) {
            consumers.emplace_back([&queue, &total_dequeued, &stop_flag]() {
                size_t local_dequeued = 0;
                Event event;
                
                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto opt_event = queue->try_dequeue();
                    if (opt_event.has_value()) {
                        event = *opt_event;
                        local_dequeued++;
                        if ((local_dequeued & 0xFFFF) == 0) { // Update every 64k events
                            total_dequeued.fetch_add(0x10000, std::memory_order_relaxed);
                        }
                    } else {
                        // Brief pause to avoid hammering
                        std::this_thread::yield();
                    }
                }
                
                // Final update
                total_dequeued.fetch_add(local_dequeued & 0xFFFF, std::memory_order_relaxed);
            });
        }
        
        // Start producers
        std::atomic<size_t> seq_counter{0};
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&queue, &total_enqueued, &seq_counter, &stop_flag, events_per_producer]() {
                size_t local_enqueued = 0;
                
                while (!stop_flag.load(std::memory_order_relaxed) && local_enqueued < events_per_producer) {
                    size_t seq = seq_counter.fetch_add(1, std::memory_order_relaxed);
                    Event event(seq, seq * 42);
                    
                    if (queue->try_enqueue(std::move(event))) {
                        local_enqueued++;
                        if ((local_enqueued & 0xFFFF) == 0) { // Update every 64k events
                            total_enqueued.fetch_add(0x10000, std::memory_order_relaxed);
                        }
                    } else {
                        // Brief pause on queue full
                        std::this_thread::yield();
                    }
                }
                
                // Final update
                total_enqueued.fetch_add(local_enqueued & 0xFFFF, std::memory_order_relaxed);
            });
        }
        
        auto start_time = high_resolution_clock::now();
        
        // Let it run for specified duration
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        
        // Signal stop
        stop_flag.store(true, std::memory_order_relaxed);
        
        // Wait for producers to finish
        for (auto& t : producers) {
            t.join();
        }
        
        // Let consumers drain the queue
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Stop consumers
        for (auto& t : consumers) {
            t.join();
        }
        
        auto end_time = high_resolution_clock::now();
        auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
        
        size_t final_enqueued = total_enqueued.load();
        size_t final_dequeued = total_dequeued.load();
        
        double enqueue_throughput = (final_enqueued * 1000.0) / duration_ms;
        double dequeue_throughput = (final_dequeued * 1000.0) / duration_ms;
        
        std::cout << "\n=== " << name << " Performance Results ===\n";
        std::cout << "Producers: " << num_producers << ", Consumers: " << num_consumers << "\n";
        std::cout << "Duration: " << duration_ms << " ms\n";
        std::cout << "Total Enqueued: " << final_enqueued << "\n";
        std::cout << "Total Dequeued: " << final_dequeued << "\n";
        std::cout << "Enqueue Throughput: " << static_cast<size_t>(enqueue_throughput) << " ops/sec\n";
        std::cout << "Dequeue Throughput: " << static_cast<size_t>(dequeue_throughput) << " ops/sec\n";
        std::cout << "Combined Throughput: " << static_cast<size_t>((enqueue_throughput + dequeue_throughput) / 2) << " ops/sec\n";
        std::cout << "=======================================\n\n";
    }
};

int main() {
    std::cout << "High-Throughput MPMC Queue Benchmark\n";
    std::cout << "====================================\n";
    
    using Queue = OrderedMPMCQueue<Event, 1024 * 1024>;
    using Bench = HighThroughputBench<Queue>;
    
    // Test different producer/consumer configurations
    std::cout << "\n🚀 EXTREME THROUGHPUT TEST - 32 CORES 🚀\n";
    
    // Light load
    Bench::run_test("OrderedQueue (2P/2C)", 2, 2, 10000000, 3);
    
    // Medium load  
    Bench::run_test("OrderedQueue (4P/4C)", 4, 4, 10000000, 3);
    
    // Heavy load
    Bench::run_test("OrderedQueue (8P/8C)", 8, 8, 10000000, 3);
    
    // Maximum load
    Bench::run_test("OrderedQueue (16P/16C)", 16, 16, 10000000, 3);
    
    return 0;
}