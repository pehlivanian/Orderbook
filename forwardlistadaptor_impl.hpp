// forwardlistadaptor_impl.hpp

#ifndef __FORWARDLISTADAPTOR_IMPL_HPP__
#define __FORWARDLISTADAPTOR_IMPL_HPP__

// forwardlistadaptor_impl.hpp

template<typename Compare, std::size_t N>
std::pair<typename ForwardListAdaptor<Compare, N>::ContainerType::iterator, 
          typename ForwardListAdaptor<Compare, N>::ContainerType::iterator>
ForwardListAdaptor<Compare, N>::findInsertionPoint(const Message::order& o) {
    auto it = orders_.before_begin();
    auto next = orders_.begin();
    
    // Find where price ordering changes OR end of same price level
    while (next != orders_.end()) {
        if (Compare{}(o, *next)) {
            // Found higher price level (for bids) or lower price level (for asks)
            break;
        }
        if (next->price_ == o.price_) {
            // At same price level, continue to end of this price level
            while (next != orders_.end() && next->price_ == o.price_) {
                ++it;
                ++next;
            }
            break;
        }
        ++it;
        ++next;
    }
    
    return {it, next};
}

template<typename Compare, std::size_t N>
ack
ForwardListAdaptor<Compare, N>::insert_(const Message::order& o) {
    auto [it, next] = findInsertionPoint(o);
    orders_.insert_after(it, o);
    return ack{o.seqNum_, o.orderId_, true, ""};
}


template<typename Compare, std::size_t N>
std::pair<ack, std::optional<typename ForwardListAdaptor<Compare, N>::TradesType>>
ForwardListAdaptor<Compare, N>::execute_(const Message::order& exec_order) {
  if (orders_.empty()) {
    return {ack{exec_order.seqNum_, exec_order.orderId_, false, "Order book empty"}, std::nullopt};
  }
  
  auto it = orders_.before_begin();
  auto next = orders_.begin();
  unsigned long remaining = exec_order.size_;
  
  // We allow executions at any price level; otherwise remove this outer loop
  // Advance to first possible price level
  
  ForwardListAdaptor<Compare, N>::TradesType trades;
  char standingOrderSide = exec_order.side_ == 'B' ? 'S' : 'B';
  
  while (next != orders_.end() && remaining > 0 && 
	   ( CompareType{}(*next, exec_order) || next->price_ == exec_order.price_)) {
        if (remaining >= next->size_) {
            remaining -= next->size_;
	    
	    // Persist trade information
	    // Liquidity-providing order
	    trades.emplace_back(next->seqNum_, exec_order.seqNum_, next->orderId_, standingOrderSide, next->price_, next->size_);
	    // Liquidity-taking order
	    trades.emplace_back(exec_order.seqNum_, next->seqNum_, exec_order.orderId_, exec_order.side_, next->price_, next->size_);
	    
            next = orders_.erase_after(it);
        } else {
            next->size_ -= remaining;

	    // Persist trade information
	    // Liquidity-providing order
	    trades.emplace_back(next->seqNum_, exec_order.seqNum_, next->orderId_, standingOrderSide, next->price_, remaining);
	    // Liquidity-taking order
	    trades.emplace_back(exec_order.seqNum_, next->seqNum_, exec_order.orderId_, exec_order.side_, next->price_, remaining);

            remaining = 0;
            break;
        }
    }
  
  return {ack{exec_order.seqNum_, exec_order.orderId_, true, ""}, trades};
}

template<typename Compare, std::size_t N>
ack
ForwardListAdaptor<Compare, N>::remove_(const Message::order& o) {
    auto it = orders_.before_begin();
    auto next = orders_.begin();
    
    while (next != orders_.end()) {
        if (next->orderId_ == o.orderId_) {
	  ack a{next->seqNum_, next->orderId_, true, ""};
            orders_.erase_after(it);
            return a;
        }
        ++it;
        ++next;
    }
    
    return ack{o.seqNum_, o.orderId_, true, ""};
}

template<typename Compare, std::size_t N>
ack
ForwardListAdaptor<Compare, N>::update_(const Message::order& o) {
    remove_(o);
    return insert_(o);
}

template<typename Compare, std::size_t N>
std::optional<long>
ForwardListAdaptor<Compare, N>::getBBOPrice() const {
  if (!orders_.empty())
    return (*orders_.begin()).price_;
  else
    return std::nullopt;
}

template<typename Compare, std::size_t N>
std::optional<long>
ForwardListAdaptor<Compare, N>::getBBOSize() const {
  if (!orders_.empty())
    return (*orders_.begin()).size_;
  else
    return std::nullopt;
}

template<typename Compare, std::size_t N>
std::optional<unsigned>
ForwardListAdaptor<Compare, N>::sizeAtPrice(long price) const {
  if (!orders_.empty()) {
    unsigned qty = 0;
    auto it = orders_.begin();
    while (CompareLong()(it->price_, price) || it->price_ == price) {
      qty += it->size_;
      ++it;
    }
    return qty;
  } else {
    return std::nullopt;
  }
}

template<typename Compare, std::size_t N>
std::vector<Book::PriceLevel>
ForwardListAdaptor<Compare, N>::getBook() const {
  std::vector<Book::PriceLevel> book;

  bool head = true, levelPushed = false;
    Book::PriceLevel price_level;
    std::size_t levels = 0;

    for (const auto& order : orders_) {
      if ((price_level.price_ != order.price_) || head) {
	if (!head) {
	  book.push_back(price_level);
	  levelPushed = true;
	  levels++;
	  if (levels == N) 
	    break;
	}

	price_level = Book::PriceLevel{};
	price_level.price_ = order.price_;
	price_level.size_ = order.size_;
	price_level.orderCount_ = 1;
	levelPushed = false;

	head = false;
      } else {
	price_level.size_ += order.size_;
	price_level.orderCount_++;
      }
      
    }
    
    if (!head && !levelPushed)
      book.push_back(price_level);
    
    return book;
}

#endif
