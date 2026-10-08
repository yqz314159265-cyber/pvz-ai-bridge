#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace pvz {
using json = nlohmann::json;
bool initialize(std::string& error);
json dispatch(const std::string& op, const json& args);
void shutdown();
}
