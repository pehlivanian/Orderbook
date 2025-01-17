#ifndef __FORWARDLISTADAPTOR_IMPL_HPP__
#define __FORWARDLISTADAPTOR_IMPL_HPP__

template<typename Compare, int N>
void
ForwardListAdaptor<Compare, N>::init_() {
  order nullOrder = order{0, 0, 0};
  for (int i=0; i<N; ++i) {
    book_.push_front(nullOrder);
  }
}

template<typename Compare, int N>
void
ForwardListAdaptor<Compare, N>::insert_(const order& o, int l) {
  auto it = book_.before_begin();
  while (l > 0) {
    ++it;
    --l;
  }

  book_.insert_after(it, o);

  return;
}

template<typename Compare, int N>
void 
ForwardListAdaptor<Compare, N>::remove_(const order& o, int l) {

  auto it = book_.before_begin();
  while (l > 0) {
    ++it;
    --l;
  }

  book_.erase_after(it);

  return;
}

template<typename Compare, int N>
void 
ForwardListAdaptor<Compare, N>::update_(const order& o, int l) {

  auto it = book_.begin();
  while (l > 1) {
    ++it;
    --l;
  }

  book_.erase_after(it);
  book_.insert_after(it, o);

  return;
}

#endif
