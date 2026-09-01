# Prototype design notes

The two supplied SIH planning references were used as design input, then reduced to the smallest useful internal-hackathon path.

## Preserved from the references

- coordination across Engineering, S&T, and TRD;
- corridor availability, protected train movements, incompatibilities, power blocks, and task dependencies;
- consolidation of compatible simultaneous work into a common corridor block;
- independent baseline, coordinated greedy, advanced optimizer, independent validator, and KPI benchmark flow;
- block count, infrastructure downtime, train impact, priority/overdue completion, and runtime reporting;
- synthetic interactive scenario with five corridors, roughly thirty tasks, and fifty movements.

## Agreed changes implemented

- Go API and C++ optimizer are used from the start;
- every train is electric, so there is no traction dimension and power-block work conflicts with every overlapping movement;
- the horizon is a transparent 672-slot grid: seven days at fifteen minutes per slot;
- optimization uses one configurable weighted objective rather than lexicographic stages;
- runtime is mandatory for Independent, Greedy, and CP-SAT;
- all three algorithms pass through the same validator and metric calculator.

## Deferred production scope

Authentication, organization hierarchy, resource/team calendars, Kafka, Redis, GIS, live railway system integration, probabilistic goods forecasting, real-time replanning, and monthly rolling plans are intentionally outside this prototype.
