#include "driver.hpp"

enum class runtime {
  async,
  sync
    };

auto main(int argc, char **argv) -> int {

  constexpr runtime mode = runtime::async;

  constexpr int N = 5;
  const std::string input_file = "GOOG_2012-06-21_34200000_57600000_message_1.csv";

  using BidContainer = ForwardListAdaptor<std::less<order>, N>;
  using AskContainer = ForwardListAdaptor<std::greater<order>, N>;
  auto orderbook = new OrderBook<BidContainer, AskContainer>{};

  using EventType = eventLOBSTER;
  using OrderType = order;
  using SPMCq = moodycamel::ConcurrentQueue<EventType>;
  using ActivatedMPMCq = ActivatedQueue<OrderType>;

  std::shared_ptr<SPMCq> q_source = std::make_shared<SPMCq>();
  std::shared_ptr<ActivatedMPMCq> q_target = std::make_shared<ActivatedMPMCq>();

  auto publisher = std::make_unique<Publisher<EventType>>(input_file, q_source);
  auto consumer = std::make_unique<Consumer<EventType, OrderType>>(q_source, q_target);
  auto serializer = std::make_unique<Serializer<OrderType>>(q_target);

  if (mode == runtime::async) {
    std::thread pub_thread{&Publisher<EventType>::publish, publisher.get()};
    std::thread con_thread{&Consumer<EventType, OrderType>::consume, consumer.get()};
    std::thread ser_thread{&Serializer<OrderType>::serialize, serializer.get()};
    
    pub_thread.join();
    con_thread.join();
    ser_thread.join();
  } else {
    
    constexpr std::size_t num = 4;

    publisher->publish_some(num);
    consumer->consume_some(num);
    serializer->serialize_some(num);
  }
  
  

  return 0;
}
