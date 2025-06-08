#include "orderedqueue.hpp"
#include "utils.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <syncstream>
#include <thread>

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

using namespace Message;

enum class runtime { async, sync };

std::random_device dev{};
std::mt19937 gen{dev()};

struct eventLite {
  std::string payload_;
  unsigned long seqNum_;

  eventLite() = default;
  eventLite(std::string p, unsigned long sn) : payload_{p}, seqNum_{sn} {}
};

std::ostream& operator<<(std::ostream& os, const eventLite& e) {
  os << "{ " << e.seqNum_ << ", " << e.payload_ << "}";
  return os;
}

std::osyncstream& operator<<(std::osyncstream& oss, const eventLite& e) {
  oss << "{ " << e.seqNum_ << ", " << e.payload_ << "}";
  return oss;
}

void publish(eventLite& e) { sync_cout << e << std::endl; }

auto main(int argc, char** argv) -> int {

  constexpr runtime mode = runtime::async;

  using EventType = eventLite;

  auto q = std::make_unique<OrderedMPMCQueue<EventType, 100>>();

  constexpr int num_producers = 4;
  constexpr int num_consumers = 20;
  constexpr int num_messages_per_producer = 10;

  std::atomic<bool> running{true};

  std::vector<std::size_t> seqNumbers(num_producers);
  std::iota(seqNumbers.begin(), seqNumbers.end(), 0);
  std::random_shuffle(seqNumbers.begin(), seqNumbers.end());
  std::cout << "Sequence number order\n";
  std::copy(seqNumbers.begin(), seqNumbers.end(),
            std::ostream_iterator<std::size_t>(std::cout, " "));
  std::cout << std::endl;

  std::vector<std::thread> producers, consumers;

  auto produce = [&q, &running](std::string p, unsigned long s1, unsigned long s2) {
    for (unsigned long i=s1; i<s2; ++i) {
      if (running) {
	q->enqueue(EventType{p, i});
      }
    }
    sync_cout << "Producer [" << s1 << ", " << s2 << "] finished." << std::endl;
  };

  if (mode == runtime::async) {

    std::thread timer([&running](){
			std::this_thread::sleep_for(std::chrono::seconds(2)); 
			running = false;
		      });
    timer.detach();

    auto consume = [&q, &running](EventType& e) {
      while (true && running) {
	auto found = q->try_dequeue(true);
	if (found.has_value()) {
	  publish(*found);
	  q->mark_processed(found->seqNum_);
	}
	/*
	bool found = q->try_dequeue(e, true);
	if (found) {
	  publish(e);
	  q->mark_processed(e.seqNum_);
	}
	*/
      }
    };
    
    for(std::size_t i=0; i<num_producers; ++i) {
      std::string payload = "payload " + std::to_string(i);
      producers.push_back(std::thread(produce, 
				      payload, 
				      (i) * num_messages_per_producer,
				      (i+1) * num_messages_per_producer));
    }

    for(std::size_t i=0; i<num_producers; ++i) {
      producers[i].join();
    }

    for (std::size_t i=0; i<num_consumers; ++i) {
      EventType e;
      consumers.push_back(std::thread(consume, std::ref(e)));
    }

    for (std::size_t i=0; i<num_consumers; ++i) {
      consumers[i].join();
    }


  } else {
    for (std::size_t i = 0; i < num_producers; ++i) {
      unsigned long seqNumber = seqNumbers[i];
      std::string payload = "payload " + std::to_string(seqNumber);
      q->enqueue(EventType{payload, seqNumber});
    }
    
    for (std::size_t i = 0; i < num_consumers; ++i) {
      EventType e;
      
      bool found = q->try_dequeue(e, true);
      if (found) {
	publish(e);
	q->mark_processed(e.seqNum_);
      } else {
	sync_cout << "<NO VALUE>" << std::endl;
      }
    }
  }


  return 0;
}
