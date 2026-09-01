#include "engine.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string option(const std::vector<std::string>& args, const std::string& name,
                   const std::string& fallback = "") {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return args[i + 1];
  }
  return fallback;
}
}

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
      std::cerr << "usage: sih-optimizer <independent|greedy|cp-sat|benchmark> --data DIR --config FILE\n";
      return 2;
    }
    const auto data_dir = option(args, "--data", "data/demo");
    const auto config = option(args, "--config", "config/optimizer.conf");
    const int time_limit = std::stoi(option(args, "--time-limit", "10"));
    const auto data = sih::load_dataset(data_dir);
    const auto weights = sih::load_weights(config);

    if (args[0] == "independent") std::cout << sih::plan_json(sih::solve_independent(data, weights));
    else if (args[0] == "greedy") std::cout << sih::plan_json(sih::solve_greedy(data, weights));
    else if (args[0] == "cp-sat") std::cout << sih::plan_json(sih::solve_cp_sat(data, weights, time_limit));
    else if (args[0] == "benchmark") {
      std::cout << sih::benchmark_json({sih::solve_independent(data, weights),
                                        sih::solve_greedy(data, weights),
                                        sih::solve_cp_sat(data, weights, time_limit)});
    } else {
      throw std::runtime_error("unknown command: " + args[0]);
    }
    std::cout << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "optimizer error: " << error.what() << '\n';
    return 1;
  }
}
