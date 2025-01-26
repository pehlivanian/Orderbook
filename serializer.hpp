#ifndef __SERIALIZER_HPP__
#define __SERIALIZER_HPP__

#include <iostream>
#include <syncstream>
#include <thread>

#define sync_cout std::osyncstream(std::cout)

template<typename OrderType>
class Serializer {
public:

  using ActivatedMPMCq = std::shared_ptr<ActivatedQueue<OrderType>>;
  // const int num_workers = std::thread::hardware_concurrency() - 1;
  const int num_workers = 4;

  struct worker {
    
    Serializer<OrderType>* outer_;

    worker(Serializer<OrderType>* outer, int id) :
      outer_{outer},
      id_{-1}
    {}

    void operator()() {
      while (true) {
	OrderType o;
	// sync_cout << "ATTEMPTING TO DEQUE FROM QUEUE 2" << std::endl;
	bool found = outer_->Serializer_q_->try_dequeue(o);
	if (found) {
	  // std::cout << "ID: " << id_ << " : " << o << std::endl;
	  sync_cout << "QUEUE 2: DEQUEUED: " << o.seqNum_ << std::endl;
	}
	else {
	  // sync_cout << "ATTEMPT TO DEQUE FROM QUEUE 2 FAILED" << std::endl;
	}
      }
    }

    int id_;

  };

  Serializer(ActivatedMPMCq& q) :
    Serializer_q_{q}
  {}

  void serialize() {
    
    // workers
    std::vector<worker> workers;
    for(std::size_t i=0; i<num_workers; ++i) {
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
      OrderType o;
      // sync_cout << "ATTEMPTING TO DEQUEUE FROM QUEUE 2" << std::endl;
      bool found = Serializer_q_->try_dequeue(o);
      if (found) {
	// sync_cout << "QUEUE 2: DEQUEUED: " << o.seqNum_ << std::endl;
	count++;
      } else {
	// sync_cout << "ATTEMPT TO DEQUE FROM QUEUE 2 FAILED" << std::endl;
      }
    }
  }

private:
  std::shared_ptr<ActivatedQueue<OrderType>> Serializer_q_;
};

#endif
