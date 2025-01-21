#ifndef __CONSUMER_HPP__
#define __CONSUMER_HPP__

#include "eventstream.hpp"
#include "concurrentqueue.h"
#include "utils.hpp"

using namespace Utils;
using namespace Message;

#include <memory>
#include <syncstream>
#include <thread>


#define sync_cout std::osyncstream(std::cout)

template<typename EventType>
class Consumer {

  using SPMCq = moodycamel::ConcurrentQueue<EventType>;

  const int num_workers = std::thread::hardware_concurrency() - 1;

  struct worker {
    
    Consumer<EventType>* outer_;

    worker(Consumer<EventType>* outer, int id) : 
      outer_{outer},
      id_{id}
    {}

    void operator()() {
      eventLOBSTER e;
      while (true) {
	bool found = outer_->SPMCqueue_source_->try_dequeue(e);
	if (found) {
	  std::cout << "ID: " << id_ << " : " << e << std::endl;
	  order o = eventLOBSTERToOrder(e);
	}
      }
    }

    int id_;
    
  };

public:
  Consumer(std::shared_ptr<SPMCq> q_source, std::shared_ptr<SPMCq> q_target) :
    SPMCqueue_source_{q_source},
    SPMCqueue_target_{q_target}
  {}
					
  void consume() {

    // workers
    std::vector<worker> workers;
    for (std::size_t i=0; i<num_workers; ++i) {
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

    for (std::size_t i=0; i<threads.size(); ++i) {
      threads[i].join();
    }
  }


private:
  std::shared_ptr<SPMCq> SPMCqueue_source_;
  std::shared_ptr<SPMCq> SPMCqueue_target_;
};

#endif
