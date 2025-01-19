#ifndef __PRIORITYQUEUEADAPTOR_HPP__
#define __PRIORITYQUEUEADAPTOR_HPP__

#include <algorithm>
#include <iterator>
#include <iostream>
#include <stack>

#include "utils.hpp"

using namespace Utils;
using namespace Message;


template<class T, class Compare=std::less<typename std::vector<T>::value_type>>
class priority_queue {
  using container = std::vector<T>;
  using value_type = typename container::value_type;
  using size_type = typename container::size_type;
  using reference = typename container::reference;
  using const_reference = typename container::const_reference;

public:
  priority_queue(const container& data) :
    data_{data}
  {
    std::make_heap(data_.begin(), data_.end());
  }
  priority_queue(const container& data, Compare comparer) :
    data_{data},
    comparer_{comparer}
  {
    std::make_heap(data_.begin(), data_.end());
  }

  bool empty() const noexcept { return data_.empty(); }
  size_type size() const noexcept { return data_.size(); }

  void push(const T& value) {
    data_.push_back(value);
    std::push_heap(data_.begin(), data_.end(), comparer_);
  }

  void pop() {
    std::pop_heap(data_.begin(), data_.end(), comparer_);
    data_.pop_back();
  }

  const_reference top() const { return data_.front(); }
  void swap(priority_queue& rhs) noexcept {
    swap(data_, rhs.data_);
    swap(comparer_, rhs.comparer_);
  }

  void print() const {
    std::copy(data_.begin(), data_.end(), std::ostream_iterator<T>(std::cout, " "));
    std::cout << std::endl;
  }

private:
  container data_;
  Compare comparer_;
};

template<class Compare, int N>
class PriorityQueueAdaptor {
public:
  PriorityQueueAdaptor() : book_{{}} {}
  
private:
  void insert_(const order&);
  void remove_(const order&);
  void update_(const order&);
  void execute_(const order&);

  priority_queue<order, Compare> book_;
};

#endif
