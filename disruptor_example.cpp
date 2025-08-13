#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <memory>
#include <random>
#include <atomic>

#include "Disruptor/Disruptor.h"
#include "Disruptor/ThreadPerTaskScheduler.h"
#include "Disruptor/BusySpinWaitStrategy.h"
#include "Disruptor/WorkerPool.h"
#include "Disruptor/IWorkHandler.h"
#include "Disruptor/IgnoreExceptionHandler.h"
#include "Disruptor/BasicExecutor.h"

namespace Example
{
    struct MessageEvent
    {
        std::int64_t id = 0;
        std::int32_t value = 0;
        std::int32_t producerId = 0;
        
        static const std::function<MessageEvent()>& eventFactory()
        {
            static std::function<MessageEvent()> result([]() { return MessageEvent{}; });
            return result;
        }
    };

    class Producer
    {
    public:
        Producer(int id, std::shared_ptr<Disruptor::RingBuffer<MessageEvent>> ringBuffer, int messageCount)
            : m_id(id), m_ringBuffer(ringBuffer), m_messageCount(messageCount)
        {
        }

        void run()
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 1000);

            for (int i = 0; i < m_messageCount; ++i)
            {
                auto sequence = m_ringBuffer->next();
                auto& event = (*m_ringBuffer)[sequence];
                
                event.id = i;
                event.value = dis(gen);
                event.producerId = m_id;
                
                m_ringBuffer->publish(sequence);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            std::cout << "Producer " << m_id << " finished producing " << m_messageCount << " messages\n";
        }

    private:
        int m_id;
        std::shared_ptr<Disruptor::RingBuffer<MessageEvent>> m_ringBuffer;
        int m_messageCount;
    };

    class Consumer : public Disruptor::IWorkHandler<MessageEvent>
    {
    public:
        Consumer(int id) : m_id(id), m_processedCount(0) {}

        void onEvent(MessageEvent& event) override
        {
            m_processedCount++;
            
            std::cout << "Consumer " << m_id 
                      << " processed event: id=" << event.id 
                      << ", value=" << event.value 
                      << ", from producer=" << event.producerId << "\n";
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        int getProcessedCount() const { return m_processedCount; }

    private:
        int m_id;
        std::atomic<int> m_processedCount;
    };
}

#include "disruptorqueue.hpp"

namespace DQ {

struct Message {
    int id;
    int value;
    int producerId;
};

void simpleExample() {
    std::cout << "\n=== Simple Single Producer/Consumer Example ===\n";
    
    DisruptorQueue<Message, 256> queue;
    
    queue.addConsumer([](Message& msg) {
        std::cout << "Consumer processed: id=" << msg.id 
                  << ", value=" << msg.value 
                  << ", producer=" << msg.producerId << "\n";
    });
    
    queue.start();
    
    for (int i = 0; i < 10; ++i) {
        Message msg{i, i * 10, 1};
        queue.enqueue(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    queue.shutdown();
}

void multiProducerConsumerExample() {
    std::cout << "\n=== Multi-Producer/Consumer Example ===\n";
    
    DisruptorQueue<Message, 1024> queue;
    
    std::atomic<int> processedCount{0};
    
    queue.addConsumer([&processedCount](Message& msg) {
        processedCount++;
        std::cout << "Consumer 1 processed: id=" << msg.id 
                  << ", value=" << msg.value 
                  << ", producer=" << msg.producerId << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    
    queue.addConsumer([&processedCount](Message& msg) {
        processedCount++;
        std::cout << "Consumer 2 processed: id=" << msg.id 
                  << ", value=" << msg.value 
                  << ", producer=" << msg.producerId << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    });
    
    queue.start();
    
    const int NUM_PRODUCERS = 3;
    const int MESSAGES_PER_PRODUCER = 5;
    
    std::vector<std::thread> producers;
    
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&queue, p, MESSAGES_PER_PRODUCER]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 100);
            
            for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i) {
                Message msg{i, dis(gen), p + 1};
                queue.enqueue(msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::cout << "Producer " << (p + 1) << " finished\n";
        });
    }
    
    for (auto& producer : producers) {
        producer.join();
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::cout << "Total messages processed: " << processedCount.load() << "\n";
    std::cout << "Expected: " << (NUM_PRODUCERS * MESSAGES_PER_PRODUCER) << "\n";
    
    queue.shutdown();
}

void tryEnqueueExample() {
    std::cout << "\n=== Try Enqueue Example ===\n";
    
    DisruptorQueue<Message, 8> queue;
    
    queue.addConsumer([](Message& msg) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Slow consumer processed: id=" << msg.id << "\n";
    });
    
    queue.start();
    
    for (int i = 0; i < 20; ++i) {
        Message msg{i, i * 5, 1};
        if (queue.try_enqueue(msg)) {
            std::cout << "Enqueued message " << i << "\n";
        } else {
            std::cout << "Failed to enqueue message " << i << " (queue full)\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    queue.shutdown();
}

}

void runDisruptorQueueExamples() {
    DQ::simpleExample();
    DQ::multiProducerConsumerExample();
    DQ::tryEnqueueExample();
}

int main()
{
    std::cout << "Choose example to run:\n";
    std::cout << "1. Original Disruptor Example\n";
    std::cout << "2. DisruptorQueue Examples\n";
    std::cout << "Enter choice (1 or 2): ";
    
    int choice;
    std::cin >> choice;
    
    if (choice == 2) {
        runDisruptorQueueExamples();
        return 0;
    }
    
    const int RING_BUFFER_SIZE = 1024;
    const int NUM_PRODUCERS = 3;
    const int NUM_CONSUMERS = 2;
    const int MESSAGES_PER_PRODUCER = 20;

    std::cout << "Starting Disruptor Producer-Consumer Example\n";
    std::cout << "Ring buffer size: " << RING_BUFFER_SIZE << "\n";
    std::cout << "Number of producers: " << NUM_PRODUCERS << "\n";
    std::cout << "Number of consumers: " << NUM_CONSUMERS << "\n";
    std::cout << "Messages per producer: " << MESSAGES_PER_PRODUCER << "\n\n";

    auto taskScheduler = std::make_shared<Disruptor::ThreadPerTaskScheduler>();
    auto waitStrategy = std::make_shared<Disruptor::BusySpinWaitStrategy>();
    
    auto disruptor = std::make_shared<Disruptor::disruptor<Example::MessageEvent>>(
        Example::MessageEvent::eventFactory(),
        RING_BUFFER_SIZE,
        taskScheduler,
        Disruptor::ProducerType::Multi,
        waitStrategy
    );

    std::vector<std::shared_ptr<Example::Consumer>> consumers;
    for (int i = 0; i < NUM_CONSUMERS; ++i)
    {
        consumers.push_back(std::make_shared<Example::Consumer>(i + 1));
    }

    // Convert to work handlers
    std::vector<std::shared_ptr<Disruptor::IWorkHandler<Example::MessageEvent>>> workHandlers;
    for (const auto& consumer : consumers)
    {
        workHandlers.push_back(std::static_pointer_cast<Disruptor::IWorkHandler<Example::MessageEvent>>(consumer));
    }

    disruptor->handleEventsWithWorkerPool(workHandlers);

    taskScheduler->start();
    auto ringBuffer = disruptor->start();

    std::vector<std::thread> producerThreads;
    std::vector<Example::Producer> producers;
    
    for (int i = 0; i < NUM_PRODUCERS; ++i)
    {
        producers.emplace_back(i + 1, ringBuffer, MESSAGES_PER_PRODUCER);
    }

    for (int i = 0; i < NUM_PRODUCERS; ++i)
    {
        producerThreads.emplace_back([&producers, i]() {
            producers[i].run();
        });
    }

    for (auto& thread : producerThreads)
    {
        thread.join();
    }

    // Wait a bit longer to ensure all events are processed
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Shutdown the disruptor before checking results
    disruptor->shutdown();
    taskScheduler->stop();

    int totalProcessed = 0;
    for (auto& consumer : consumers)
    {
        int processed = consumer->getProcessedCount();
        std::cout << "Consumer " << (std::distance(consumers.begin(), 
                                   std::find(consumers.begin(), consumers.end(), consumer)) + 1)
                  << " processed " << processed << " events\n";
        totalProcessed += processed;
    }

    int expectedTotal = NUM_PRODUCERS * MESSAGES_PER_PRODUCER;
    std::cout << "\nTotal events produced: " << expectedTotal << "\n";
    std::cout << "Total events processed: " << totalProcessed << "\n";
    
    if (totalProcessed == expectedTotal)
    {
        std::cout << "SUCCESS: All events were processed!\n";
    }
    else
    {
        std::cout << "WARNING: Event count mismatch!\n";
    }

    return 0;
}
