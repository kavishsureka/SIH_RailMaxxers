#include "engine.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

sih::Dataset soft_train_fixture() {
  sih::Dataset data;
  data.corridors.push_back({"C1", "Test corridor"});
  data.tasks.push_back({"T1", "C1", "ENGINEERING", "TRACK_REPAIR", 2, 8, 8,
                        8, true, false, 0, 8});
  data.availability.push_back({"C1", 0, 8});
  data.trains.push_back({"S1", "C1", 0, 2, false, 7});
  return data;
}

}  // namespace

int main() {
  {
    auto data = soft_train_fixture();
    data.trains.push_back({"H1", "C1", 4, 6, true, 10});
    data.trains.push_back({"H2", "C1", 5, 8, true, 10});
    data.tasks[0].mandatory = false;
    data.tasks[0].severity = 5;
    data.tasks[0].criticality = 5;
    data.tasks[0].duration_slots = 2;
    auto candidates = sih::generate_candidate_windows(data);
    const auto& windows = candidates.at("T1");
    require(windows.size() == 1, "overlapping HARD intervals should merge before subtraction");
    require(windows[0].start_slot == 0 && windows[0].end_slot == 4,
            "SOFT interval must remain inside a candidate window");
  }

  {
    const auto data = soft_train_fixture();
    const sih::Weights weights;
    const std::vector<sih::Placement> overlapping{{"T1", 0, 2}};
    const std::vector<sih::Placement> clear{{"T1", 2, 4}};
    const auto overlap_metrics = sih::calculate_metrics(data, overlapping,
        sih::derive_blocks(data, overlapping), weights);
    const auto clear_metrics = sih::calculate_metrics(data, clear,
        sih::derive_blocks(data, clear), weights);
    require(overlap_metrics.train_impact == 7, "SOFT overlap should have positive impact");
    require(clear_metrics.train_impact == 0, "non-overlapping placement should have zero impact");

    const auto plan = sih::solve_cp_sat(data, weights, 5);
    require(plan.validation.valid, "CP-SAT soft-impact fixture should validate");
    require(plan.metrics.scheduled_tasks == 1, "CP-SAT must schedule the task exactly once");
    require(plan.metrics.train_impact == 0, "CP-SAT should prefer the equal-cost clear placement");
    require(plan.task_traces.size() == 1, "plan should expose one evidence trace per task");
    require(!plan.task_traces[0].candidates.empty(), "task trace should expose evaluated windows");
    require(plan.task_traces[0].candidates[0].status == "SELECTED",
            "selected placement should be explicit in the evidence trace");
  }

  {
    const auto data = soft_train_fixture();
    const auto validation = sih::validate(data, {});
    require(!validation.valid, "validator must reject any unscheduled monthly task");
  }

  std::cout << "optimizer unit tests passed\n";
  return 0;
}
