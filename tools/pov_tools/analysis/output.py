"""Output generation: JSON and HTML report."""

import json
from pathlib import Path

from .types import AnalysisContext, AnalysisResult


def results_to_json(results: list[AnalysisResult], ctx: AnalysisContext) -> dict:
    """Convert analysis results to JSON-serializable dict."""
    output: dict = {
        "data_dir": str(ctx.output_dir),
    }

    # Merge metrics from each analyzer
    for result in results:
        if result.name == "data_quality":
            # Preserve nested structure for readability
            output["data_quality"] = {
                "summary": {
                    "accel_samples": len(ctx.accel),
                    "hall_events": len(ctx.hall),
                    "rotations_covered": int(ctx.hall["rotation_num"].nunique()),
                },
                "sample_timing": result.metrics.get("sample_timing", {}),
                "sequence_gaps": result.metrics.get("sequence_gaps", {}),
                "samples_per_rotation": result.metrics.get("samples_per_rotation", {}),
                "phase_coverage": result.metrics.get("phase_coverage", {}),
                "capture_stats": result.metrics.get("capture_stats", {}),
                "saturation": result.metrics.get("saturation", {}),
            }
            if "hall_timing" in result.metrics:
                output["data_quality"]["hall_timing"] = result.metrics["hall_timing"]
        elif result.name == "rpm_sweep":
            output["rpm_range"] = {
                "min": result.metrics.get("rpm_min"),
                "max": result.metrics.get("rpm_max"),
            }
            # Add hall glitches to hall_timing section
            if "hall_timing" not in output.get("data_quality", {}):
                output.setdefault("data_quality", {})["hall_timing"] = {}
            output["data_quality"]["hall_timing"]["glitches"] = result.metrics.get(
                "hall_glitches", 0
            )
        elif result.name == "phase_analysis":
            output["frequency_analysis"] = result.metrics.get("frequency_analysis", {})
            output["balancing"] = result.metrics.get("balancing", {})
            if "phase_by_position" in result.metrics:
                output["phase_by_position"] = result.metrics["phase_by_position"]
            if "phase_summary" in result.metrics:
                output["phase_summary"] = result.metrics["phase_summary"]
        elif result.name == "gyro_wobble":
            output["gyro_wobble"] = {
                "wobble_vs_rpm": result.metrics.get("wobble_vs_rpm", {}),
                "precession": result.metrics.get("precession", {}),
                "gyro_phase": result.metrics.get("gyro_phase", {}),
            }
        elif result.name == "validation":
            output["validation"] = {
                "gyro_vs_hall": result.metrics.get("gyro_vs_hall", {}),
                "spin_direction": result.metrics.get("spin_direction", {}),
                "rpm_stability": result.metrics.get("rpm_stability", {}),
                "centrifugal": result.metrics.get("centrifugal", {}),
            }

    # Collect all plots
    all_plots = []
    for result in results:
        for plot in result.plots:
            all_plots.append(str(plot.relative_to(ctx.output_dir)))

    output["plots"] = all_plots
    output["report_path"] = str(ctx.output_dir / "report.html")

    return output


def generate_report(results: list[AnalysisResult], ctx: AnalysisContext) -> Path:
    """Generate HTML report from analysis results."""
    # Collect all findings
    all_findings = []
    for result in results:
        if result.findings:
            all_findings.append(f"<h3>{result.name.replace('_', ' ').title()}</h3>")
            all_findings.append("<ul>")
            for finding in result.findings:
                all_findings.append(f"  <li>{finding}</li>")
            all_findings.append("</ul>")

    findings_html = "\n".join(all_findings)

    # Collect plots by category
    plots_html_parts = []
    for result in results:
        if result.plots:
            plots_html_parts.append(
                f"<h3>{result.name.replace('_', ' ').title()}</h3>"
            )
            for plot in result.plots:
                rel_path = plot.relative_to(ctx.output_dir)
                plots_html_parts.append(
                    f'<img src="{rel_path}" style="max-width: 100%; margin: 10px 0;">'
                )

    plots_html = "\n".join(plots_html_parts)

    # Get JSON data for embedding
    json_data = results_to_json(results, ctx)

    html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Telemetry Analysis Report</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }}
        h1 {{ color: #333; border-bottom: 2px solid #007acc; padding-bottom: 10px; }}
        h2 {{ color: #007acc; margin-top: 30px; }}
        h3 {{ color: #555; margin-top: 20px; }}
        .findings {{ background: white; padding: 20px; border-radius: 8px; margin: 20px 0; }}
        .plots {{ background: white; padding: 20px; border-radius: 8px; margin: 20px 0; }}
        .plots img {{ border: 1px solid #ddd; border-radius: 4px; }}
        ul {{ line-height: 1.8; }}
        .balancing-summary {{
            background: #e8f4f8;
            border-left: 4px solid #007acc;
            padding: 15px 20px;
            margin: 20px 0;
            font-size: 1.1em;
        }}
        .balancing-summary strong {{ color: #007acc; }}
        pre {{
            background: #2d2d2d;
            color: #f8f8f2;
            padding: 15px;
            border-radius: 8px;
            overflow-x: auto;
            font-size: 12px;
        }}
        .definitions {{
            background: #f9f9f9;
            border: 1px solid #ddd;
            border-radius: 8px;
            padding: 20px;
            margin: 20px 0;
        }}
        .definitions h4 {{
            color: #007acc;
            margin-top: 15px;
            margin-bottom: 5px;
        }}
        .definitions p {{
            margin: 5px 0 15px 0;
            color: #555;
            font-size: 0.95em;
        }}
    </style>
</head>
<body>
    <h1>Telemetry Analysis Report</h1>
    <p>Data directory: <code>{ctx.output_dir}</code></p>

    <div class="balancing-summary">
        <strong>Balancing Recommendation:</strong><br>
        Peak imbalance at: <strong>{json_data.get('balancing', {}).get('peak_imbalance_deg', 'N/A')} deg</strong><br>
        Place counterweight at: <strong>{json_data.get('balancing', {}).get('counterweight_position_deg', 'N/A')} deg</strong>
    </div>

    <h2>Understanding the Results</h2>
    <div class="definitions">
        <h4>Wobble (gyro_wobble_dps)</h4>
        <p>Magnitude of rotation on non-spin axes (GX, GY). Indicates how much the
        disc tilts/wobbles as it spins. For classic imbalance, wobble scales with RPM squared.</p>

        <h4>Precession Direction</h4>
        <p>The axis around which the disc wobbles. Derived from mean GX/GY values.
        Consistent direction across speeds indicates reliable imbalance measurement.</p>

        <h4>Phase</h4>
        <p>Angular position (0-360 deg) of the heavy spot relative to the hall sensor.
        Determined by fitting sinusoids to sensor data vs rotation angle.</p>

        <h4>R-squared (Fit Quality)</h4>
        <p>How well the sinusoid fits the data. R-squared > 0.15 indicates detectable signal.
        Higher R-squared = more confident phase estimate.</p>

        <h4>1x Component</h4>
        <p>Vibration at once-per-revolution frequency. This is the imbalance signal.
        The 3x component (three times per revolution) is the arm geometry pattern.</p>

        <h4>Gyro Z Saturation</h4>
        <p>The gyroscope Z-axis (spin axis) maxes out at +/-2000 deg/s = ~333 RPM.
        Above this speed, gz_dps is saturated and unreliable. GX/GY (wobble axes)
        should remain usable at all speeds.</p>

        <h4>Centrifugal Force</h4>
        <p>X-axis (radial) sees centrifugal acceleration = omega-squared * r. At 25.4mm radius,
        the +/-16g accelerometer saturates around 760 RPM. This is expected.</p>

        <h4>Spin Direction</h4>
        <p>Detected from gz_dps sign at low speed (before saturation).
        Negative = clockwise when viewed from above (motor shaft pointing up).</p>
    </div>

    <h2>Findings</h2>
    <div class="findings">
        {findings_html}
    </div>

    <h2>Plots</h2>
    <div class="plots">
        {plots_html}
    </div>

    <h2>Raw Data (JSON)</h2>
    <pre>{json.dumps(json_data, indent=2)}</pre>
</body>
</html>
"""

    report_path = ctx.output_dir / "report.html"
    report_path.write_text(html)
    return report_path
