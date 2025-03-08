#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <syncstream>


#include <string>
#include <string_view>
#include <cmath>
#include <utility>
#include <optional>
#include <functional>
#include <algorithm>
#include <vector>

#include <boost/filesystem.hpp>

namespace Numerics {

  constexpr bool isPowerOfTwo(std::size_t x) {
    return x && !(x & (x - 1));
  }

  constexpr std::size_t nextPowerOfTwo(std::size_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x++;
    return x;
  }

}

namespace Book {
  struct PriceLevel {
    long price_;
    unsigned long size_;
    std::size_t orderCount_;
  };

  struct BookSnapshot {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
  };

} // namespace Book

namespace Message {

  struct ack {
    unsigned long seqNum_;
    unsigned long orderId_;
    bool acked_;
    std::string reasonRejected_;
  };

  struct trade {
    unsigned long seqNum_;
    unsigned long counterSeqNum_;
    unsigned long orderId_;
    char side_;
    long price_;
    unsigned long size_;
  };

  struct order {   
    unsigned long seqNum_;
    char side_;
    double time_;
    unsigned long orderId_;
    long price_;
    unsigned long size_;
    short orderType_;
    
    bool operator==(const Message::order& rhs) {
      return (side_ == rhs.side_) && (fabs(price_ - rhs.price_) < std::numeric_limits<double>::epsilon());
    }
    
  };
  
  struct event {
    unsigned long seqNum;
    char msgType;
    char side;
    int level;
    long price;
    unsigned long size;    
  };
  
  struct eventLOBSTER {
    unsigned long seqNum_;
    double time_;
    short eventType_;
    unsigned long orderId_;
    unsigned size_;
    long price_;
    char direction_;
  };
  
  using TradesType = std::vector<trade>;
  using AckTrades = std::pair<ack, std::optional<TradesType>>;

} // namespace Message

namespace Utils {

  Message::order eventLOBSTERToOrder(const Message::eventLOBSTER& e) {
    return Message::order{e.seqNum_, e.direction_, e.time_, e.orderId_, e.price_, e.size_, e.eventType_};
  }

  void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
				    [](unsigned char c){
				      return !std::isspace(c);
				    }));
  }

  void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), 
			 [](unsigned char c) {
			   return !std::isspace(c);
			 }).base(), s.end());
  }

  void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
  }

  void ltrim(std::string_view& sv) {
    return sv.remove_prefix(std::min(sv.find_first_not_of(" /t/n/r/f/v"), sv.size()));
  }

  void rtrim(std::string_view& sv) {
    while (!sv.empty() && std::isspace(sv.back())) {
      sv.remove_suffix(1);
    }
  }

  void trim(std::string_view& sv) {
    ltrim(sv);
    rtrim(sv);
  }

  std::vector<std::string_view> split(const std::string_view& sv) {
    std::string::size_type first=0, last=0;
    std::vector<std::string_view> tokens;
    while ((last = sv.find(" ",first)) != std::string_view::npos) {
      tokens.push_back(sv.substr(first, last-first));
      first = last + 1;
    }
    tokens.push_back(sv.substr(first));
    return tokens;
  }

  std::vector<std::string> split(const std::string& s, const char tok=' ') {
    std::string::size_type first=0, last=0;
    std::vector<std::string> tokens;
    while ((last = s.find(",", first)) != std::string::npos) {
      tokens.push_back(s.substr(first,last-first));
      first = last + 1;
    }
    tokens.push_back(s.substr(first));
    return tokens;
  }

  std::ostream& operator<<(std::ostream& os, const Message::order& o) {
    os << "{ "
       << o.seqNum_ << ", "
       << o.time_   << ", "
       << o.side_   << ", "
       << o.price_  << ", "
       << o.size_   << "}";
    return os;
  }

  std::osyncstream& operator<<(std::osyncstream& os, const Message::order& o) {
    os << "{ "
       << o.seqNum_    << ", "
       << o.time_      << ", "
       << o.side_      << ", "
       << o.price_     << ", "
       << o.size_      << ", "
       << o.orderType_ << "}";
    return os;
  }

  std::ostream& operator<<(std::ostream& os, const Message::event& e) {
    os << "{ " 
       << e.seqNum  << ", "
       << e.msgType << ", "
       << e.side    << ", "
       << e.level   << ", "
       << e.price   << ", "
       << e.size    << "}";
    return os;
  }

  std::ostream& operator<<(std::ostream& os, const Message::eventLOBSTER& e) {
    os << "{"
       << std::setprecision(14) 
       << e.seqNum_ << ", "
       << e.time_ << ", "
       << e.eventType_ << ", "
       << e.orderId_ << ", "
       << e.size_ << ", "
       << e.price_ << ", "
       << e.direction_ << "}";
    return os;
  }

  void check(int test, const char* message, ...) {
    if (test) {
      va_list args;
      va_start(args, message);
      vfprintf(stderr, message, args);
      va_end(args);
      fprintf(stderr, "\n");
      exit(EXIT_FAILURE);
    }
  }

  std::size_t fileSize(const std::string& fn) {
    std::ifstream ifs{fn, std::ifstream::binary};
    std::filebuf* pbuf = ifs.rdbuf();
    std::size_t size = pbuf->pubseekoff(0, ifs.end, ifs.in);
    return size;
  }

  const char* read_mmap(const char* fn) {

    const char* mapped;
    std::size_t size = fileSize(fn);

    int fd = open(fn, O_RDONLY);
    check(fd < 0, "open %s failed: %s", fn, strerror(errno));

    mapped = (char*)mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    check(mapped == MAP_FAILED, "mmap %s failed: %s", fn, strerror(errno));

    return mapped;
  }
  
  std::vector<Message::eventLOBSTER> readDataLOBSTER(const boost::filesystem::path& path) {
    
    /*
      struct eventLOBSTER {
      unsigned long seqNum_;
      float time_;
      short eventType_;
      unsigned long orderId_;
      unsigned size_;
      long price_;
      char direction_;
      };
    */

    std::vector<Message::eventLOBSTER> res;

    std::string line;
    std::ifstream fh{path.string()};

    bool eof = false;
    unsigned long seqNum = 0;

    while (std::getline(fh, line) and !eof) {

      auto tokens = split(line, ',');
      
      if (tokens.size() == 6) {
	float time = std::stof(tokens[0]);
	short eventType = std::stoi(tokens[1]);
	unsigned long orderId = std::stoul(tokens[2]);
	unsigned int size = static_cast<unsigned int>(std::stoul(tokens[3]));
	long price = std::stol(tokens[4]);
	char direction = tokens[5] == "1" ? 'B' : 'S';
	
	Message::eventLOBSTER e = {++seqNum, time, eventType, orderId, size, price, direction};
	res.push_back(e);
      }
      
    }

    return res;

  }

  std::vector<Message::event> readDataDefault(const boost::filesystem::path& path) {

    /*
      struct event {
      unsigned long seqNum;
      char msgType;
      char side;
      int level;
      long price;
      unsigned long size;    
      };
    */
    
    const double MULT = 1000.;
    
    std::vector<Message::event> res;

    std::string line;
    std::ifstream fh{path.string()};
    while (std::getline(fh, line)) {
      auto tokens = split(line, ',');
      Message::event e = {std::stoul(tokens[0]), tokens[1][0], tokens[2][0], 
		 std::stoi(tokens[3]), static_cast<long>(std::stod(tokens[4])*MULT),
		 std::stoul(tokens[5])};
      res.push_back(e);
    }

    return res;
  }

} // namespace Utils

namespace std {
  template<>
  struct less<Message::order> {
    bool operator()(const Message::order& a, const Message::order& b) const {
      return a.price_ < b.price_;
    };
  };

  template<>
  struct greater<Message::order> {
    bool operator()(const Message::order& a, const Message::order& b) const {
      return a.price_ > b.price_;
    }
  };

}


#endif
