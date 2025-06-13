#include "driver.hpp"

#define UNUSED(a) \
  do {            \
    (void)(a);    \
  } while (0);
#define sync_cout std::osyncstream(std::cout)

enum class runtime { async, sync };

// New callback that just processes the order without promise
void process_order(Message::order&& o) {
  UNUSED(o);
  std::this_thread::sleep_for(10ms);
  sync_cout << "Processed order: " << o.seqNum_ << std::endl;
}

// Staging
// 
// SPMCQueue [NUM_CONSUMERS] --> OrderedMPMCQueue [NUM_SERIALIZERS] --> terminus
//

auto main(int argc, char** argv) -> int {
  UNUSED(argc);
  UNUSED(argv);

  constexpr runtime mode = runtime::async;

  // Number of workers moving from SPMCQueue -> OrderedMPMCQueue
  constexpr std::size_t NUM_CONSUMERS = 24;

  // Number of workers moving off from OrderedMPMCQueue -> terminus
  constexpr std::size_t NUM_SERIALIZERS = 4;

  // Delay between reading from 1st SPMC queue to enqueing in ordered queue
  constexpr auto FIRST_HOP_DELAY = 10ms;

  // const std::string input_file = "GOOG_2012-06-21_34200000_57600000_message_1.csv";
  const std::string input_file = "GOOG_message_short.csv";

  using EventType = eventLOBSTER;
  using OrderType = order;
  using SPMCq = moodycamel::ConcurrentQueue<EventType>;
  using OrderedMPMCq = OrderedMPMCQueue<OrderType>;

  std::shared_ptr<SPMCq> q_source = std::make_shared<SPMCq>();
  std::shared_ptr<OrderedMPMCQueue<OrderType>> q_target =
      std::make_shared<OrderedMPMCQueue<OrderType>>();

  auto publisher = std::make_unique<Publisher<EventType>>(input_file, q_source);
  auto consumer = std::make_unique<Consumer<EventType, OrderType>>(q_source, q_target,
                                                                   NUM_CONSUMERS, FIRST_HOP_DELAY);
  auto serializer =
      std::make_unique<Serializer<OrderType>>(q_target, process_order, NUM_SERIALIZERS);

  if (mode == runtime::async) {
    std::thread pub_thread{&Publisher<EventType>::publish, publisher.get()};
    std::thread con_thread{&Consumer<EventType, OrderType>::consume, consumer.get()};
    std::thread ser_thread{&Serializer<OrderType>::serialize, serializer.get()};

    pub_thread.join();
    con_thread.join();
    ser_thread.join();

  } else {
    constexpr std::size_t num = 100;
    publisher->publish_some(num);
    consumer->consume_some(num);
    serializer->serialize_some(num);
  }

  return 0;
}
