#pragma once
#include "../external/json.hpp"
#include <fstream>
#include <iostream>
#include <streambuf>

using json = nlohmann::json;

namespace xn {
json load_json_file(const std::string &filepath);
} // namespace xn