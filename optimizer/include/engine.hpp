#pragma once

#include "model.hpp"
#include <filesystem>
#include <string>

namespace sih {

Dataset load_dataset(const std::filesystem::path& directory);
Weights load_weights(const std::filesystem::path& config);
std::vector<Block> derive_blocks(const Dataset& data, const std::vector<Placement>& placements);
ValidationResult validate(const Dataset& data, const std::vector<Placement>& placements);
Metrics calculate_metrics(const Dataset& data, const std::vector<Placement>& placements,
                          const std::vector<Block>& blocks, const Weights& weights);
Plan solve_independent(const Dataset& data, const Weights& weights);
Plan solve_greedy(const Dataset& data, const Weights& weights);
Plan solve_cp_sat(const Dataset& data, const Weights& weights, int time_limit_seconds);
std::string plan_json(const Plan& plan);
std::string benchmark_json(const std::vector<Plan>& plans);

}  // namespace sih

