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
void 
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
            return;
    }

    // Create order from event
    Message::order order = Utils::eventLOBSTERToOrder(event);
    
    // Route to appropriate side
    if (event.direction_ == 'B') {
      if (auto bbo = askSide_->getBBOprice(); bbo && event.price_ >= *bbo) {
	// Book is locked/crossed
	// 1. Execute appropriate orders
	// 2. Determine remaining quantity
	// 3. Post in bid book
      }
      bidSide_->processEvent(order, 0, msgType);
    } else if (event.direction_ == 'S') {
      if (auto bbo = bidSide_->getBBOprice(); bbo && event.price_ <= *bbo) {
	// Book is locked/crossed
	// 1. Execute appropriate orders
	// 2. Determine remaining quantity
	// 3. Post in ask book
      }
      askSide_->processEvent(order, 0, msgType);
    }
}

template<typename Container>
std::vector<Book::PriceLevel> 
BookSide<Container>::getBook() const {
  
  return side_.getBook();

}

#endif
