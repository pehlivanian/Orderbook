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
#include <regex>
#include <ranges>

#include <boost/filesystem.hpp>

#include "concurrentqueue.h"
#include "utils.hpp"
#include "eventstream.hpp"

using namespace Message;
using namespace Utils;

using order = struct order;

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

  void insert(const order& o) { static_cast<T*>(this)->insert_(o); }
  void remove(const order& o) { static_cast<T*>(this)->remove_(o); }
  void update(const order& o) { static_cast<T*>(this)->update_(o); }
  void execute(const order& o) { static_cast<T*>(this)->insert_(o); }


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

  void processEvent(const order&, int, char);

  Container getSide() const { return side_; }

  friend BookFeeder<BookSide<Container>>;
  friend std::ostream& operator<< <> (std::ostream&, const BookSide<Container>*);

private:
  void insert_(const order& o) { side_.insert_(o); }
  void remove_(const order& o) { side_.remove_(o); }
  void update_(const order& o) { side_.update_(o); }
  void execute_(const order& o) { side_.execute_(o); }

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
  void replay(std::string);
  
private:

  

  std::unique_ptr<BookSide<BidContainer>> bidSide_;
  std::unique_ptr<BookSide<AskContainer>> askSide_;

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
