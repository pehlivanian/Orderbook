#ifndef __TRADEJOURNAL_HPP__
#define __TRADEJOURNAL_HPP__

#include "orderbook.hpp"
#include "utils.hpp"

#include <memory>
#include <optional>
#include <unordered_map>

using namespace Message;
using namespace Book;


struct DataStore {
 
  std::optional<trade> get(long key) {
    if (auto ind = store_.find(key); ind != store_.end()) {
      return ind->second;
    }
    return std::nullopt;
  }
 
  std::unordered_map<long, trade> get_store() const { return store_; }

  std::unordered_map<long, trade> store_;
};

template<typename BidContainer, typename AskContainer>
class TradeJournalerOrderBook {

public:

  AckTrades processEvent(const eventLOBSTER& e) {
    auto res = orderBook_->processEvent(e); 

    bool has_value = res.second.has_value();

    if (has_value) {
      for (auto & trade : *res.second) {
	(dataStore_->store_)[trade.seqNum_] = trade;
      }

    }

    return res;
  }
  BookSnapshot getBook() const { return orderBook_->getBook(); }
  std::optional<long> getBestBidPrice() const { return orderBook_->getBestBidPrice(); }
  std::optional<unsigned long> getBestBidSize() const { return orderBook_->getBestBidSize(); }
  std::optional<long> getBestAskPrice() const { return orderBook_->getBestAskPrice(); }
  std::optional<unsigned long> getBestAskSize() const { return orderBook_->getBestAskSize(); }  
  std::optional<unsigned long> sizeAtPrice(long price, bool isBid) const { return orderBook_->sizeAtPrice(price, isBid); }
  
  
  TradeJournalerOrderBook() :
    orderBook_{std::make_unique<OrderBook<BidContainer, AskContainer>>()}
  {}
  
  TradeJournalerOrderBook(OrderBook<BidContainer, AskContainer> orderBook) :
    orderBook_{std::make_unique<OrderBook<BidContainer, AskContainer>>(std::move(orderBook))}
  {}

  DataStore get_journal() const { return *dataStore_; }

private:
  
  std::unique_ptr<OrderBook<BidContainer, AskContainer>> orderBook_;
  std::unique_ptr<DataStore> dataStore_ = std::make_unique<DataStore>();
 
};

#endif
