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

auto main(int argc, char** argv) -> int {
  using EventType = eventLite;

  auto q = std::make_unique<OrderedMPMCQueue<EventType, 100>>();

  constexpr int num_producers = 30;
  constexpr int num_consumers = 30;

  std::vector<std::size_t> seqNumbers(num_producers);
  std::iota(seqNumbers.begin(), seqNumbers.end(), 0);
  std::random_shuffle(seqNumbers.begin(), seqNumbers.end());
  std::cout << "Sequence number order\n";
  std::copy(seqNumbers.begin(), seqNumbers.end(),
            std::ostream_iterator<std::size_t>(std::cout, " "));
  std::cout << std::endl;

  std::vector<std::thread> producers, consumers;

  auto produce = [&q](std::string p, unsigned long s) { q->enqueue(EventType{p, s}); };

  auto consume = [&q](EventType& e) {
    bool found = q->try_dequeue(e);
    if (found) {
      sync_cout << e << std::endl;
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

  for (std::size_t i = 0; i < num_producers; ++i) {
    unsigned long seqNumber = seqNumbers[i];
    std::string payload = "payload " + std::to_string(seqNumber);
    q->enqueue(EventType{payload, seqNumber});
  }

  /*
  for (std::size_t i=0; i<num_consumers; ++i) {
    EventType e;
    consumers.push_back(std::thread(consume, std::ref(e)));
  }

  for (std::size_t i=0; i<num_consumers; ++i) {
    consumers[i].join();
  }
  */

  for (std::size_t i = 0; i < num_consumers; ++i) {
    EventType e;

    bool found = q->try_dequeue(e);

    if (found) {
      sync_cout << e << std::endl;
    } else {
      sync_cout << "<NO VALUE>" << std::endl;
    }
  }

  return 0;
}
