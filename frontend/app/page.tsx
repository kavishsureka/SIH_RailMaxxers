"use client";

import { useEffect, useMemo, useState } from "react";

type Metrics = {
  objective: number; block_count: number; downtime_minutes: number; train_impact: number;
  overdue_penalty: number; critical_noncompletion: number; scheduled_tasks: number; critical_completed: number;
};
type Block = { corridor_id: string; start_slot: number; end_slot: number };
type Plan = {
  algorithm: string; solver_status: string; runtime_ms: number; native_cp_sat: boolean;
  validation: { valid: boolean; violations: string[] }; metrics: Metrics; blocks: Block[];
};
type Benchmark = { horizon_slots: number; slot_minutes: number; plans: Plan[] };

const API = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8080";
const labels: Record<string, string> = { independent: "Independent", greedy: "Coordinated Greedy", "cp-sat": "CP-SAT" };
const colors: Record<string, string> = { C1: "#37d5a5", C2: "#f6c85f", C3: "#ef6f88", C4: "#70a7ff", C5: "#a78bfa" };

function formatSlot(slot: number) {
  const day = Math.floor(slot / 96);
  const within = slot % 96;
  const hour = Math.floor(within / 4).toString().padStart(2, "0");
  const minute = ((within % 4) * 15).toString().padStart(2, "0");
  return `${["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"][day]} ${hour}:${minute}`;
}

function Metric({ label, value, suffix = "" }: { label: string; value: string | number; suffix?: string }) {
  return <div className="metric"><span>{label}</span><strong>{value}{suffix}</strong></div>;
}

export default function Home() {
  const [data, setData] = useState<Benchmark | null>(null);
  const [active, setActive] = useState("cp-sat");
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  const run = async () => {
    setLoading(true); setError("");
    try {
      const response = await fetch(`${API}/api/benchmark`, { cache: "no-store" });
      if (!response.ok) throw new Error(await response.text());
      setData(await response.json());
    } catch {
      setError("The planner API is offline. Start the Go service, then run the benchmark again.");
    } finally { setLoading(false); }
  };
  useEffect(() => { void run(); }, []);
  const plan = useMemo(() => data?.plans.find((item) => item.algorithm === active) ?? data?.plans[0], [data, active]);

  return <main>
    <header>
      <div className="brand"><div className="mark">RB</div><div><b>RailBlock</b><span>SIH 26027 planner</span></div></div>
      <div className="scope"><span className="pulse"/>7 days · 15-minute slots · all-electric</div>
      <button className="primary" onClick={run} disabled={loading}>{loading ? "Solving…" : "Run benchmark"}</button>
    </header>

    <section className="hero">
      <div><p className="eyebrow">Coordinated maintenance control</p><h1>Make every block<br/><em>work harder.</em></h1></div>
      <p className="intro">One weekly plan for Engineering, S&amp;T and TRD — balanced against protected train movements and independently verified.</p>
    </section>

    {error && <div className="notice"><b>Planner unavailable</b><span>{error}</span><button onClick={run}>Retry</button></div>}

    {data && <>
      <section className="comparison">
        {data.plans.map((item) => <button key={item.algorithm} className={`plan-card ${active === item.algorithm ? "selected" : ""}`} onClick={() => setActive(item.algorithm)}>
          <div className="card-top"><span>{labels[item.algorithm] ?? item.algorithm}</span><i className={item.validation.valid ? "valid" : "invalid"}>{item.validation.valid ? "VALID" : `${item.validation.violations.length} ISSUES`}</i></div>
          <strong>{item.metrics.objective.toLocaleString("en-IN")}</strong><small>weighted objective</small>
          <div className="mini"><span>{item.metrics.block_count} blocks</span><span>{item.metrics.downtime_minutes / 60}h downtime</span><span>{item.runtime_ms.toFixed(1)}ms</span></div>
        </button>)}
      </section>

      {plan && <section className="workspace">
        <div className="panel gantt-panel">
          <div className="panel-head"><div><p className="eyebrow">Weekly possession map</p><h2>{labels[plan.algorithm]} plan</h2></div><div className="legend">{["C1","C2","C3","C4","C5"].map(c => <span key={c}><i style={{background: colors[c]}}/>{c}</span>)}</div></div>
          <div className="days">{["MON","TUE","WED","THU","FRI","SAT","SUN"].map(day => <span key={day}>{day}</span>)}</div>
          <div className="gantt">
            {["C1","C2","C3","C4","C5"].map((corridor, row) => <div className="gantt-row" key={corridor} style={{gridRow: row + 1}}><b>{corridor}</b></div>)}
            {plan.blocks.map((block, index) => {
              const row = Number(block.corridor_id.slice(1));
              return <div key={`${block.corridor_id}-${index}`} className="block" title={`${block.corridor_id}: ${formatSlot(block.start_slot)} – ${formatSlot(block.end_slot)}`}
                style={{gridRow: row, left: `${(block.start_slot / 672) * 100}%`, width: `${Math.max(.5, ((block.end_slot - block.start_slot) / 672) * 100)}%`, background: colors[block.corridor_id]}}/>;
            })}
          </div>
        </div>

        <aside className="panel summary">
          <div className="panel-head"><div><p className="eyebrow">Plan health</p><h2>{plan.solver_status.replaceAll("_", " ")}</h2></div><span className={plan.validation.valid ? "shield ok" : "shield bad"}>✓</span></div>
          <div className="metric-grid">
            <Metric label="Blocks" value={plan.metrics.block_count}/><Metric label="Downtime" value={(plan.metrics.downtime_minutes/60).toFixed(1)} suffix="h"/>
            <Metric label="Train impact" value={plan.metrics.train_impact}/><Metric label="Runtime" value={plan.runtime_ms.toFixed(1)} suffix="ms"/>
            <Metric label="Tasks placed" value={plan.metrics.scheduled_tasks}/><Metric label="Critical done" value={plan.metrics.critical_completed}/>
          </div>
          <div className="validator"><span>Independent validator</span><b>{plan.validation.valid ? "All hard rules pass" : plan.validation.violations[0]}</b></div>
          {plan.algorithm === "cp-sat" && <p className="solver-note">{plan.native_cp_sat ? "Native OR-Tools CP-SAT model" : "Portable solver fallback — enable OR-Tools for native CP-SAT"}</p>}
        </aside>
      </section>}
    </>}

    <footer><span>Prototype decision-support system</span><span>Engineering · S&amp;T · TRD</span><span>Runtime measured for every algorithm</span></footer>
  </main>;
}
