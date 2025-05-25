#ifndef __PRIORITYQUEUEADAPTOR_IMPL_HPP__
#define __PRIORITYQUEUEADAPTOR_IMPL_HPP__

template <typename Compare, int N>
void PriorityQueueAdaptor<Compare, N>::insert_(const order& o) {
  ;
}

template <typename Compare, int N>
void PriorityQueueAdaptor<Compare, N>::remove_(const order& o) {
  ;
}

template <typename Compare, int N>
void PriorityQueueAdaptor<Compare, N>::update_(const order& o) {
  ;
}

template <typename Compare, int N>
void PriorityQueueAdaptor<Compare, N>::execute_(const order& o) {
  ;
}

/*
template<typename Compare>
void
PriorityQueueAdaptor::insert_(const order& o, int l) {
  book_.push(o);
}

template<typename Compare>
void
PriorityQueueAdaptor::remove_(const order& o, int l) {
  int l0 = l;
  std::stack<order> popped;


  while (l > 1) {
    popped.push(book_.top());
    popped.pop();
    --l;
  }

  book_.pop();

  while (!popped.empty()) {
    book_.push(popped.top());
    book_.pop();
  }
}

template<typename Compare>
void update_(const order& o, int l) {
  int l0 = l;
  std::stack<order> popped;
  popped.push(o);

  while (l > 1) {
    popped.push(book_.top());
    popped.pop();
    --l;
  }

  book_.pop();

  while (!popped.empty()) {
    book_.push(popped.top());
    popped.pop();
  }
}
*/
#endif
