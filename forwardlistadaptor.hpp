#ifndef __FORWARDLISTADAPTOR_HPP__
#define __FORWARDLISTADAPTOR_HPP__

#include <cmath>
#include <forward_list>
#include <optional>
#include <vector>

#include "orderbook.hpp"
#include "utils.hpp"

using namespace Message;

template <typename Compare, std::size_t N>
class ForwardListAdaptor : public BookFeeder<ForwardListAdaptor<Compare, N>> {
 public:
  using ContainerType = std::forward_list<Message::order>;
  using CompareType = Compare;

  using CompareLong = std::conditional_t<std::is_same_v<Compare, std::less<Message::order>>,
                                         std::less<long>, std::greater<long>>;

  using CompareUnsignedLong =
      std::conditional_t<std::is_same_v<Compare, std::less<Message::order>>,
                         std::less<unsigned long>, std::greater<unsigned long>>;

  using TradesType = std::vector<trade>;
  static constexpr std::size_t NumLevels = N;

  ForwardListAdaptor() = default;
  std::vector<Book::PriceLevel> getBook() const;
  std::optional<long> getBBOPrice() const;
  std::optional<long> getBBOSize() const;
  std::optional<unsigned> sizeAtPrice(long) const;

 private:
  // These need to be accessible to BookFeeder through CRTP
  friend class BookFeeder<ForwardListAdaptor<Compare, N>>;

  ack insert_(const Message::order& o);
  ack remove_(const Message::order& o);
  ack update_(const Message::order& o);
  std::pair<ack, std::optional<TradesType>> execute_(const Message::order& o);

  std::pair<typename ContainerType::iterator, typename ContainerType::iterator> findInsertionPoint(
      const Message::order& o);

  ContainerType orders_;

  friend class BookSide<ForwardListAdaptor<Compare, N>>;
};

#include "forwardlistadaptor_impl.hpp"

#endif
