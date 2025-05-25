#include "tradejournal.hpp"

#include <memory>
#include <vector>

auto main(int argc, char **argv) -> int {
  using BidContainer = std::vector<int>;
  using AskContainer = std::vector<int>;
  using DecType = OrderBook<BidContainer, AskContainer>;
  using OrderBookType = TradeJournalerOrderBook<BidContainer, AskContainer>;

  auto orderbook = std::make_unique<OrderBookType>(std::make_unique<DecType>());

  orderbook->seeMe();
  orderbook->anotherMethod();

  return 0;
}
