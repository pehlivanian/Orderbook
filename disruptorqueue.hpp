#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <vector>
#include <Disruptor/Disruptor.h>
#include <Disruptor/ThreadPerTaskScheduler.h>
#include <Disruptor/BusySpinWaitStrategy.h>
#include <Disruptor/WorkerPool.h>
#include <Disruptor/ITaskScheduler.h>
#include <Disruptor/IWaitStrategy.h>
#include <Disruptor/IgnoreExceptionHandler.h>
#include <Disruptor/BasicExecutor.h>

template<typename EventType>
struct DisruptorQueueConfig {

  explicit DisruptorQueueConfig() :
    m_taskScheduler_{std::make_shared<Disruptor::ThreadPerTaskScheduler>()},
    m_waitStrategy_{std::make_shared<Disruptor::BusySpinWaitStrategy>()}
  {}

  std::shared_ptr<Disruptor::ITaskScheduler> m_taskScheduler_;
  std::shared_ptr<Disruptor::IWaitStrategy> m_waitStrategy_;

  DisruptorQueueConfig put_taskScheduler(std::shared_ptr<Disruptor::ITaskScheduler> rhs) {
    this->m_taskScheduler_ = std::move(rhs);
    return *this;
  }

  DisruptorQueueConfig put_waitStrategy(std::shared_ptr<Disruptor::IWaitStrategy> rhs) {
    this->m_waitStrategy_ = std::move(rhs);
    return *this;
  }

};

template<typename EventType, std::size_t RingBufferSize = 1024>
class DisruptorQueue {

  int num_threads = std::max(std::thread::hardware_concurrency(), 2u);

public:
    explicit DisruptorQueue() 
        : m_taskScheduler(std::make_shared<Disruptor::ThreadPerTaskScheduler>())
        , m_waitStrategy(std::make_shared<Disruptor::BusySpinWaitStrategy>())
        , m_disruptor(std::make_shared<Disruptor::disruptor<EventType>>(
            []() { return EventType{}; },
            RingBufferSize,
            m_taskScheduler,
            Disruptor::ProducerType::Multi,
            m_waitStrategy))
        , m_ringBuffer(nullptr)
        , m_started(false)
        , m_consumersStarted(false)
    {
    }

  DisruptorQueue(const DisruptorQueueConfig<EventType>& rhs) :
    m_taskScheduler(std::move(rhs.m_taskScheduler_)),
    m_waitStrategy(std::move(rhs.m_waitStrategy_)),
    m_disruptor(std::make_shared<Disruptor::disruptor<EventType>>(
								  [](){ return EventType{}; },
								  RingBufferSize,
								  m_taskScheduler,
								  Disruptor::ProducerType::Multi,
								  m_waitStrategy)),
    m_ringBuffer(nullptr),
    m_started(false),
    m_consumersStarted(false)
  {}
		
		   
		    

    ~DisruptorQueue() {
        shutdown();
    }

    DisruptorQueue(const DisruptorQueue&) = delete;
    DisruptorQueue& operator=(const DisruptorQueue&) = delete;
    DisruptorQueue(DisruptorQueue&&) = delete;
    DisruptorQueue& operator=(DisruptorQueue&&) = delete;

    void start() {
        if (!m_started) {
            if (!m_workHandlers.empty()) {
                std::vector<std::shared_ptr<Disruptor::IWorkHandler<EventType>>> handlers;
                for (const auto& handler : m_workHandlers) {
                    handlers.push_back(handler);
                }
                m_disruptor->handleEventsWithWorkerPool(handlers);
                m_consumersStarted = true;
            }
            
            m_taskScheduler->start(num_threads);
            m_ringBuffer = m_disruptor->start();
            m_started = true;
        }
    }

    void shutdown() {
        if (m_started) {
            m_disruptor->shutdown();
            m_taskScheduler->stop();
            m_started = false;
        }
    }

    bool try_enqueue(const EventType& item) {
        try {
            if (!m_started || !m_ringBuffer) {
                return false;
            }
            
            auto sequence = m_ringBuffer->tryNext();
            if (sequence == -1) {
                return false;
            }
            
            auto& event = (*m_ringBuffer)[sequence];
            event = item;
            m_ringBuffer->publish(sequence);
            return true;
        } catch (...) {
            return false;
        }
    }

    void enqueue(const EventType& item) {
        if (!m_started || !m_ringBuffer) {
            throw std::runtime_error("DisruptorQueue not started");
        }
        
        auto sequence = m_ringBuffer->next();
        auto& event = (*m_ringBuffer)[sequence];
        event = item;
        m_ringBuffer->publish(sequence);
    }

    bool try_dequeue(EventType& /*item*/) {
        return false;
    }

    EventType dequeue() {
        throw std::runtime_error("dequeue() not supported in disruptor pattern - use consumer handlers instead");
    }

    template<typename Handler>
    void addConsumer(Handler&& handler) {
        if (m_started) {
            throw std::runtime_error("Cannot add consumers after queue has started");
        }
        
        auto workHandler = std::make_shared<WorkHandlerWrapper<Handler>>(std::forward<Handler>(handler));
        m_workHandlers.push_back(workHandler);
    }

private:
    template<typename Handler>
    class WorkHandlerWrapper : public Disruptor::IWorkHandler<EventType> {
    public:
        explicit WorkHandlerWrapper(Handler handler) : m_handler(std::move(handler)) {}
        
        void onEvent(EventType& event) override {
            m_handler(event);
        }
        
    private:
        Handler m_handler;
    };

    std::shared_ptr<Disruptor::ITaskScheduler> m_taskScheduler;
    std::shared_ptr<Disruptor::IWaitStrategy> m_waitStrategy;
    std::shared_ptr<Disruptor::disruptor<EventType>> m_disruptor;
    std::shared_ptr<Disruptor::RingBuffer<EventType>> m_ringBuffer;
    std::vector<std::shared_ptr<Disruptor::IWorkHandler<EventType>>> m_workHandlers;
    std::atomic<bool> m_started;
    std::atomic<bool> m_consumersStarted;
};
