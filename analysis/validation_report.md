# Predictor validation (horizon k = 5 samples = 50 ms)

Training data: 81 runs. Sample = 10 ms. Same estimator as `src/predict/markov_predictor.cpp`.

## 3-fold cross-validation by run (81 runs)

30143 scored points, base rate of 'deadlock within k' = 0.776

| model | Brier | log loss | AUC |
|---|---|---|---|
| Markov chain (1st order) | 0.1496 | 0.4675 | 0.717 |
| Constant hazard | 0.1879 | 0.5833 | 0.354 |
| Per-state frequency | 0.1448 | 0.4577 | 0.713 |
| Markov chain (2nd order) | 0.1518 | 0.4762 | 0.723 |
| Always predict base rate | 0.1740 | 0.5324 | 0.500 |

Calibration of the 1st-order chain:

| predicted bin | n | mean predicted | observed |
|---|---|---|---|
| 0.35-0.50 | 1087 | 0.410 | 0.227 |
| 0.65-0.80 | 8596 | 0.703 | 0.575 |
| 0.80-0.90 | 15492 | 0.872 | 0.902 |
| 0.90-1.00 | 4968 | 0.910 | 0.848 |

| state | n | mean predicted | observed |
|---|---|---|---|
| NORMAL | 1087 | 0.410 | 0.227 |
| CONTENTION | 8596 | 0.703 | 0.575 |
| HIGH_RISK | 20460 | 0.881 | 0.889 |

## Fresh held-out dataset (54 runs, light-load profile, new seeds)

6985 scored points, base rate of 'deadlock within k' = 0.284

| model | Brier | log loss | AUC |
|---|---|---|---|
| Markov chain (1st order) | 0.3093 | 0.8457 | 0.746 |
| Constant hazard | 0.5367 | 1.4576 | 0.500 |
| Per-state frequency | 0.2293 | 0.6607 | 0.746 |
| Markov chain (2nd order) | 0.3162 | 0.8810 | 0.756 |
| Always predict base rate | 0.2035 | 0.5971 | 0.500 |

Calibration of the 1st-order chain:

| predicted bin | n | mean predicted | observed |
|---|---|---|---|
| 0.35-0.50 | 2028 | 0.421 | 0.047 |
| 0.65-0.80 | 3676 | 0.706 | 0.298 |
| 0.90-1.00 | 1281 | 0.907 | 0.621 |

| state | n | mean predicted | observed |
|---|---|---|---|
| NORMAL | 2028 | 0.421 | 0.047 |
| CONTENTION | 3676 | 0.706 | 0.298 |
| HIGH_RISK | 1281 | 0.907 | 0.621 |

## Light-load set, trained and tested in the SAME regime (3-fold CV by run)

6985 scored points, base rate of 'deadlock within k' = 0.284

| model | Brier | log loss | AUC |
|---|---|---|---|
| Markov chain (1st order) | 0.1896 | 0.5586 | 0.714 |
| Constant hazard | 0.2487 | 0.7210 | 0.303 |
| Per-state frequency | 0.1907 | 0.5571 | 0.719 |
| Markov chain (2nd order) | 0.2010 | 0.5837 | 0.657 |
| Always predict base rate | 0.2035 | 0.5971 | 0.500 |

Calibration of the 1st-order chain:

| predicted bin | n | mean predicted | observed |
|---|---|---|---|
| 0.00-0.10 | 345 | 0.080 | 0.067 |
| 0.10-0.20 | 1683 | 0.138 | 0.043 |
| 0.20-0.35 | 3420 | 0.248 | 0.318 |
| 0.35-0.50 | 1302 | 0.394 | 0.555 |
| 0.65-0.80 | 235 | 0.661 | 0.349 |

| state | n | mean predicted | observed |
|---|---|---|---|
| NORMAL | 2028 | 0.128 | 0.047 |
| CONTENTION | 3676 | 0.255 | 0.298 |
| HIGH_RISK | 1281 | 0.451 | 0.621 |

## Chain fitted on all training runs

Counts (rows NORMAL, CONTENTION, HIGH_RISK; columns NORMAL, CONTENTION, HIGH_RISK, DEADLOCK):

```
    876      433       12       29
    348     5199     1872     1229
      9     1027    10731     8693
```

| state | expected steps to deadlock | std dev | empirical mean (censored) | P(deadlock within k) |
|---|---|---|---|---|
| NORMAL | 7.1 | 4.3 | 49.5 | 0.421 |
| CONTENTION | 4.6 | 3.6 | 13.0 | 0.706 |
| HIGH_RISK | 2.6 | 2.4 | 3.1 | 0.907 |

C++ cross-check (`build/markov_cli` on the same counts): NORMAL E=7.149 risk=0.4208, CONTENTION E=4.640 risk=0.7059, HIGH_RISK E=2.600 risk=0.9068
