#ifndef __ORDERBOOK_IMPL_HPP__
#define __ORDERBOOK_IMPL_HPP__


template<typename Container>
std::pair<ack, std::optional<TradesType>>
BookSide<Container>::processEvent(const order& o, int l, char msgType) {
    switch(msgType) {
    case('I'): {
      auto a = this->insert(o);
      return std::make_pair(a, std::nullopt);;
        break;
    }
    case('D'): {
      auto a = this->remove(o);
      return std::make_pair(a, std::nullopt);
      break;
    }
    case('U'): {
      auto a = this->update(o);
      return std::make_pair(a, std::nullopt);
      break;
    }
    case('E'): {
      return this->execute(o);
      break;
    }
    }
}

template<typename BidContainer, typename AskContainer>
OrderBook<BidContainer, AskContainer>::OrderBook() {
  bidSide_.reset(new BookSide<BidContainer>{});
  askSide_.reset(new BookSide<AskContainer>{});
}

template<typename BidContainer, typename AskContainer>
void
OrderBook<BidContainer, AskContainer>::replay(std::string input_file) {

  namespace ranges = std::ranges;
  namespace views = std::views;

  boost::filesystem::path path = boost::filesystem::current_path();
  path /= input_file;
  
  auto eventstream = EventStream(path.string());

  for (auto e : eventstream)
    std::cout << e << std::endl;

}

template<typename BidContainer, typename AskContainer>
BookSnapshot
OrderBook<BidContainer, AskContainer>::getBook() const {
  return {bidSide_->getBook(), askSide_->getBook() };
}

template<typename BidContainer, typename AskContainer>
AckTrades
OrderBook<BidContainer, AskContainer>::processUndersizedCross(const Message::eventLOBSTER& event,
							      bool isBid) {
  auto tradeEvent = eventLOBSTER{event};
  tradeEvent.eventType_ = 4;
  tradeEvent.direction_ = isBid ? 'S' : 'B';
  
  Message::order tradeOrder = Utils::eventLOBSTERToOrder(tradeEvent);
  
  if (isBid) {
    return askSide_->processEvent(tradeOrder, 0, 'E');
  } else {
    return bidSide_->processEvent(tradeOrder, 0, 'E');
  }

}



template<typename BidContainer, typename AskContainer>
AckTrades
OrderBook<BidContainer, AskContainer>::processOversizedCross(const Message::eventLOBSTER& event,
							     bool isBid) {

  AckTrades res1, res2;
  
  unsigned remainder = event.size_;
  TradesType allTrades;

  using SideType = std::variant<std::reference_wrapper<BookSide<BidContainer>>, std::reference_wrapper<BookSide<AskContainer>>>;
  using CompType = std::variant<typename BidContainer::CompareLong, typename AskContainer::CompareLong>;

  SideType execSide = isBid ? static_cast<SideType>(std::ref(*askSide_)) : static_cast<SideType>(std::ref(*bidSide_));
  SideType orderSide = isBid ? static_cast<SideType>(std::ref(*bidSide_)) : static_cast<SideType>(std::ref(*askSide_));
  CompType comparer = isBid ? static_cast<CompType>(typename AskContainer::CompareLong()) : static_cast<CompType>(typename BidContainer::CompareLong());

  /* All spelled out
     
  std::variant<std::reference_wrapper<BookSide<BidContainer>>, std::reference_wrapper<BookSide<AskContainer>>> execSide = isBid ?
    std::variant<std::reference_wrapper<BookSide<BidContainer>>, std::reference_wrapper<BookSide<AskContainer>>>(std::ref(*askSide_)) :
    std::variant<std::reference_wrapper<BookSide<BidContainer>>, std::reference_wrapper<BookSide<AskContainer>>>(std::ref(*bidSide_));

  std::variant<BookSide<BidContainer>, BookSide<AskContainer>> orderSide = isBid ?
    std::variant<BookSide<BidContainer>, BookSide<AskContainer>>(*bidSide_) :
    std::variant<BookSide<BidContainer>, BookSide<AskContainer>>(*askSide_);

  std::variant<typename AskContainer::CompareLong, typename BidContainer::CompareLong> comparer = isBid ? 
    std::variant<typename AskContainer::CompareLong, typename BidContainer::CompareLong>(typename AskContainer::CompareLong()) : 
    std::variant<typename AskContainer::CompareLong, typename BidContainer::CompareLong>(typename BidContainer::CompareLong());

  */

  while (!std::visit([&event, &execSide](auto& comp)
		     { 
		       return comp(event.price_, std::visit([](auto& side)
							    { 
							      return *(side.get().getBBOPrice()); 
							    }, 
							    execSide)
				   ); 
		     }, comparer) && remainder) {
    
    auto tradeEvent = eventLOBSTER{event};
    
    unsigned bookSize = std::visit([](auto& side){ return *(side.get().getBBOSize()); }, execSide);
    unsigned tradeSize = bookSize > remainder ? remainder : bookSize;
    
    if (tradeSize) {
      tradeEvent.eventType_ = 4;
      tradeEvent.direction_ = isBid? 'S' : 'B';
      tradeEvent.size_ = tradeSize;
      
      Message::order tradeOrder = Utils::eventLOBSTERToOrder(tradeEvent);
      
      res1 = std::visit([&tradeOrder](auto& side){ return side.get().processEvent(tradeOrder, 0, 'E'); }, execSide);
      // res1 = execSide->processEvent(tradeOrder, 0, 'E');
      for (auto & trade : *res1.second)
	allTrades.push_back(trade);
      
      // Inside has been executed against
      remainder -= tradeSize;

    } 
    else {
      remainder = 0;
    }
  }
  
  // Now remainder of order is smaller than current BBO size
  
  // Post order
  
  if (remainder) {
    auto orderEvent = eventLOBSTER{event};
    orderEvent.size_ = remainder;
    
    Message::order remainderOrder = Utils::eventLOBSTERToOrder(orderEvent);
    
    res2 = std::visit([&remainderOrder](auto& side){ return side.get().processEvent(remainderOrder, 0, 'I'); }, orderSide);
    // res2 = bidSide_->processEvent(remainderOrder, 0, 'I');
    return {res2.first, allTrades};
  }

  return {res1.first, allTrades};
}

template<typename BidContainer, typename AskContainer>
AckTrades
OrderBook<BidContainer, AskContainer>::processEvent(const Message::eventLOBSTER& event) {
    // Convert LOBSTER event type to message type
    char msgType;
    switch(event.eventType_) {
        case 1: msgType = 'I'; break;  // Add order
        case 2: msgType = 'D'; break;  // Cancel order
        case 3: msgType = 'D'; break;  // Delete order
        case 4: msgType = 'E'; break;  // Execute order
        case 5: msgType = 'U'; break;  // Update order
        default:
            std::cerr << "Unknown event type: " << event.eventType_ << std::endl;
            return {ack{}, std::nullopt};
    }

    // Create order from event
    Message::order order = Utils::eventLOBSTERToOrder(event);
    
    // Route to appropriate side
    if (event.direction_ == 'B') {
      if (msgType == 'I') {
	if (auto bbo = askSide_->getBBOPrice(); bbo && event.price_ >= *bbo) {
	  if (event.size_ <= askSide_->getBBOSize()) {

	    return processUndersizedCross(event, true);

	  } else {

	    return processOversizedCross(event, true);
	  }
	} else {
	  // Normal order entry
	  return bidSide_->processEvent(order, 0, msgType);
	}
      } else {
	return bidSide_->processEvent(order, 0, msgType);
      }
    } else if (event.direction_ == 'S') {
      if (msgType == 'I') {
	if (auto bbo = bidSide_->getBBOPrice(); bbo && event.price_ <= *bbo) {
	  if (event.size_ <= bidSide_->getBBOSize()) {

	    return processUndersizedCross(event, false);

	  } else {
	    
	    return processOversizedCross(event, false);

	  }
	} else {
	  // Normal order entry
	  return askSide_->processEvent(order, 0, msgType);
	}
      }
      return askSide_->processEvent(order, 0, msgType);
    }
}

template<typename Container>
std::vector<Book::PriceLevel> 
BookSide<Container>::getBook() const {
  
  return side_.getBook();

}

#endif
