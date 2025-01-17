#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <string>
#include <string_view>
#include <cmath>
#include <functional>
#include <algorithm>
#include <vector>

#include <boost/filesystem.hpp>

struct order {    
  unsigned long seqNum_;
  long price_;
  unsigned long size_;
  
  bool operator=(const Utils::order& rhs) {
    return fabs(price_ - rhs.price_) < std::numeric_limits<double>::epsilon();
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
  float time_;
  char eventType_;
  unsigned long orderId_;
  unsigned size_;
  float price_;
  char direction_;
};

namespace Utils {

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

  std::ostream& operator<<(std::ostream& os, const order& o) {
    os << "{ "
       << o.seqNum_ << ", "
       << o.price_ << ", "
       << o.size_ << "}";
    return os;
  }

  std::ostream& operator<<(std::ostream& os, const event& e) {
    os << "{ " 
       << e.seqNum  << ", "
       << e.msgType << ", "
       << e.side    << ", "
       << e.level   << ", "
       << e.price   << ", "
       << e.size    << "}";
    return os;
  }
  
  std::vector<eventLOBSTER> readDataLOBSTER(const boost:filesystem::path& path) {
    
    /*
      struct eventLOBSTER {
      float time_;
      char eventType_;
      unsigned long orderId_;
      unsigned size_;
      float price_;
      char direction_;
      };
    */

    std::vector<eventLOBSTER> res;

    std::string line;
    std::ifstream fh{path.string()};
    while (std::getline(fh, line)) {
      auto tokens = split(line, ',');
      eventLOBSTER = {std::stof(tokens[0]), tokens[1][0], std::stoul(tokens[2]),
		      std::stoul(tokens[3]), std::stof(tokens[4]), tokens[5][0]};
    }
  }

  std::vector<event> readDataDefault(const boost::filesystem::path& path) {

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
    
    std::vector<event> res;

    std::string line;
    std::ifstream fh{path.string()};
    while (std::getline(fh, line)) {
      auto tokens = split(line, ',');
      event e = {std::stoul(tokens[0]), tokens[1][0], tokens[2][0], 
		 std::stoi(tokens(3)), static_cast<long>(std::stod(tokens[4])*MULT),
		 std::stoul(tokens[5])};
      res.push_back(e);
    }

    return res;
  }

} // namespace Utils

namespace std {
  template<>
  struct less<Utils::order> {
    bool operator()(const Utils::order& a, const Utils::order& b) const {
      return a.price_ < b.price_;
    };
  };

  template<>
  struct greater<Utils::order> {
    bool operator()(const Utils::order& a, const Utils::order& b) const {
      return a.price_ > b.price_;
    }
  };

}


#endif
