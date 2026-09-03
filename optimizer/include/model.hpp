#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sih {

constexpr int kSlotMinutes = 15;
constexpr int kSlotsPerDay = 24 * 4;
constexpr int kHorizonDays = 28;
constexpr int kHorizonWeeks = 4;
constexpr int kHorizonSlots = kHorizonDays * kSlotsPerDay;

struct Corridor { std::string id; std::string name; };

struct Task {
  std::string id;
  std::string corridor_id;
  std::string department;
  std::string type;
  int duration_slots{};
  int severity{};
  int criticality{};
  int due_slot{};
  bool mandatory{};
  bool requires_power_block{};
  int earliest_slot{};
  int latest_end_slot{kHorizonSlots};
  double priority_score{};
};

struct TrainMovement {
  std::string id;
  std::string corridor_id;
  int start_slot{};
  int end_slot{};
  bool hard_conflict{};
  int impact_weight{};
};

struct AvailabilityWindow {
  std::string corridor_id;
  int start_slot{};
  int end_slot{};
};

struct CandidateWindow {
  int start_slot{};
  int end_slot{};
};

using CandidateWindows = std::map<std::string, std::vector<CandidateWindow>>;

struct Dependency {
  std::string predecessor_id;
  std::string successor_id;
  int min_lag_slots{};
};

struct Placement { std::string task_id; int start_slot{}; int end_slot{}; };
struct Block { std::string corridor_id; int start_slot{}; int end_slot{}; };

struct CandidateEvaluation {
  int start_slot{};
  int end_slot{};
  std::string status;
  int train_cost{};
  int added_downtime_minutes{};
  bool block_reuse{};
};

struct TaskTrace {
  std::string task_id;
  std::vector<CandidateEvaluation> candidates;
};

struct Weights {
  std::int64_t block_count{400};
  std::int64_t downtime_minute{2};
  std::int64_t train_impact{25};
  std::int64_t lateness_minute{5};
  std::int64_t deadline_violation{5000};
  std::int64_t priority_weighted_delay{1};
};

struct Dataset {
  std::vector<Corridor> corridors;
  std::vector<Task> tasks;
  std::vector<TrainMovement> trains;
  std::vector<AvailabilityWindow> availability;
  std::vector<Dependency> dependencies;
  std::map<std::pair<std::string, std::string>, bool> compatibility;
};

struct Metrics {
  int block_count{};
  int downtime_minutes{};
  int train_impact{};
  int lateness_minutes{};
  int deadline_violations{};
  std::int64_t priority_weighted_delay{};
  int scheduled_tasks{};
  int total_tasks{};
  int critical_completed{};
  int critical_total{};
  std::int64_t objective{};
};

struct ValidationResult { bool valid{}; std::vector<std::string> violations; };

struct Plan {
  std::string algorithm;
  std::string solver_status{"FEASIBLE"};
  double preprocessing_ms{};
  double algorithm_ms{};
  double total_runtime_ms{};
  bool native_cp_sat{};
  std::vector<Placement> placements;
  std::vector<Block> blocks;
  std::vector<TaskTrace> task_traces;
  Metrics metrics;
  ValidationResult validation;
};

}  // namespace sih
