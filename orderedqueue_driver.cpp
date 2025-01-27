#include "orderedqueue.hpp"
#include "utils.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <functional>
#include <syncstream>
#include <random>
#include <iterator>

#define sync_cout std::osyncstream(std::cout)

using namespace std::chrono_literals;

using namespace Message;

std::random_device dev{};
std::mt19937 gen{dev()};

struct eventLite {
  std::string payload_;
  unsigned long seqNumber_;

  eventLite() = default;
  eventLite(std::string p, unsigned long sn) :
    payload_{p},
    seqNumber_{sn}
  {}
    
};

std::ostream& operator<<(std::ostream& os, const eventLite& e) {
  os << "{ "
     << e.seqNumber_ << ", "
     << e.payload_ << "}";
  return os;
}

std::osyncstream& operator<<(std::osyncstream& oss, const eventLite& e) {
  oss << "{ "
      << e.seqNumber_ << ", "
      << e.payload_ << "}";
  return oss;
}


auto main(int argc, char **argv) -> int {

  using EventType = eventLite;

  auto q = std::make_unique<OrderedMPMCQueue<EventType, 100>>();

  constexpr int num_producers = 30;
  constexpr int num_consumers = 30;

  std::vector<std::size_t> seqNumbers(num_producers);
  std::iota(seqNumbers.begin(), seqNumbers.end(), 0);
  std::random_shuffle(seqNumbers.begin(), seqNumbers.end());
  std::cout << "Sequence number order\n";
  std::copy(seqNumbers.begin(), seqNumbers.end(), std::ostream_iterator<std::size_t>(std::cout, " "));
  std::cout << std::endl;

  std::vector<std::thread> producers, consumers;

  auto produce = [&q](std::string p, unsigned long s) {
    q->enqueue(EventType{p, s});
  };

  auto consume = [&q](std::optional<EventType>& e){
    e = q->try_dequeue();
    if (e.has_value()) {
      sync_cout << *e << std::endl;
    } else {
      sync_cout << "<NO VALUE>" << std::endl;
    }
  };

  /* 
  for(std::size_t i=0; i<num_producers; ++i) {
    std::string payload = "payload " + std::to_string(i);
    unsigned long sequenceNum = i;
    producers.push_back(std::thread(produce, payload, sequenceNum));
  }
    
  for(std::size_t i=0; i<num_producers; ++i) {
    producers[i].join();
  }
  */

  for (std::size_t i=0; i<num_producers; ++i) {
    unsigned long seqNumber = seqNumbers[i];
    std::string payload = "payload " + std::to_string(seqNumber);
    q->enqueue(EventType{payload, seqNumber});
  }

  
  /*
  for (std::size_t i=0; i<num_consumers; ++i) {
    std::optional<EventType> oe;
    consumers.push_back(std::thread(consume, std::ref(oe)));
  }

  for (std::size_t i=0; i<num_consumers; ++i) {
    consumers[i].join();
  }
  */

  for (std::size_t i=0; i<num_consumers; ++i) {
    std::optional<EventType> e = q->try_dequeue();
    if (e.has_value()) {
      sync_cout << *e << std::endl;
    } else {
      sync_cout << "<NO VALUE>" << std::endl;
    }
  }

  return 0;
  
}
