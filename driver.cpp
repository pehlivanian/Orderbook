#include "driver.hpp"


auto main(int argc, char **argv) -> int {

  constexpr int N = 5;
  const std::string input_file = "GOOG_2012-06-21_34200000_57600000_message_1.csv";

  using BidContainer = ForwardListAdaptor<std::less<order>, N>;
  using AskContainer = ForwardListAdaptor<std::greater<order>, N>;
  using SPMCq = moodycamel::ConcurrentQueue<eventLOBSTER>;

  auto orderbook = new OrderBook<BidContainer, AskContainer>{};
  std::shared_ptr<SPMCq> SPMCqueue_source = std::make_shared<SPMCq>();
  std::shared_ptr<SPMCq> SPMCqueue_target = std::make_shared<SPMCq>();

  auto publisher = new Publisher<eventLOBSTER>{input_file, SPMCqueue_source};
  auto consumer = new Consumer<eventLOBSTER>{SPMCqueue_source, SPMCqueue_target};

  publisher->publish();
  consumer->consume();


  return 0;
}
