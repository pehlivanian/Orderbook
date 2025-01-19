#include "driver.hpp"


auto main(int argc, char **argv) -> int {

  constexpr int N = 5;
  using BidContainer = ForwardListAdaptor<std::less<order>, N>;
  using AskContainer = ForwardListAdaptor<std::greater<order>, N>;

  auto orderbook = new OrderBook<BidContainer, AskContainer>{};

  const std::string path = "GOOG_2012-06-21_34200000_57600000_message_1.csv";

  /*
  const char* buf = read_mmap(path.c_str());
  
  eventiterator eit = eventiterator(buf);
  
  auto ev = *eit;
  std::cout << ev << std::endl;
  
  eit++;
  ev = *eit;
  std::cout << ev << std::endl;

  eit++;
  ev = *eit;
  std::cout << ev << std::endl;
  */

  EventStream es = EventStream(path.c_str());

  auto esit = es.begin();

  std::cout << *esit << std::endl;

  return 0;
}
