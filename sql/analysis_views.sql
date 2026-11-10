-- Summaries of the predictor data (MySQL 8: window functions). Safe to re-run.
-- Load after schema.sql:  mysql pride_bank < sql/analysis_views.sql

-- One row per run: how long, how bad, and how the time was spread over the four states.
CREATE OR REPLACE VIEW v_run_summary AS
SELECT s.dataset,
       s.run_id,
       COUNT(*)                     AS samples,
       MAX(s.waiting)               AS max_waiting,
       MAX(s.max_chain)             AS max_chain,
       SUM(s.new_deadlocks)         AS deadlocks,
       ROUND(AVG(s.state = 0), 4)   AS share_normal,
       ROUND(AVG(s.state = 1), 4)   AS share_contention,
       ROUND(AVG(s.state = 2), 4)   AS share_high_risk,
       ROUND(AVG(s.state = 3), 4)   AS share_deadlock
FROM telemetry_sample s
GROUP BY s.dataset, s.run_id;

-- One row per dataset and state: how often it occurs and what the load looks like there.
CREATE OR REPLACE VIEW v_state_summary AS
SELECT dataset,
       state,
       COUNT(*)                                                   AS samples,
       ROUND(COUNT(*) / SUM(COUNT(*)) OVER (PARTITION BY dataset), 4) AS share,
       ROUND(AVG(active), 2)                                      AS avg_active,
       ROUND(AVG(waiting), 2)                                     AS avg_waiting,
       ROUND(AVG(max_chain), 2)                                   AS avg_chain
FROM telemetry_sample
GROUP BY dataset, state;

-- Transition counts the Markov chain learns from, derived with SQL only.
-- Same rules as the predictor: an idle sample (no transfers, NORMAL) ends an episode, and a
-- transition out of DEADLOCK is not counted.
CREATE OR REPLACE VIEW v_transition_counts AS
SELECT dataset, from_state, to_state, COUNT(*) AS cnt
FROM (
    SELECT dataset,
           LAG(state)  OVER w AS from_state,
           LAG(active) OVER w AS from_active,
           state              AS to_state,
           active             AS to_active
    FROM telemetry_sample
    WINDOW w AS (PARTITION BY dataset, run_id ORDER BY sample_id)
) t
WHERE from_state IS NOT NULL
  AND from_state < 3
  AND NOT (from_active = 0 AND from_state = 0)
  AND NOT (to_active = 0 AND to_state = 0)
GROUP BY dataset, from_state, to_state;
