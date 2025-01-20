#ifndef __ORDERBOOK_IMPL_HPP__
#define __ORDERBOOK_IMPL_HPP__

template<typename Container>
void 
BookSide<Container>::processEvent(const order& o, int l, char msgType) {
  switch(msgType) {
  case('I'):
    BookFeeder<BookSide<Container>>::insert(o);
    break;
  case('D'):
    BookFeeder<BookSide<Container>>::remove(o);
    break;
  case('U'):
    BookFeeder<BookSide<Container>>::update(o);
    break;
  case('E'):
    BookFeeder<BookSide<Container>>::execute(o);
    break;
  }
}

template<typename BidContainer, typename AskContainer>
OrderBook<BidContainer, AskContainer>::OrderBook() {
  bidSide_.reset(new BookSide<BidContainer>{});
  askSide_.reset(new BookSide<AskContainer>{});
}

template<typename BidContainer, typename AskContainer>
void
OrderBook<BidContainer, AskContainer>::replay() {

  namespace ranges = std::ranges;
  namespace views = std::views;

  boost::filesystem::path path = boost::filesystem::current_path();
  path /= input_file;
  
  auto eventstream = EventStream(path.string());

  for (auto e : eventstream)
    std::cout << e << std::endl;
}

#endif
