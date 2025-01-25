#include "driver.hpp"


auto main(int argc, char **argv) -> int {

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

  auto publisher = new Publisher<EventType>{input_file, q_source};
  auto consumer = new Consumer<EventType, OrderType>{q_source, q_target};

  publisher->publish();
  consumer->consume();

  return 0;
}
