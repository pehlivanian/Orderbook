#ifndef __FORWARDLISTADAPTOR_HPP__
#define __FORWARDLISTADAPTOR_HPP__

#include <forward_list>
#include <vector>
#include <optional>
#include <cmath>

#include "utils.hpp"
#include "orderbook.hpp"

using namespace Message;

template<typename Compare, std::size_t N>
class ForwardListAdaptor : public BookFeeder<ForwardListAdaptor<Compare, N>> {
public:
    using ContainerType = std::forward_list<Message::order>;
    using CompareType = Compare;
  using TradesType = std::vector<trade>;
    static constexpr std::size_t NumLevels = N;

  ForwardListAdaptor() = default;
  std::vector<Book::PriceLevel> getBook() const;
  std::optional<long> getBBOprice() const;

private:
    // These need to be accessible to BookFeeder through CRTP
    friend class BookFeeder<ForwardListAdaptor<Compare, N>>;
  
  ack insert_(const Message::order& o);
  ack remove_(const Message::order& o);
  ack update_(const Message::order& o);
  std::pair<ack, std::optional<TradesType>>execute_(const Message::order& o);

  
  std::pair<typename ContainerType::iterator, typename ContainerType::iterator>
  findInsertionPoint(const Message::order& o);
  
  ContainerType orders_;

  friend class BookSide<ForwardListAdaptor<Compare, N>>;
};

#include "forwardlistadaptor_impl.hpp"

#endif
