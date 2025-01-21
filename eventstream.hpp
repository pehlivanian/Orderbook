#ifndef __EVENTSTREAM_HPP__
#define __EVENTSTREAM_HPP__

#include "utils.hpp"

#include <iterator>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>

using namespace Utils;
using namespace Message;

struct eventiterator {

  using iterator_category = std::forward_iterator_tag;
  using value_type = eventLOBSTER;
  using difference_type = std::ptrdiff_t;
  using pointer = eventLOBSTER*;
  using reference = eventLOBSTER&;

  static int seqNum_;

  eventiterator() noexcept {}

  explicit eventiterator(const std::istreambuf_iterator<char>& rhs) noexcept :
    it{rhs}
  {}

  eventiterator(const char* buf) :
    iss{buf},
    it{iss}
  {}

  eventiterator(eventiterator&& rhs) {
    iss = std::move(rhs.iss);
    it = std::move(rhs.it);
  }

  eventiterator(const eventiterator& rhs) {
    it = rhs.it;
  }

  eventiterator operator=(const eventiterator& rhs) {
    if (this != &rhs) {
      it = rhs.it;
    }
    return *this;
  }

  // prefix
  eventiterator& operator++() noexcept {
    ++it;
    return *this; 
  }

  // postfix
  eventiterator operator++(int) noexcept {
    eventiterator rhs = *this;
    ++*this;
    return rhs;
  }

  bool operator==(const eventiterator& rhs) const noexcept {
    return rhs.it == it;
  }

  bool operator!=(const eventiterator& rhs) const noexcept {
    return !(rhs == *this);
  }

  eventLOBSTER operator*() {

    // Fill out this struct
    //
    //     struct eventLOBSTER {
    //       double time_;
    //       short eventType_;
    //       unsigned long orderId_;
    //       unsigned size_;
    //       long price_;
    //       char direction_;
    //     };


    // We assume there is one EventStream object
    // as we place seqNum sequentially on each event 
    // read.

    std::string line;
    
    eventLOBSTER e{};
    e.seqNum_ = ++seqNum_;

    int fieldNum = 0;
    
    while (*it != '\n') {
      while ((*it != ',') && (*it != '\n')) {
	line += *it;
	++it;
      }
      
      switch(fieldNum) {
      case(0):
	e.time_ = std::stod(line);
	break;
      case(1):
	e.eventType_ = std::stoi(line);
	break;
      case(2):
	e.orderId_ = std::stoul(line);
	break;
      case(3):
	e.size_ = static_cast<unsigned int>(std::stoul(line));
	break;
      case(4):
	e.price_ = std::stol(line);
	break;
      case(5):
	e.direction_ = line == "1" ? 'B' : 'S';
	break;
      }

      if (*it == '\n') break;
      line = std::string{};
      fieldNum++;
      ++it;
    }


    return e;
  }

  std::istringstream iss;
  std::istreambuf_iterator<char> it;

};

class EventStream {
public:

  EventStream(std::string path) :
    buf_{read_mmap(path.c_str())},
    begin_{eventiterator{buf_}}
  {}

  eventiterator begin() { return begin_; }
  eventiterator end(){ return end_; }

private:
  const char* buf_;
  eventiterator begin_;
  eventiterator end_;
};

int eventiterator::seqNum_ = 0;

#endif
