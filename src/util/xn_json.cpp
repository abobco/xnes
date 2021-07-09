#include "xn_json.hpp"
namespace xn {
json load_json_file(const std::string &filepath) {
  std::ifstream t(filepath);
  std::string buf((std::istreambuf_iterator<char>(t)),
                  std::istreambuf_iterator<char>());
  return json::parse(buf);
}
} // namespace xn