#ifndef __SERIALIZER_HPP__
#define __SERIALIZER_HPP__

#include "orderedqueue.hpp"

#include <functional>
#include <iostream>
#include <queue>
#include <syncstream>
#include <thread>

#define sync_cout std::osyncstream(std::cout)

template <typename EventType>
class Serializer {
 public:
  using FnType = std::function<void(EventType&&)>;

  struct wrapped_callback {
    void operator()(FnType& func, EventType&& o) { func(std::move(o)); }
  };

  struct worker {
    Serializer<EventType>* outer_;

    worker(Serializer<EventType>* outer, int id) : outer_{outer}, id_{id} {}

    void operator()() {
      while (true) {
        EventType event;
        if (outer_->Serializer_q_->try_dequeue(event)) {
          // Process the event
          wrapped_callback callback;
          callback(outer_->callback_, std::move(event));

          // Mark this event as processed so next events can be handled
          outer_->Serializer_q_->mark_processed(event.seqNum_);

          if (event.seqNum_ == 999)
            break;
        }
      }

      // outer_->Serializer_q_->replay();
      exit(0);
    }

    int id_;
  };

  Serializer(std::shared_ptr<OrderedMPMCQueue<EventType>> q, FnType&& callback)
      : Serializer_q_{q}, callback_{FnType{callback}}, num_workers_{1} {}

  Serializer(std::shared_ptr<OrderedMPMCQueue<EventType>> q, FnType&& callback,
             std::size_t num_workers)
      : Serializer_q_{q}, callback_{FnType{callback}}, num_workers_{num_workers} {}

  void serialize() {
    // workers
    std::vector<worker> workers;
    for (std::size_t i = 0; i < num_workers_; ++i) {
      workers.emplace_back(this, i);
    }

    // tasks
    std::vector<std::function<void(void)>> tasks;
    for (std::size_t i = 0; i < workers.size(); ++i) {
      tasks.emplace_back(workers[i]);
    }

    // threads
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < workers.size(); ++i) {
      threads.emplace_back(tasks[i]);
    }

    for (auto& thread : threads)
      thread.join();
  }

  void serialize_some(std::size_t num) {
    std::size_t count = 0;
    while (count < num) {
      EventType event;
      if (Serializer_q_->try_dequeue(event)) {
        sync_cout << "QUEUE 2: DEQUEUED: " << event.seqNum_ << std::endl;
        count++;
      } else {
        sync_cout << "ATTEMPT TO DEQUE FROM QUEUE 2 FAILED" << std::endl;
      }
    }
  }

 private:
  std::shared_ptr<OrderedMPMCQueue<EventType>> Serializer_q_;
  FnType callback_;
  std::size_t num_workers_;
};

#endif
