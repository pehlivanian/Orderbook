#ifndef __SERIALIZER_HPP__
#define __SERIALIZER_HPP__

#include "orderedqueue.hpp"

#include <iostream>
#include <syncstream>
#include <functional>
#include <thread>
#include <queue>

#define sync_cout std::osyncstream(std::cout)

template<typename EventType>
class Serializer {
public:

  using FnType = std::function<void(std::promise<void>&, EventType&&)>;
  
  struct wrapped_callback {
    void operator()(FnType& func,
		    std::promise<void>& p,
		    EventType&& o) {
      func(p, std::move(o));
    }
  };

  struct worker {
    
    Serializer<EventType>* outer_;

    worker(Serializer<EventType>* outer, int id) :
      outer_{outer},
      id_{-1} // thread id
    {}

    void operator()() {
      while (true) {
	// sync_cout << "ATTEMPTING TO DEQUE FROM QUEUE 2" << std::endl;
	auto [o,p] = outer_->Serializer_q_->try_dequeue_p();
	
	if (o.has_value() and p.has_value()) {
	  wrapped_callback callback;
	  callback(outer_->callback_, *p, std::move(*o));
	}
	else {
	  // std::cerr << "DEQUEUE FAILED - QUEUE MAY BE EMPTY" << std::endl;
	  ;
	}
      }
    }

    int id_;

  };

  Serializer(std::shared_ptr<OrderedMPMCQueue<EventType>> q,
	     FnType&& callback) :
    Serializer_q_{q},
    callback_{FnType{callback}},
    num_workers_{1}
  {}

  Serializer(std::shared_ptr<OrderedMPMCQueue<EventType>> q, 
	     FnType&& callback,
	     std::size_t num_workers) :
    Serializer_q_{q},
    callback_{FnType{callback}},
    num_workers_{num_workers}
  {}

  void serialize() {
    
    // workers
    std::vector<worker> workers;
    for(std::size_t i=0; i<num_workers_; ++i) {
      workers.emplace_back(this, i);
    }

    // tasks
    std::vector<std::function<void(void)>> tasks;
    for (std::size_t i=0; i<workers.size(); ++i) {
      tasks.emplace_back(workers[i]);
    }

    // threads
    std::vector<std::thread> threads;
    for (std::size_t i=0; i<workers.size(); ++i) {
      threads.emplace_back(tasks[i]);
    }

    for (auto& thread : threads)
      thread.join();

  }

  void serialize_some(std::size_t num) {
    std::size_t count = 0;

    while (count < num) {
      EventType o;
      // sync_cout << "ATTEMPTING TO DEQUEUE FROM QUEUE 2" << std::endl;
      bool found = Serializer_q_->try_dequeue(o);
      if (found) {
	sync_cout << "QUEUE 2: DEQUEUED: " << o.seqNum_ << std::endl;
	count++;
      } else {
	sync_cout << "ATTEMPT TO DEQUE FROM QUEUE 2 FAILED" << std::endl;
      }
    }
  }

private:
  std::shared_ptr<OrderedMPMCQueue<EventType>> Serializer_q_;
  std::function<void(std::promise<void>&, EventType&&)> callback_;
  std::size_t num_workers_;
};

#endif
