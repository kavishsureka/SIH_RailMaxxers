#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#ifdef SIH_WITH_ORTOOLS
#include "ortools/sat/cp_model.h"
#endif

namespace sih {
namespace {

using Clock = std::chrono::steady_clock;

bool overlap(int a0, int a1, int b0, int b1) { return a0 < b1 && b0 < a1; }

bool critical_task(const Task& task) {
  return task.mandatory || task.severity >= 9 || task.criticality >= 9;
}

int effective_latest_end(const Task& task) {
  int latest = std::min(task.latest_end_slot, kHorizonSlots);
  if (critical_task(task) && task.due_slot >= 0) latest = std::min(latest, task.due_slot);
  return latest;
}

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
      else quoted = !quoted;
    } else if (ch == ',' && !quoted) {
      fields.push_back(field); field.clear();
    } else field += ch;
  }
  fields.push_back(field);
  return fields;
}

template <typename Fn>
void read_csv(const std::filesystem::path& path, Fn fn) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open " + path.string());
  std::string line;
  std::getline(input, line);
  int row = 1;
  while (std::getline(input, line)) {
    ++row;
    if (line.empty() || line[0] == '#') continue;
    try { fn(split_csv(line)); }
    catch (const std::exception& e) {
      throw std::runtime_error(path.string() + ":" + std::to_string(row) + ": " + e.what());
    }
  }
}

bool boolean(const std::string& value) {
  return value == "1" || value == "true" || value == "TRUE" || value == "HARD";
}

std::string escape_json(const std::string& input) {
  std::ostringstream out;
  for (char ch : input) {
    if (ch == '"' || ch == '\\') out << '\\' << ch;
    else if (ch == '\n') out << "\\n";
    else out << ch;
  }
  return out.str();
}

const Task& task_by_id(const Dataset& data, const std::string& id) {
  const auto it = std::find_if(data.tasks.begin(), data.tasks.end(),
                               [&](const Task& t) { return t.id == id; });
  if (it == data.tasks.end()) throw std::runtime_error("unknown task " + id);
  return *it;
}

bool compatible(const Dataset& data, const Task& a, const Task& b) {
  auto key = std::minmax(a.type, b.type);
  auto it = data.compatibility.find({key.first, key.second});
  return it == data.compatibility.end() || it->second;
}

bool independently_hard_feasible(const Dataset& data, const Task& task, int start) {
  const int end = start + task.duration_slots;
  std::vector<CandidateWindow> availability;
  for (const auto& window : data.availability) {
    if (window.corridor_id == task.corridor_id) availability.push_back({window.start_slot, window.end_slot});
  }
  std::sort(availability.begin(), availability.end(), [](const auto& a, const auto& b) {
    return a.start_slot < b.start_slot ||
           (a.start_slot == b.start_slot && a.end_slot < b.end_slot);
  });
  int covered_until = start;
  for (const auto& window : availability) {
    if (window.end_slot <= covered_until) continue;
    if (window.start_slot > covered_until) break;
    covered_until = window.end_slot;
    if (covered_until >= end) break;
  }
  if (covered_until < end) return false;
  return std::none_of(data.trains.begin(), data.trains.end(), [&](const auto& train) {
    return train.hard_conflict && train.corridor_id == task.corridor_id &&
           overlap(start, end, train.start_slot, train.end_slot);
  });
}

bool is_candidate_start(const CandidateWindows& candidates, const Task& task, int start) {
  const auto it = candidates.find(task.id);
  if (it == candidates.end()) return false;
  const int end = start + task.duration_slots;
  return std::any_of(it->second.begin(), it->second.end(), [&](const auto& window) {
    return start >= window.start_slot && end <= window.end_slot;
  });
}

bool can_place(const Dataset& data, const CandidateWindows& candidates, const Task& task, int start,
               const std::vector<Placement>& placed, bool coordinate_departments) {
  const int end = start + task.duration_slots;
  if (!is_candidate_start(candidates, task, start)) return false;
  for (const auto& p : placed) {
    const auto& other = task_by_id(data, p.task_id);
    if (other.corridor_id == task.corridor_id && overlap(start, end, p.start_slot, p.end_slot)) {
      if (coordinate_departments && !compatible(data, task, other)) return false;
    }
  }
  for (const auto& dep : data.dependencies) {
    if (dep.successor_id == task.id) {
      auto pred = std::find_if(placed.begin(), placed.end(), [&](const auto& p) { return p.task_id == dep.predecessor_id; });
      if (pred == placed.end() || pred->end_slot + dep.min_lag_slots > start) return false;
    }
    if (dep.predecessor_id == task.id) {
      auto succ = std::find_if(placed.begin(), placed.end(), [&](const auto& p) { return p.task_id == dep.successor_id; });
      if (succ != placed.end() && end + dep.min_lag_slots > succ->start_slot) return false;
    }
  }
  return true;
}

int priority_score(const Task& t) {
  const int urgency = t.due_slot < kHorizonSlots ? std::max(0, kHorizonSlots - t.due_slot) / kSlotsPerDay : 0;
  return (critical_task(t) ? 10000 : 0) + 40 * t.severity + 25 * t.criticality + urgency;
}

std::vector<Task> ordered_tasks(const Dataset& data, std::optional<std::string> department = std::nullopt) {
  std::vector<Task> tasks;
  for (const auto& task : data.tasks) if (!department || task.department == *department) tasks.push_back(task);
  std::stable_sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
    return priority_score(a) > priority_score(b);
  });
  // A small topological repair keeps predecessors before their successors.
  for (const auto& dep : data.dependencies) {
    auto pred = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == dep.predecessor_id; });
    auto succ = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == dep.successor_id; });
    if (pred != tasks.end() && succ != tasks.end() && pred > succ) std::rotate(succ, pred, pred + 1);
  }
  return tasks;
}

std::vector<TaskTrace> build_task_traces(const Dataset& data,
                                         const std::vector<Placement>& placements);

Plan finalize(std::string algorithm, const Dataset& data, const Weights& weights,
              std::vector<Placement> placements, double preprocessing_ms, double algorithm_ms,
              Clock::time_point total_started,
              std::string status = "FEASIBLE", bool native = false) {
  Plan plan;
  plan.algorithm = std::move(algorithm);
  plan.solver_status = std::move(status);
  plan.native_cp_sat = native;
  plan.placements = std::move(placements);
  plan.blocks = derive_blocks(data, plan.placements);
  plan.task_traces = build_task_traces(data, plan.placements);
  plan.validation = validate(data, plan.placements);
  plan.metrics = calculate_metrics(data, plan.placements, plan.blocks, weights);
  plan.preprocessing_ms = preprocessing_ms;
  plan.algorithm_ms = algorithm_ms;
  plan.total_runtime_ms = std::chrono::duration<double, std::milli>(Clock::now() - total_started).count();
  return plan;
}

std::vector<int> candidate_starts(const CandidateWindows& candidates, const Task& task) {
  std::vector<int> starts;
  const auto it = candidates.find(task.id);
  if (it == candidates.end()) return starts;
  for (const auto& window : it->second) {
    for (int start = window.start_slot; start + task.duration_slots <= window.end_slot; start += 2) {
      starts.push_back(start);
    }
    const int latest = window.end_slot - task.duration_slots;
    if (latest >= window.start_slot && (starts.empty() || starts.back() != latest)) starts.push_back(latest);
  }
  return starts;
}

std::vector<Placement> greedy_schedule(const Dataset& data, const CandidateWindows& candidates,
                                       const Weights& weights,
                                       const std::vector<Task>& tasks, bool coordinate,
                                       bool optimize_cost,
                                       std::vector<Placement> initial = {}) {
  auto placed = std::move(initial);
  for (const auto& task : tasks) {
    std::optional<int> best_start;
    std::int64_t best_value{};
    const auto existing_blocks = optimize_cost ? derive_blocks(data, placed) : std::vector<Block>{};
    for (const int start : candidate_starts(candidates, task)) {
      if (!can_place(data, candidates, task, start, placed, coordinate)) continue;
      if (!optimize_cost) { best_start = start; break; }
      const int end = start + task.duration_slots;
      int new_active_slots = 0;
      int touching_blocks = 0;
      for (const auto& block : existing_blocks) {
        if (block.corridor_id != task.corridor_id) continue;
        if (block.start_slot <= end && start <= block.end_slot) ++touching_blocks;
      }
      for (int slot = start; slot < end; ++slot) {
        const bool covered = std::any_of(existing_blocks.begin(), existing_blocks.end(), [&](const auto& block) {
          return block.corridor_id == task.corridor_id && block.start_slot <= slot && slot < block.end_slot;
        });
        if (!covered) ++new_active_slots;
      }
      const int block_delta = new_active_slots == 0 ? 0 : 1 - touching_blocks;
      std::int64_t value = weights.block_count * block_delta +
                           weights.downtime_minute * new_active_slots * kSlotMinutes;
      for (const auto& train : data.trains) {
        if (train.hard_conflict || train.corridor_id != task.corridor_id ||
            !overlap(start, end, train.start_slot, train.end_slot)) continue;
        const bool already_impacted = std::any_of(existing_blocks.begin(), existing_blocks.end(), [&](const auto& block) {
          return block.corridor_id == train.corridor_id &&
                 overlap(block.start_slot, block.end_slot, train.start_slot, train.end_slot);
        });
        if (!already_impacted) value += weights.train_impact * train.impact_weight;
      }
      if (task.due_slot >= 0 && end > task.due_slot) {
        value += weights.lateness_minute * (end - task.due_slot) * kSlotMinutes;
        value += weights.deadline_violation;
      }
      if (!best_start || value < best_value) { best_start = start; best_value = value; }
    }
    if (best_start) placed.push_back({task.id, *best_start, *best_start + task.duration_slots});
  }
  return placed;
}

std::vector<Placement> departmental_schedule(const Dataset& data, const CandidateWindows& candidates,
                                              const Weights& weights) {
  std::vector<Placement> combined;
  std::unordered_set<std::string> scheduled;
  for (std::size_t pass = 0; pass < data.tasks.size(); ++pass) {
    const auto before = combined.size();
    for (const std::string department : {"ENGINEERING", "ST", "TRD"}) {
      auto pending = ordered_tasks(data, department);
      pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Task& task) {
        return scheduled.contains(task.id);
      }), pending.end());
      combined = greedy_schedule(data, candidates, weights, pending, true, false,
                                 std::move(combined));
      scheduled.clear();
      for (const auto& placement : combined) scheduled.insert(placement.task_id);
    }
    if (combined.size() == data.tasks.size() || combined.size() == before) break;
  }
  return combined;
}

std::vector<TaskTrace> build_task_traces(const Dataset& data,
                                         const std::vector<Placement>& placements) {
  const auto feasible = generate_candidate_windows(data);
  std::vector<TaskTrace> traces;
  traces.reserve(data.tasks.size());
  for (const auto& task : data.tasks) {
    TaskTrace trace{task.id, {}};
    const auto selected = std::find_if(placements.begin(), placements.end(), [&](const auto& placement) {
      return placement.task_id == task.id;
    });
    auto without = placements;
    without.erase(std::remove_if(without.begin(), without.end(), [&](const auto& placement) {
      return placement.task_id == task.id;
    }), without.end());
    const auto other_blocks = derive_blocks(data, without);
    auto evaluate = [&](int start, int end, std::string status) {
      CandidateEvaluation item{start, end, std::move(status), 0, 0, false};
      for (const auto& train : data.trains) {
        if (!train.hard_conflict && train.corridor_id == task.corridor_id &&
            overlap(start, end, train.start_slot, train.end_slot)) item.train_cost += train.impact_weight;
      }
      for (int slot = start; slot < end; ++slot) {
        const bool covered = std::any_of(other_blocks.begin(), other_blocks.end(), [&](const auto& block) {
          return block.corridor_id == task.corridor_id && block.start_slot <= slot && slot < block.end_slot;
        });
        if (!covered) item.added_downtime_minutes += kSlotMinutes;
      }
      item.block_reuse = std::any_of(other_blocks.begin(), other_blocks.end(), [&](const auto& block) {
        return block.corridor_id == task.corridor_id && block.start_slot <= end && start <= block.end_slot;
      });
      trace.candidates.push_back(std::move(item));
    };

    if (selected != placements.end()) evaluate(selected->start_slot, selected->end_slot, "SELECTED");
    const auto candidate_it = feasible.find(task.id);
    if (candidate_it != feasible.end()) {
      for (const auto& window : candidate_it->second) {
        for (const int start : {window.start_slot, window.end_slot - task.duration_slots}) {
          if (start < window.start_slot || start + task.duration_slots > window.end_slot) continue;
          if (selected != placements.end() && start == selected->start_slot) continue;
          const bool duplicate = std::any_of(trace.candidates.begin(), trace.candidates.end(), [&](const auto& item) {
            return item.start_slot == start && item.end_slot == start + task.duration_slots;
          });
          if (!duplicate && trace.candidates.size() < 5) evaluate(start, start + task.duration_slots, "FEASIBLE");
        }
        if (trace.candidates.size() >= 5) break;
      }
    }
    for (const auto& train : data.trains) {
      if (!train.hard_conflict || train.corridor_id != task.corridor_id || trace.candidates.size() >= 7) continue;
      const int start = std::max(task.earliest_slot, train.start_slot);
      const int end = std::min(effective_latest_end(task), std::max(train.end_slot, start + task.duration_slots));
      if (start < end) evaluate(start, end, "HARD_TRAIN_CONFLICT");
    }
    for (const auto& availability : data.availability) {
      if (availability.corridor_id != task.corridor_id || trace.candidates.size() >= 8) continue;
      const int start = std::max(task.earliest_slot, availability.start_slot);
      const int end = std::min(effective_latest_end(task), availability.end_slot);
      if (start < end && end - start < task.duration_slots) evaluate(start, end, "TOO_SHORT");
    }
    traces.push_back(std::move(trace));
  }
  return traces;
}

}  // namespace

Dataset load_dataset(const std::filesystem::path& dir) {
  Dataset data;
  read_csv(dir / "corridors.csv", [&](const auto& f) { data.corridors.push_back({f.at(0), f.at(1)}); });
  read_csv(dir / "tasks.csv", [&](const auto& f) {
    data.tasks.push_back({f.at(0), f.at(1), f.at(2), f.at(3), std::stoi(f.at(4)),
      std::stoi(f.at(5)), std::stoi(f.at(6)), std::stoi(f.at(7)), boolean(f.at(8)),
      boolean(f.at(9)), std::stoi(f.at(10)), std::stoi(f.at(11))});
  });
  read_csv(dir / "trains.csv", [&](const auto& f) {
    data.trains.push_back({f.at(0), f.at(1), std::stoi(f.at(2)), std::stoi(f.at(3)),
                           f.at(4) == "HARD", std::stoi(f.at(5))});
  });
  read_csv(dir / "availability.csv", [&](const auto& f) {
    data.availability.push_back({f.at(0), std::stoi(f.at(1)), std::stoi(f.at(2))});
  });
  read_csv(dir / "dependencies.csv", [&](const auto& f) {
    data.dependencies.push_back({f.at(0), f.at(1), std::stoi(f.at(2))});
  });
  read_csv(dir / "compatibility.csv", [&](const auto& f) {
    auto key = std::minmax(f.at(0), f.at(1));
    data.compatibility[{key.first, key.second}] = boolean(f.at(2));
  });
  if (data.corridors.empty() || data.tasks.empty()) throw std::runtime_error("dataset is empty");
  return data;
}

Weights load_weights(const std::filesystem::path& path) {
  Weights w;
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open " + path.string());
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    const auto key = line.substr(0, pos);
    const auto value = std::stoll(line.substr(pos + 1));
    if (key == "wB") w.block_count = value;
    else if (key == "wD") w.downtime_minute = value;
    else if (key == "wT") w.train_impact = value;
    else if (key == "wL") w.lateness_minute = value;
    else if (key == "wV") w.deadline_violation = value;
  }
  return w;
}

CandidateWindows generate_candidate_windows(const Dataset& data) {
  CandidateWindows result;
  for (const auto& task : data.tasks) {
    const int task_start = std::max(0, task.earliest_slot);
    const int task_end = effective_latest_end(task);
    auto& windows = result[task.id];
    if (task_end - task_start < task.duration_slots) continue;

    for (const auto& availability : data.availability) {
      if (availability.corridor_id != task.corridor_id) continue;
      const int available_start = std::max(task_start, availability.start_slot);
      const int available_end = std::min(task_end, availability.end_slot);
      if (available_end - available_start < task.duration_slots) continue;

      std::vector<CandidateWindow> forbidden;
      for (const auto& train : data.trains) {
        if (!train.hard_conflict || train.corridor_id != task.corridor_id) continue;
        const int start = std::max(available_start, train.start_slot);
        const int end = std::min(available_end, train.end_slot);
        if (start < end) forbidden.push_back({start, end});
      }
      std::sort(forbidden.begin(), forbidden.end(), [](const auto& a, const auto& b) {
        return a.start_slot < b.start_slot ||
               (a.start_slot == b.start_slot && a.end_slot < b.end_slot);
      });
      std::vector<CandidateWindow> merged;
      for (const auto& interval : forbidden) {
        if (merged.empty() || interval.start_slot > merged.back().end_slot) merged.push_back(interval);
        else merged.back().end_slot = std::max(merged.back().end_slot, interval.end_slot);
      }

      int cursor = available_start;
      for (const auto& interval : merged) {
        if (interval.start_slot - cursor >= task.duration_slots) windows.push_back({cursor, interval.start_slot});
        cursor = std::max(cursor, interval.end_slot);
      }
      if (available_end - cursor >= task.duration_slots) windows.push_back({cursor, available_end});
    }

    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) {
      return a.start_slot < b.start_slot ||
             (a.start_slot == b.start_slot && a.end_slot < b.end_slot);
    });
    std::vector<CandidateWindow> merged_free;
    for (const auto& window : windows) {
      if (merged_free.empty() || window.start_slot > merged_free.back().end_slot) merged_free.push_back(window);
      else merged_free.back().end_slot = std::max(merged_free.back().end_slot, window.end_slot);
    }
    windows = std::move(merged_free);
  }
  return result;
}

std::vector<Block> derive_blocks(const Dataset& data, const std::vector<Placement>& placements) {
  std::vector<Block> blocks;
  for (const auto& corridor : data.corridors) {
    std::vector<bool> active(kHorizonSlots, false);
    for (const auto& p : placements) {
      const auto& task = task_by_id(data, p.task_id);
      if (task.corridor_id != corridor.id) continue;
      for (int slot = std::max(0, p.start_slot); slot < std::min(kHorizonSlots, p.end_slot); ++slot) active[slot] = true;
    }
    for (int slot = 0; slot < kHorizonSlots;) {
      if (!active[slot]) { ++slot; continue; }
      int end = slot + 1;
      while (end < kHorizonSlots && active[end]) ++end;
      blocks.push_back({corridor.id, slot, end});
      slot = end;
    }
  }
  return blocks;
}

ValidationResult validate(const Dataset& data, const std::vector<Placement>& placements) {
  ValidationResult result{true, {}};
  std::unordered_map<std::string, Placement> placed;
  auto fail = [&](std::string message) { result.valid = false; result.violations.push_back(std::move(message)); };
  for (const auto& p : placements) {
    if (placed.contains(p.task_id)) { fail("task " + p.task_id + " scheduled more than once"); continue; }
    placed[p.task_id] = p;
    const auto& task = task_by_id(data, p.task_id);
    if (p.end_slot - p.start_slot != task.duration_slots) fail("task " + task.id + " has incorrect duration");
    if (p.start_slot < task.earliest_slot || p.end_slot > effective_latest_end(task)) fail("task " + task.id + " outside task window");
    if (!independently_hard_feasible(data, task, p.start_slot)) fail("task " + task.id + " violates availability or a HARD train interval");
    for (const auto& train : data.trains) {
      if (train.corridor_id == task.corridor_id && overlap(p.start_slot, p.end_slot, train.start_slot, train.end_slot) &&
          train.hard_conflict) {
        fail("task " + task.id + " conflicts with train " + train.id);
      }
    }
  }
  for (const auto& task : data.tasks) {
    if (!placed.contains(task.id)) fail("monthly task " + task.id + " is unscheduled");
  }
  for (std::size_t i = 0; i < placements.size(); ++i) for (std::size_t j = i + 1; j < placements.size(); ++j) {
    const auto& a = task_by_id(data, placements[i].task_id);
    const auto& b = task_by_id(data, placements[j].task_id);
    if (a.corridor_id == b.corridor_id && overlap(placements[i].start_slot, placements[i].end_slot,
                                                   placements[j].start_slot, placements[j].end_slot) &&
        !compatible(data, a, b)) fail("incompatible tasks " + a.id + " and " + b.id + " overlap");
  }
  for (const auto& dep : data.dependencies) {
    const auto succ = placed.find(dep.successor_id);
    if (succ == placed.end()) continue;
    const auto pred = placed.find(dep.predecessor_id);
    if (pred == placed.end()) fail("dependency predecessor " + dep.predecessor_id + " is unscheduled");
    else if (pred->second.end_slot + dep.min_lag_slots > succ->second.start_slot)
      fail("dependency " + dep.predecessor_id + " -> " + dep.successor_id + " violates precedence");
  }
  return result;
}

Metrics calculate_metrics(const Dataset& data, const std::vector<Placement>& placements,
                          const std::vector<Block>& blocks, const Weights& w) {
  Metrics m;
  m.block_count = static_cast<int>(blocks.size());
  for (const auto& block : blocks) m.downtime_minutes += (block.end_slot - block.start_slot) * kSlotMinutes;
  for (const auto& train : data.trains) {
    if (train.hard_conflict) continue;
    const bool impacted = std::any_of(blocks.begin(), blocks.end(), [&](const auto& b) {
      return b.corridor_id == train.corridor_id && overlap(b.start_slot, b.end_slot, train.start_slot, train.end_slot);
    });
    if (impacted) m.train_impact += train.impact_weight;
  }
  std::unordered_set<std::string> scheduled;
  for (const auto& p : placements) scheduled.insert(p.task_id);
  m.scheduled_tasks = static_cast<int>(scheduled.size());
  m.total_tasks = static_cast<int>(data.tasks.size());
  for (const auto& task : data.tasks) {
    const bool critical = critical_task(task);
    if (critical) ++m.critical_total;
    const auto placement = std::find_if(placements.begin(), placements.end(), [&](const auto& p) {
      return p.task_id == task.id;
    });
    if (placement == placements.end()) continue;
    if (critical) ++m.critical_completed;
    if (task.due_slot >= 0 && placement->end_slot > task.due_slot) {
      ++m.deadline_violations;
      m.lateness_minutes += (placement->end_slot - task.due_slot) * kSlotMinutes;
    }
  }
  m.objective = w.block_count * m.block_count + w.downtime_minute * m.downtime_minutes +
                w.train_impact * m.train_impact + w.lateness_minute * m.lateness_minutes +
                w.deadline_violation * m.deadline_violations;
  return m;
}

Plan solve_independent(const Dataset& data, const Weights& weights) {
  const auto total_started = Clock::now();
  const auto preprocessing_started = Clock::now();
  const auto candidates = generate_candidate_windows(data);
  const double preprocessing_ms = std::chrono::duration<double, std::milli>(Clock::now() - preprocessing_started).count();
  const auto algorithm_started = Clock::now();
  auto combined = departmental_schedule(data, candidates, weights);
  const double algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - algorithm_started).count();
  return finalize("independent", data, weights, std::move(combined), preprocessing_ms, algorithm_ms,
                  total_started);
}

Plan solve_greedy(const Dataset& data, const Weights& weights) {
  const auto total_started = Clock::now();
  const auto preprocessing_started = Clock::now();
  const auto candidates = generate_candidate_windows(data);
  const double preprocessing_ms = std::chrono::duration<double, std::milli>(Clock::now() - preprocessing_started).count();
  const auto algorithm_started = Clock::now();
  auto placements = greedy_schedule(data, candidates, weights, ordered_tasks(data), true, true);
  if (placements.size() != data.tasks.size()) placements = departmental_schedule(data, candidates, weights);
  const double algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - algorithm_started).count();
  return finalize("greedy", data, weights, std::move(placements), preprocessing_ms, algorithm_ms,
                  total_started);
}

#ifdef SIH_WITH_ORTOOLS
Plan solve_cp_sat(const Dataset& data, const Weights& weights, int time_limit_seconds) {
  using namespace operations_research;
  using namespace operations_research::sat;
  const auto total_started = Clock::now();
  const auto preprocessing_started = Clock::now();
  const auto candidates = generate_candidate_windows(data);
  const double preprocessing_ms = std::chrono::duration<double, std::milli>(Clock::now() - preprocessing_started).count();
  const auto algorithm_started = Clock::now();
  CpModelBuilder model;
  const int n = static_cast<int>(data.tasks.size());
  auto incumbent = greedy_schedule(data, candidates, weights, ordered_tasks(data), true, true);
  if (incumbent.size() != data.tasks.size()) incumbent = departmental_schedule(data, candidates, weights);
  std::unordered_map<std::string, int> hinted_start;
  for (const auto& placement : incumbent) hinted_start[placement.task_id] = placement.start_slot;
  std::vector<std::vector<int>> starts(n);
  std::vector<std::vector<BoolVar>> x(n);
  std::vector<IntVar> start_vars;

  for (int i = 0; i < n; ++i) {
    const auto& task = data.tasks[i];
    start_vars.push_back(model.NewIntVar(Domain(0, kHorizonSlots)));
    LinearExpr start_expr;
    for (const int s : candidate_starts(candidates, task)) {
      starts[i].push_back(s);
      x[i].push_back(model.NewBoolVar());
      const auto hint = hinted_start.find(task.id);
      if (hint != hinted_start.end() && hint->second == s) model.AddHint(x[i].back(), true);
      start_expr += s * x[i].back();
    }
    LinearExpr sum;
    for (const auto& var : x[i]) sum += var;
    model.AddEquality(sum, 1);
    model.AddEquality(start_vars[i], start_expr);
  }

  std::unordered_map<std::string, int> corridor_index;
  for (std::size_t i = 0; i < data.corridors.size(); ++i) corridor_index[data.corridors[i].id] = static_cast<int>(i);
  std::vector<std::vector<std::vector<BoolVar>>> active_vars(
      data.corridors.size(), std::vector<std::vector<BoolVar>>(kHorizonSlots));
  for (int i = 0; i < n; ++i) {
    const int corridor = corridor_index.at(data.tasks[i].corridor_id);
    for (std::size_t k = 0; k < starts[i].size(); ++k) {
      for (int t = starts[i][k]; t < starts[i][k] + data.tasks[i].duration_slots; ++t) {
        active_vars[corridor][t].push_back(x[i][k]);
      }
    }
  }

  std::map<std::pair<std::string, int>, BoolVar> block;
  std::map<std::pair<std::string, int>, BoolVar> block_start;
  for (std::size_t corridor_index_value = 0; corridor_index_value < data.corridors.size(); ++corridor_index_value) {
    const auto& corridor = data.corridors[corridor_index_value];
    for (int t = 0; t < kHorizonSlots; ++t) {
    auto b = model.NewBoolVar(); block[{corridor.id, t}] = b;
    LinearExpr active_expr;
    for (const auto& variable : active_vars[corridor_index_value][t]) {
      active_expr += variable;
      model.AddGreaterOrEqual(b, variable);
    }
    model.AddLessOrEqual(b, active_expr);
    auto q = model.NewBoolVar(); block_start[{corridor.id, t}] = q;
    if (t == 0) model.AddEquality(q, b);
    else {
      const auto prev = block.at({corridor.id, t - 1});
      model.AddGreaterOrEqual(q, LinearExpr(b) - prev);
      model.AddLessOrEqual(q, b);
      model.AddLessOrEqual(q, LinearExpr(1) - prev);
    }
    }
  }

  for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
    if (data.tasks[i].corridor_id != data.tasks[j].corridor_id || compatible(data, data.tasks[i], data.tasks[j])) continue;
    const auto i_before_j = model.NewBoolVar();
    model.AddLessOrEqual(start_vars[i] + data.tasks[i].duration_slots, start_vars[j]).OnlyEnforceIf(i_before_j);
    model.AddLessOrEqual(start_vars[j] + data.tasks[j].duration_slots, start_vars[i]).OnlyEnforceIf(Not(i_before_j));
  }
  std::unordered_map<std::string, int> index;
  for (int i = 0; i < n; ++i) index[data.tasks[i].id] = i;
  for (const auto& dep : data.dependencies) {
    const int p = index.at(dep.predecessor_id), s = index.at(dep.successor_id);
    model.AddLessOrEqual(start_vars[p] + data.tasks[p].duration_slots + dep.min_lag_slots, start_vars[s]);
  }

  LinearExpr objective;
  for (const auto& [key, q] : block_start) objective += weights.block_count * q;
  for (const auto& [key, b] : block) objective += weights.downtime_minute * kSlotMinutes * b;
  for (const auto& train : data.trains) if (!train.hard_conflict) {
    auto impacted = model.NewBoolVar();
    LinearExpr overlap_sum;
    for (int t = train.start_slot; t < train.end_slot; ++t) overlap_sum += block.at({train.corridor_id, t});
    model.AddLessOrEqual(impacted, overlap_sum);
    for (int t = train.start_slot; t < train.end_slot; ++t) model.AddGreaterOrEqual(impacted, block.at({train.corridor_id, t}));
    objective += weights.train_impact * train.impact_weight * impacted;
  }
  for (int i = 0; i < n; ++i) {
    if (data.tasks[i].due_slot < 0 || data.tasks[i].due_slot >= kHorizonSlots) continue;
    const auto lateness = model.NewIntVar(Domain(0, kHorizonSlots));
    model.AddMaxEquality(lateness, {start_vars[i] + data.tasks[i].duration_slots - data.tasks[i].due_slot, 0});
    const auto violation = model.NewBoolVar();
    model.AddGreaterOrEqual(lateness, 1).OnlyEnforceIf(violation);
    model.AddEquality(lateness, 0).OnlyEnforceIf(Not(violation));
    objective += weights.lateness_minute * kSlotMinutes * lateness;
    objective += weights.deadline_violation * violation;
  }
  model.Minimize(objective);
  Model solver;
  SatParameters parameters;
  parameters.set_max_time_in_seconds(time_limit_seconds);
  parameters.set_num_search_workers(8);
  parameters.set_random_seed(26027);
  solver.Add(NewSatParameters(parameters));
  const CpSolverResponse response = SolveCpModel(model.Build(), &solver);
  const double algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - algorithm_started).count();
  std::vector<Placement> placements;
  if (response.status() == CpSolverStatus::OPTIMAL || response.status() == CpSolverStatus::FEASIBLE) {
    for (int i = 0; i < n; ++i) for (std::size_t k = 0; k < x[i].size(); ++k) if (SolutionBooleanValue(response, x[i][k]))
      placements.push_back({data.tasks[i].id, starts[i][k], starts[i][k] + data.tasks[i].duration_slots});
  }
  const std::string status = response.status() == CpSolverStatus::OPTIMAL ? "OPTIMAL" :
                             response.status() == CpSolverStatus::FEASIBLE ? "FEASIBLE" :
                             response.status() == CpSolverStatus::INFEASIBLE ? "INFEASIBLE" : "UNKNOWN";
  return finalize("cp-sat", data, weights, std::move(placements), preprocessing_ms, algorithm_ms,
                  total_started, status, true);
}
#else
Plan solve_cp_sat(const Dataset& data, const Weights& weights, int) {
  // Portable fallback keeps the entire prototype runnable without a local OR-Tools C++ package.
  // Enabling SIH_WITH_ORTOOLS switches this exact API to the native model above.
  const auto total_started = Clock::now();
  const auto preprocessing_started = Clock::now();
  const auto candidates = generate_candidate_windows(data);
  const double preprocessing_ms = std::chrono::duration<double, std::milli>(Clock::now() - preprocessing_started).count();
  const auto algorithm_started = Clock::now();
  auto placements = greedy_schedule(data, candidates, weights, ordered_tasks(data), true, true);
  if (placements.size() != data.tasks.size()) placements = departmental_schedule(data, candidates, weights);
  auto best_blocks = derive_blocks(data, placements);
  auto best = calculate_metrics(data, placements, best_blocks, weights).objective;
  for (std::size_t i = 0; i < placements.size(); ++i) {
    const auto original = placements[i];
    const auto& task = task_by_id(data, original.task_id);
    auto without = placements; without.erase(without.begin() + static_cast<long>(i));
    for (const int s : candidate_starts(candidates, task)) {
      if (!can_place(data, candidates, task, s, without, true)) continue;
      auto trial = without; trial.push_back({task.id, s, s + task.duration_slots});
      const auto value = calculate_metrics(data, trial, derive_blocks(data, trial), weights).objective;
      if (value < best) { placements = std::move(trial); best = value; break; }
    }
  }
  const double algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - algorithm_started).count();
  return finalize("cp-sat", data, weights, std::move(placements), preprocessing_ms, algorithm_ms,
                  total_started, "FALLBACK_FEASIBLE", false);
}
#endif

std::string plan_json(const Plan& p) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << "{\"algorithm\":\"" << escape_json(p.algorithm) << "\",\"solver_status\":\"" << p.solver_status
      << "\",\"preprocessing_ms\":" << p.preprocessing_ms
      << ",\"algorithm_ms\":" << p.algorithm_ms
      << ",\"total_runtime_ms\":" << p.total_runtime_ms
      << ",\"runtime_ms\":" << p.total_runtime_ms
      << ",\"native_cp_sat\":" << (p.native_cp_sat ? "true" : "false")
      << ",\"validation\":{\"valid\":" << (p.validation.valid ? "true" : "false") << ",\"violations\":[";
  for (std::size_t i = 0; i < p.validation.violations.size(); ++i) {
    if (i) out << ','; out << '"' << escape_json(p.validation.violations[i]) << '"';
  }
  out << "]},\"metrics\":{\"objective\":" << p.metrics.objective << ",\"block_count\":" << p.metrics.block_count
      << ",\"downtime_minutes\":" << p.metrics.downtime_minutes << ",\"train_impact\":" << p.metrics.train_impact
      << ",\"lateness_minutes\":" << p.metrics.lateness_minutes
      << ",\"deadline_violations\":" << p.metrics.deadline_violations
      << ",\"scheduled_tasks\":" << p.metrics.scheduled_tasks
      << ",\"total_tasks\":" << p.metrics.total_tasks
      << ",\"critical_completed\":" << p.metrics.critical_completed
      << ",\"critical_total\":" << p.metrics.critical_total << "},\"placements\":[";
  for (std::size_t i = 0; i < p.placements.size(); ++i) {
    if (i) out << ','; const auto& x = p.placements[i];
    out << "{\"task_id\":\"" << escape_json(x.task_id) << "\",\"start_slot\":" << x.start_slot << ",\"end_slot\":" << x.end_slot << '}';
  }
  out << "],\"blocks\":[";
  for (std::size_t i = 0; i < p.blocks.size(); ++i) {
    if (i) out << ','; const auto& b = p.blocks[i];
    out << "{\"id\":\"B-" << std::setfill('0') << std::setw(3) << (i + 1) << std::setfill(' ')
        << "\",\"corridor_id\":\"" << escape_json(b.corridor_id) << "\",\"start_slot\":" << b.start_slot << ",\"end_slot\":" << b.end_slot << '}';
  }
  out << "],\"task_traces\":[";
  for (std::size_t i = 0; i < p.task_traces.size(); ++i) {
    if (i) out << ',';
    out << "{\"task_id\":\"" << escape_json(p.task_traces[i].task_id) << "\",\"candidates\":[";
    for (std::size_t j = 0; j < p.task_traces[i].candidates.size(); ++j) {
      if (j) out << ',';
      const auto& candidate = p.task_traces[i].candidates[j];
      out << "{\"start_slot\":" << candidate.start_slot << ",\"end_slot\":" << candidate.end_slot
          << ",\"status\":\"" << candidate.status << "\",\"train_cost\":" << candidate.train_cost
          << ",\"added_downtime_minutes\":" << candidate.added_downtime_minutes
          << ",\"block_reuse\":" << (candidate.block_reuse ? "true" : "false") << '}';
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

std::string benchmark_json(const std::vector<Plan>& plans) {
  std::ostringstream out; out << "{\"horizon_days\":" << kHorizonDays
                              << ",\"horizon_weeks\":" << kHorizonWeeks
                              << ",\"horizon_slots\":" << kHorizonSlots
                              << ",\"slot_minutes\":" << kSlotMinutes << ",\"plans\":[";
  for (std::size_t i = 0; i < plans.size(); ++i) { if (i) out << ','; out << plan_json(plans[i]); }
  out << "]}"; return out.str();
}

}  // namespace sih
