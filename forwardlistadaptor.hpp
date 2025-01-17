#ifndef __FORWARDLISTADAPTOR_HPP__
#define __FORWARDLISTADAPTOR_HPP__

#include <iostream>
#include <forward_list>

#include "orderbook.hpp"
#include "utils.hpp"

using namespace Utils;

template<typename Compare, int N>
class ForwardListAdaptor {
public:

  using ContainerType = std::forward_list<order>;
  static int NumLevels;

  ForwardListAdaptor() : book_{{}} {
    init_();
  }

  ContainerType getBook() const { return book_; }

  friend BookSide<ForwardListAdaptor>;

private:
  void insert_(const order&);
  void remove_(const order&);
  void update_(const order&);
  void execute_(const order&);

  ContainerType book_;

  void init_();
};

template<typename Compare, int N>
int ForwardListAdaptor<Compare, N>::NumLevels = N;

#include "forwardlistadaptor_impl.hpp"

#endif
