#include "driver.hpp"


auto main(int argc, char **argv) -> int {

  constexpr int N = 5;

  using BidContainer = ForwardListAdaptor<std::less<order>, N>;
  using AskContainer = ForwardListAdaptor<std::greater<order>, N>;

  auto orderbook = new OrderBook<BidContainer, AskContainer>{};
  orderbook->replay();
  

  return 0;
}
