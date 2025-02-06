#ifndef __ORDERBOOK_HPP__
#define __ORDERBOOK_HPP__

#include <iterator>
#include <cmath>
#include <array>
#include <memory>
#include <iostream>
#include <vector>
#include <fstream>
#include <fstream>
#include <optional>
#include <variant>
#include <regex>
#include <ranges>

#include <boost/filesystem.hpp>

#include "concurrentqueue.h"
#include "utils.hpp"
#include "eventstream.hpp"

using namespace Message;
using namespace Book;
using namespace Utils;

using order = struct order;
using TradesType = std::vector<trade>;

//
// T ~ BookSide<OrderStatisticsTreeAdaptor<int, std::less<int>>
//

template<typename T>
class BookFeeder {

  enum class STATES {
    START = 0,
      UPDATING = 1,
      UPDATED = 2,
      ERROR = 3,
      NUMSTATES
      };

public:

  BookFeeder() : current_state_{STATES::START} {}

  ack insert(const order& o) { return static_cast<T*>(this)->insert_(o); }
  ack remove(const order& o) { return static_cast<T*>(this)->remove_(o); }
  ack update(const order& o) { return static_cast<T*>(this)->update_(o); }
  AckTrades execute(const order& o) { return static_cast<T*>(this)->execute_(o); }


  STATES get_current_state() const { return current_state_; }  
 
private:
  STATES current_state_;

};

template<typename Container>
class BookSide;
template<typename Container>
std::ostream& operator<<(std::ostream&, const BookSide<Container>*);

//
// Container ~ OrderStatisticsTreeAdaptor<int, std::less<int>>
//

template<typename Container>
class BookSide : public BookFeeder<BookSide<Container>> {
public:

  using ContainerType = typename Container::ContainerType;
  const int BookSize = Container::NumLevels;

  BookSide() : side_{Container{}} {}
  BookSide(const BookSide& rhs) :
    side_{rhs.side_}
  {}
  BookSide(BookSide&& rhs) :
    side_{std::move(rhs.side)}
  {}
  BookSide(const Container& rhs) :
    side_{rhs}
  {}

  BookSide(Container&& rhs) :
    side_{std::move(rhs.side_)}
  {}

  BookSide(std::unique_ptr<BookSide<Container>> rhs) :
    side_{std::make_unique<BookSide<Container>>(rhs)}
  {}

  AckTrades processEvent(const order&, int, char);

  Container getSide() const { return side_; }

  std::vector<Book::PriceLevel> getBook() const;
  std::optional<long> getBBOPrice() const { return side_.getBBOPrice(); }
  std::optional<unsigned long> getBBOSize() const { return side_.getBBOSize(); }
  std::optional<unsigned long> sizeAtPrice(long price) { return side_.sizeAtPrice(price); }

  friend BookFeeder<BookSide<Container>>;
  friend std::ostream& operator<< <> (std::ostream&, const BookSide<Container>*);

private:
  ack insert_(const order& o) { return side_.insert_(o); }
  ack remove_(const order& o) { return side_.remove_(o); }
  ack update_(const order& o) { return side_.update_(o); }
  std::pair<ack, std::optional<TradesType>> execute_(const order& o) { return side_.execute_(o); }

  Container side_;
};

// 
// BidContainer ~ OrderStatisticsTreeAdaptor<std::less<T>>
// AskContainer ~ OrderStatisticsTreeAdaptor<std::greater<T>>
// N ~ number of levels per side
//

template<typename BidContainer, typename AskContainer>
class OrderBook {
public:

  // const std::string input_file = "input.dat";
  // const std::string input_file = "GOOG_2012-06-21_34200000_57600000_message_1.csv";

  OrderBook();
  OrderBook(const OrderBook&);
  OrderBook(OrderBook&&);
	       
  void replay(std::string);

  AckTrades processEvent(const Message::eventLOBSTER& event);
  
  BookSnapshot getBook() const;
  std::optional<long> getBestBidPrice() const;
  std::optional<unsigned long> getBestBidSize() const;
  std::optional<long> getBestAskPrice() const;
  std::optional<unsigned long> getBestAskSize() const;
  std::optional<unsigned long> sizeAtPrice(long, bool) const;
  
  std::optional<long> getBestPrice(bool) const;
  std::optional<unsigned long> getBestSize(bool) const; 
  
  
private:

  std::unique_ptr<BookSide<BidContainer>> bidSide_;
  std::unique_ptr<BookSide<AskContainer>> askSide_;

  AckTrades processUndersizedCross(const Message::eventLOBSTER&, bool);
  AckTrades processOversizedCross(const Message::eventLOBSTER&, bool);

};

template<typename Container>
std::ostream& operator<<(std::ostream& os, const BookSide<Container>& rhs) {
#define PRINT_ALL_LEVELS
#ifdef PRINT_ALL_LEVELS
  auto book = rhs.getSide().getBook();
  auto it = book.begin(), last = book.end();
  while(it!=last) {
    if (it->size_ > 0)
      os << it->price_ << "," << it->size_ << ",";
    else
      os << ",,";
    ++it;
  }
#else
  auto it = book.begin();
  for (std::size_t i=0; i<N; ++i) {
    if (it->size_ > 0)
      os << it->price_ << "," << it->size_ << ",";
    else
      os << ",,";
    ++it;
  }
#endif
  os << "*****";
#undef PRINT_ALL_LEVELS
  return os;
}


#include "orderbook_impl.hpp"

#endif
