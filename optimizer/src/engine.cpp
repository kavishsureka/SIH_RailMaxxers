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

bool fits_availability(const Dataset& data, const Task& task, int start) {
  const int end = start + task.duration_slots;
  return std::any_of(data.availability.begin(), data.availability.end(), [&](const auto& window) {
    return window.corridor_id == task.corridor_id && start >= window.start_slot && end <= window.end_slot;
  });
}

bool conflicts_with_train(const Dataset& data, const Task& task, int start) {
  const int end = start + task.duration_slots;
  return std::any_of(data.trains.begin(), data.trains.end(), [&](const auto& train) {
    if (train.corridor_id != task.corridor_id || !overlap(start, end, train.start_slot, train.end_slot)) return false;
    return train.hard_conflict || task.requires_power_block;
  });
}

bool compatible(const Dataset& data, const Task& a, const Task& b) {
  auto key = std::minmax(a.type, b.type);
  auto it = data.compatibility.find({key.first, key.second});
  return it == data.compatibility.end() || it->second;
}

bool can_place(const Dataset& data, const Task& task, int start,
               const std::vector<Placement>& placed, bool coordinate_departments) {
  const int end = start + task.duration_slots;
  if (start < task.earliest_slot || end > task.latest_end_slot || start < 0 || end > kHorizonSlots) return false;
  if (!fits_availability(data, task, start) || conflicts_with_train(data, task, start)) return false;
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
  const int overdue = t.due_slot < kHorizonSlots ? std::min(100, (kHorizonSlots - t.due_slot) / 3) : 0;
  return (t.mandatory ? 10000 : 0) + 40 * t.severity + 25 * t.criticality + overdue;
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

std::int64_t unscheduled_cost(const Task& task, const Weights& w) {
  std::int64_t cost = 0;
  if (task.due_slot < kHorizonSlots) {
    const int overdue_days = std::max(1, (kHorizonSlots - task.due_slot + 95) / 96);
    cost += w.overdue_penalty * overdue_days * task.severity;
  }
  if (task.severity >= 9 || task.criticality >= 9 || task.mandatory) cost += w.critical_noncompletion;
  return cost;
}

Plan finalize(std::string algorithm, const Dataset& data, const Weights& weights,
              std::vector<Placement> placements, Clock::time_point started,
              std::string status = "FEASIBLE", bool native = false) {
  Plan plan;
  plan.algorithm = std::move(algorithm);
  plan.solver_status = std::move(status);
  plan.native_cp_sat = native;
  plan.placements = std::move(placements);
  plan.blocks = derive_blocks(data, plan.placements);
  plan.validation = validate(data, plan.placements);
  plan.metrics = calculate_metrics(data, plan.placements, plan.blocks, weights);
  plan.runtime_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  return plan;
}

std::vector<Placement> greedy_schedule(const Dataset& data, const Weights& weights,
                                       const std::vector<Task>& tasks, bool coordinate,
                                       std::vector<Placement> initial = {}) {
  auto placed = std::move(initial);
  for (const auto& task : tasks) {
    std::optional<int> best_start;
    std::int64_t best_value = unscheduled_cost(task, weights);
    for (int start = task.earliest_slot; start + task.duration_slots <= task.latest_end_slot; ++start) {
      if (!can_place(data, task, start, placed, coordinate)) continue;
      auto trial = placed;
      trial.push_back({task.id, start, start + task.duration_slots});
      auto blocks = derive_blocks(data, trial);
      auto metrics = calculate_metrics(data, trial, blocks, weights);
      // Evaluate the total plan including penalties for every task still absent.
      std::unordered_set<std::string> scheduled;
      for (const auto& p : trial) scheduled.insert(p.task_id);
      std::int64_t value = metrics.objective;
      for (const auto& candidate : data.tasks) if (!scheduled.contains(candidate.id)) value += unscheduled_cost(candidate, weights);
      if (!best_start || value < best_value) { best_start = start; best_value = value; }
    }
    if (best_start) placed.push_back({task.id, *best_start, *best_start + task.duration_slots});
  }
  return placed;
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
    else if (key == "wO") w.overdue_penalty = value;
    else if (key == "wC") w.critical_noncompletion = value;
  }
  return w;
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
    if (p.start_slot < task.earliest_slot || p.end_slot > task.latest_end_slot) fail("task " + task.id + " outside task window");
    if (!fits_availability(data, task, p.start_slot)) fail("task " + task.id + " outside corridor availability");
    for (const auto& train : data.trains) {
      if (train.corridor_id == task.corridor_id && overlap(p.start_slot, p.end_slot, train.start_slot, train.end_slot) &&
          (train.hard_conflict || task.requires_power_block)) {
        fail("task " + task.id + " conflicts with train " + train.id);
      }
    }
  }
  for (const auto& task : data.tasks) if (task.mandatory && !placed.contains(task.id)) fail("mandatory task " + task.id + " is unscheduled");
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
  for (const auto& task : data.tasks) {
    const bool critical = task.severity >= 9 || task.criticality >= 9 || task.mandatory;
    if (scheduled.contains(task.id)) { if (critical) ++m.critical_completed; continue; }
    if (task.due_slot < kHorizonSlots) m.overdue_penalty += std::max(1, (kHorizonSlots - task.due_slot + 95) / 96) * task.severity;
    if (critical) ++m.critical_noncompletion;
  }
  m.objective = w.block_count * m.block_count + w.downtime_minute * m.downtime_minutes +
                w.train_impact * m.train_impact + w.overdue_penalty * m.overdue_penalty +
                w.critical_noncompletion * m.critical_noncompletion;
  return m;
}

Plan solve_independent(const Dataset& data, const Weights& weights) {
  const auto started = Clock::now();
  std::vector<Placement> combined;
  for (const std::string department : {"ENGINEERING", "ST", "TRD"}) {
    auto departmental = greedy_schedule(data, weights, ordered_tasks(data, department), false);
    combined.insert(combined.end(), departmental.begin(), departmental.end());
  }
  return finalize("independent", data, weights, std::move(combined), started);
}

Plan solve_greedy(const Dataset& data, const Weights& weights) {
  const auto started = Clock::now();
  auto placements = greedy_schedule(data, weights, ordered_tasks(data), true);
  return finalize("greedy", data, weights, std::move(placements), started);
}

#ifdef SIH_WITH_ORTOOLS
Plan solve_cp_sat(const Dataset& data, const Weights& weights, int time_limit_seconds) {
  using namespace operations_research;
  using namespace operations_research::sat;
  const auto started = Clock::now();
  CpModelBuilder model;
  const int n = static_cast<int>(data.tasks.size());
  std::vector<std::vector<int>> starts(n);
  std::vector<std::vector<BoolVar>> x(n);
  std::vector<BoolVar> scheduled;
  std::vector<IntVar> start_vars;

  for (int i = 0; i < n; ++i) {
    const auto& task = data.tasks[i];
    scheduled.push_back(model.NewBoolVar());
    start_vars.push_back(model.NewIntVar(Domain(0, kHorizonSlots)));
    LinearExpr start_expr;
    for (int s = task.earliest_slot; s + task.duration_slots <= task.latest_end_slot; ++s) {
      if (!fits_availability(data, task, s) || conflicts_with_train(data, task, s)) continue;
      starts[i].push_back(s);
      x[i].push_back(model.NewBoolVar());
      start_expr += s * x[i].back();
    }
    LinearExpr sum;
    for (const auto& var : x[i]) sum += var;
    model.AddEquality(sum, scheduled[i]);
    model.AddEquality(start_vars[i], start_expr);
    if (task.mandatory) model.AddEquality(scheduled[i], 1);
  }

  std::map<std::pair<std::string, int>, BoolVar> block;
  std::map<std::pair<std::string, int>, BoolVar> block_start;
  for (const auto& corridor : data.corridors) for (int t = 0; t < kHorizonSlots; ++t) {
    auto b = model.NewBoolVar(); block[{corridor.id, t}] = b;
    LinearExpr active;
    for (int i = 0; i < n; ++i) if (data.tasks[i].corridor_id == corridor.id) {
      for (std::size_t k = 0; k < starts[i].size(); ++k) if (starts[i][k] <= t && t < starts[i][k] + data.tasks[i].duration_slots) {
        active += x[i][k]; model.AddGreaterOrEqual(b, x[i][k]);
      }
    }
    model.AddLessOrEqual(b, active);
    auto q = model.NewBoolVar(); block_start[{corridor.id, t}] = q;
    if (t == 0) model.AddEquality(q, b);
    else {
      const auto prev = block.at({corridor.id, t - 1});
      model.AddGreaterOrEqual(q, LinearExpr(b) - prev);
      model.AddLessOrEqual(q, b);
      model.AddLessOrEqual(q, LinearExpr(1) - prev);
    }
  }

  for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
    if (data.tasks[i].corridor_id != data.tasks[j].corridor_id || compatible(data, data.tasks[i], data.tasks[j])) continue;
    for (int t = 0; t < kHorizonSlots; ++t) {
      LinearExpr active;
      for (std::size_t k = 0; k < starts[i].size(); ++k) if (starts[i][k] <= t && t < starts[i][k] + data.tasks[i].duration_slots) active += x[i][k];
      for (std::size_t k = 0; k < starts[j].size(); ++k) if (starts[j][k] <= t && t < starts[j][k] + data.tasks[j].duration_slots) active += x[j][k];
      model.AddLessOrEqual(active, 1);
    }
  }
  std::unordered_map<std::string, int> index;
  for (int i = 0; i < n; ++i) index[data.tasks[i].id] = i;
  for (const auto& dep : data.dependencies) {
    const int p = index.at(dep.predecessor_id), s = index.at(dep.successor_id);
    model.AddLessOrEqual(scheduled[s], scheduled[p]);
    model.AddLessOrEqual(start_vars[p] + data.tasks[p].duration_slots + dep.min_lag_slots, start_vars[s]).OnlyEnforceIf(scheduled[s]);
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
  for (int i = 0; i < n; ++i) objective += unscheduled_cost(data.tasks[i], weights) * (LinearExpr(1) - scheduled[i]);
  model.Minimize(objective);
  Model solver;
  SatParameters parameters;
  parameters.set_max_time_in_seconds(time_limit_seconds);
  parameters.set_num_search_workers(8);
  solver.Add(NewSatParameters(parameters));
  const CpSolverResponse response = SolveCpModel(model.Build(), &solver);
  std::vector<Placement> placements;
  if (response.status() == CpSolverStatus::OPTIMAL || response.status() == CpSolverStatus::FEASIBLE) {
    for (int i = 0; i < n; ++i) for (std::size_t k = 0; k < x[i].size(); ++k) if (SolutionBooleanValue(response, x[i][k]))
      placements.push_back({data.tasks[i].id, starts[i][k], starts[i][k] + data.tasks[i].duration_slots});
  }
  const std::string status = response.status() == CpSolverStatus::OPTIMAL ? "OPTIMAL" :
                             response.status() == CpSolverStatus::FEASIBLE ? "FEASIBLE" :
                             response.status() == CpSolverStatus::INFEASIBLE ? "INFEASIBLE" : "UNKNOWN";
  return finalize("cp-sat", data, weights, std::move(placements), started, status, true);
}
#else
Plan solve_cp_sat(const Dataset& data, const Weights& weights, int) {
  // Portable fallback keeps the entire prototype runnable without a local OR-Tools C++ package.
  // Enabling SIH_WITH_ORTOOLS switches this exact API to the native model above.
  const auto started = Clock::now();
  auto placements = greedy_schedule(data, weights, ordered_tasks(data), true);
  auto best_blocks = derive_blocks(data, placements);
  auto best = calculate_metrics(data, placements, best_blocks, weights).objective;
  for (std::size_t i = 0; i < placements.size(); ++i) {
    const auto original = placements[i];
    const auto& task = task_by_id(data, original.task_id);
    auto without = placements; without.erase(without.begin() + static_cast<long>(i));
    for (int s = task.earliest_slot; s + task.duration_slots <= task.latest_end_slot; ++s) {
      if (!can_place(data, task, s, without, true)) continue;
      auto trial = without; trial.push_back({task.id, s, s + task.duration_slots});
      const auto value = calculate_metrics(data, trial, derive_blocks(data, trial), weights).objective;
      if (value < best) { placements = std::move(trial); best = value; break; }
    }
  }
  return finalize("cp-sat", data, weights, std::move(placements), started, "FALLBACK_FEASIBLE", false);
}
#endif

std::string plan_json(const Plan& p) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << "{\"algorithm\":\"" << escape_json(p.algorithm) << "\",\"solver_status\":\"" << p.solver_status
      << "\",\"runtime_ms\":" << p.runtime_ms << ",\"native_cp_sat\":" << (p.native_cp_sat ? "true" : "false")
      << ",\"validation\":{\"valid\":" << (p.validation.valid ? "true" : "false") << ",\"violations\":[";
  for (std::size_t i = 0; i < p.validation.violations.size(); ++i) {
    if (i) out << ','; out << '"' << escape_json(p.validation.violations[i]) << '"';
  }
  out << "]},\"metrics\":{\"objective\":" << p.metrics.objective << ",\"block_count\":" << p.metrics.block_count
      << ",\"downtime_minutes\":" << p.metrics.downtime_minutes << ",\"train_impact\":" << p.metrics.train_impact
      << ",\"overdue_penalty\":" << p.metrics.overdue_penalty << ",\"critical_noncompletion\":" << p.metrics.critical_noncompletion
      << ",\"scheduled_tasks\":" << p.metrics.scheduled_tasks << ",\"critical_completed\":" << p.metrics.critical_completed << "},\"placements\":[";
  for (std::size_t i = 0; i < p.placements.size(); ++i) {
    if (i) out << ','; const auto& x = p.placements[i];
    out << "{\"task_id\":\"" << escape_json(x.task_id) << "\",\"start_slot\":" << x.start_slot << ",\"end_slot\":" << x.end_slot << '}';
  }
  out << "],\"blocks\":[";
  for (std::size_t i = 0; i < p.blocks.size(); ++i) {
    if (i) out << ','; const auto& b = p.blocks[i];
    out << "{\"corridor_id\":\"" << escape_json(b.corridor_id) << "\",\"start_slot\":" << b.start_slot << ",\"end_slot\":" << b.end_slot << '}';
  }
  out << "]}";
  return out.str();
}

std::string benchmark_json(const std::vector<Plan>& plans) {
  std::ostringstream out; out << "{\"horizon_slots\":" << kHorizonSlots << ",\"slot_minutes\":" << kSlotMinutes << ",\"plans\":[";
  for (std::size_t i = 0; i < plans.size(); ++i) { if (i) out << ','; out << plan_json(plans[i]); }
  out << "]}"; return out.str();
}

}  // namespace sih
