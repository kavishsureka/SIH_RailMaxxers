# Prototype design notes

The two supplied SIH planning references were used as design input, then reduced to the smallest useful internal-hackathon path.

## Preserved from the references

- coordination across Engineering, S&T, and TRD;
- corridor availability, protected train movements, incompatibilities, power blocks, and task dependencies;
- consolidation of compatible simultaneous work into a common corridor block;
- independent baseline, coordinated greedy, advanced optimizer, independent validator, and KPI benchmark flow;
- block count, infrastructure downtime, train impact, priority/overdue completion, and runtime reporting;
- synthetic interactive scenario with 10 corridors, 120 tasks, and 120 movements per day over four weeks;
- generated 100-, 250-, and 500-task benchmark presets.

## Agreed changes implemented

- Go API and C++ optimizer are used from the start;
- every train is electric, so there is no traction dimension; HARD movements are forbidden and SOFT movements remain feasible, including for power-block work;
- the horizon is a transparent 2,688-slot grid: 28 days at fifteen minutes per slot;
- all tasks are compulsory and must appear exactly once by month end;
- candidate windows are shared by all algorithms and subtract merged HARD intervals only;
- CP-SAT solves the complete month in one model rather than four sequential weekly solves;
- optimization uses one configurable weighted objective rather than lexicographic stages;
- runtime is mandatory for Independent, Greedy, and CP-SAT;
- all three algorithms pass through the same validator and metric calculator.

## Deferred production scope

Authentication, organization hierarchy, resource/team calendars, Kafka, Redis, GIS, live railway system integration, probabilistic goods forecasting, real-time replanning, and monthly rolling plans are intentionally outside this prototype.
