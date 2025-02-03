#ifndef __TRADEJOURNAL_HPP__
#define __TRADEJOURNAL_HPP__

#include "orderbook.hpp"

template<typename BidContainer, typename AskContainer>
class TradeJournalerOrderBook : public OrderBookDecoratorBase<BidContainer, AskContainer> {
  
public:

  using BaseType = OrderBookDecoratorBase<BidContainer, AskContainer>;
  using BaseType::DecoratorBase;

  auto operator->() {
    std::cout << "TradeJournalerOrderBook operator->\n";
    return BaseType::operator->();
  }

  void seeMe() {
    std::cout << "I see you from TradeJournalerOrderBook!!" << std::endl;
  }
  
};

#endif
