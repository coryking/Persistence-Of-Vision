# Analyzers

Each analyzer receives an `AnalysisContext` and returns an `AnalysisResult`.

| File | Purpose |
|------|---------|
| `data_quality.py` | Sample rates, sequence gaps, timing jitter, phase coverage |
| `rpm_sweep.py` | RPM progression over time, glitch detection |
| `axis_timeseries.py` | Time series plots for X/Y/Z axes |
| `distribution.py` | Histogram distributions of axis values |
| `phase.py` | Polar plots, FFT analysis, balancing recommendations |

## Adding a New Analyzer

Create a function `def my_analysis(ctx: AnalysisContext) -> AnalysisResult` in a new file, then add it to `ANALYZERS` in `__init__.py`. The result's `metrics` dict goes into JSON output, `findings` list becomes bullet points in the report, and `plots` list gets embedded in HTML.
