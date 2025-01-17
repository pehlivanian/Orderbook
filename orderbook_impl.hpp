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
    BookFeeder<BookSide<Container>>::update(o);
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
  boost::filesystem::path path = boost::filesystem::current_path();
  path /= input_file;

  auto events = readData(path.string());
  for (auto &[seqNum,
	      msgType,
	      side,
	      level,
	      price,
	      size]: events) {
    
    order ord = order{seqNum, price, size};

    switch (side) {
    case('B'):
      std::cout << "BIDMSG: " << msgType << " : " << ord << " : " << level << std::endl;
      std::cout << "BIDBOOK BEFORE: " << *bidSide_ << std::endl;
      bidSide_->processEvent({seqNum, price, size}, level, msgType);
      std::cout << "BIDBOOK AFTER: " << *bidSide_ << std::endl;
      break;
    case('S'):
      std::cout << "ASKMSG: " << msgType << " : " << ord << " : " << level << std::endl;
      std::cout << "ASKBOOK BEFORE: " << *askSide_ << std::endl;
      askSide_->processEvent({seqNum, price, size}, level, msgType);
      std::cout << "ASKBOOK AFTER: " << *askSide_ << std::endl;

      break;
    }
  }
}

#endif
