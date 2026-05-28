#include <wchar.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>

#include "aliasing.h"
#include "level3_parser_threaded.h"
#include "orderbook_delta.h"
#include "orderbook_entry.h"

const std::filesystem::path test_path{
    R"(C:\Users\pawel\Desktop\Programowanie\trading_system\example_responses\orderbook\level3.txt)"};

int main() {
  std::ifstream file{test_path, std::ios::in};
  std::string contents{(std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>()};

  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << test_path << '\n';
    return 1;
  }

  Level3ParserThreaded eq_threaded;
  eq_threaded.Parse(contents);
  std::println("{}", eq_threaded.delta().trunc());

  return 0;
};
