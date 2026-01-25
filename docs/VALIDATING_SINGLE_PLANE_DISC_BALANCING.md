# Validating single-plane disc balancing with on-rotor sensors

Your methodology aligns well with established industrial and academic practice, with a few important refinements needed. The core approach—measuring wobble magnitude from gyroscope axes, fitting against ω², using sinusoid/angle-binned phase analysis, and discrete speed steps—is fundamentally sound. The primary concern is ensuring you account for the **phase lag between sensor measurement and actual heavy spot location**, which varies from 0° to 180° depending on operating speed relative to critical speed.

## Industrial balancing equipment relies on synchronous detection

Professional balancing systems from Schenck, IRD, Hofmann, and CEMB universally employ **piezoelectric accelerometers** (100 mV/g typical sensitivity) or velocity pickups combined with **optical tachometers** (laser or IR with reflective tape) for phase reference. The fundamental signal processing technique is **synchronous detection** (also called phase-sensitive detection or lock-in amplification), which multiplies the vibration signal by a reference signal at the rotation frequency, then low-pass filters to extract only the 1× component.

The mathematical implementation uses dual-phase demodulation: the signal is simultaneously multiplied by sine and cosine references to extract in-phase (X) and quadrature (Y) components. Converting to polar coordinates yields amplitude R = √(X² + Y²) and phase θ = arctan(Y/X)—**directly analogous to your sqrt(gx² + gy²) approach**. Modern systems like the Schenck SmartBalancer 4 and CEMB N600 also perform simultaneous FFT analysis, extracting the 1× harmonic magnitude and phase. Your angle-binned sinusoid fitting is essentially a time-domain equivalent of extracting this 1× component.

Hard-bearing machines measure centrifugal force directly through force transducers and maintain permanent calibration. Soft-bearing machines measure displacement or acceleration and require rotor-specific calibration via the **influence coefficient method**: apply a trial weight, measure the vector change in vibration, and compute the influence coefficient as (change in vibration)/(trial weight). The correction weight equals -(original vibration)/(influence coefficient). This empirical approach implicitly captures all machine-specific dynamics.

## The ω² relationship is fundamental but requires understanding its limits

The centrifugal force from unbalance is **F = m × e × ω²**, where m is the eccentric mass, e is the eccentricity, and ω is angular velocity. This ω² relationship is unambiguously correct for the forcing function and universally applied across industry. Your approach of fitting wobble magnitude versus RPM² to confirm classic imbalance signature is theoretically valid.

However, three corrections merit attention:

**Dynamic amplification near critical speeds** causes the vibration response to deviate significantly from pure ω² scaling. The Jeffcott rotor model shows that amplitude follows A/e = f²/√[(1-f²)² + (2ζf)²], where f = ω/ωn (frequency ratio to natural frequency) and ζ is damping ratio. Well below critical speed, this reduces to approximately f² (pure ω² behavior). At critical speed (f=1), amplification can reach **10× or more** depending on damping, and phase shifts 90°. Well above critical speed, amplitude approaches a constant while phase approaches 180°.

**Gyroscopic stiffening** affects rotating discs by adding apparent stiffness proportional to spin speed, causing forward and backward whirl modes to diverge. For thin discs operating well below critical speed, this effect is typically small but becomes significant at higher speeds.

**Structural resonances** of your test fixture can corrupt measurements if excited. Operating at speeds where the machine's natural frequency coincides with rotation frequency will produce anomalous results.

Your R² > 0.9 expectation for the ω² fit is reasonable **provided you operate well below the first critical speed** (typically recommended to stay below 70% of critical). Near resonance, deviations from pure ω² behavior will reduce R². If you observe R² dropping at higher speeds, this likely indicates approach to a structural resonance rather than a methodology problem.

## Phase determination methods differ in noise tolerance

Three primary approaches exist for determining the phase angle (heavy spot location):

**FFT with 1× extraction** is the industry standard. Compute the FFT, identify the bin at the rotation frequency, and extract magnitude and phase directly. The phase is calculated as the argument (angle) of the complex FFT coefficient. Limitations include frequency resolution (Δf = 1/sample_period) and spectral leakage if the rotation frequency doesn't align precisely with FFT bins. Windowing (Hanning, Hamming) helps but introduces minor phase errors.

**Synchronous detection / lock-in amplification** multiplies the signal by reference sine and cosine at the known rotation frequency, then low-pass filters. This is computationally efficient and provides excellent noise rejection by operating as an extremely narrow bandpass filter centered exactly on the rotation frequency. This is what professional analyzers use internally.

**Sinusoid fitting via least squares** (your approach) optimizes amplitude, phase, and optionally DC offset to minimize residual error. Academic research indicates this method provides **better phase accuracy than FFT at low signal-to-noise ratios**, achieving reliable estimates starting at SNR of -6 dB where FFT struggles. When the frequency is known (locked to rotation speed), three-parameter fitting (amplitude, phase, DC offset with known frequency) is optimal.

Your angle-binned sinusoid fitting is a reasonable implementation. The binning by angle (rather than time) naturally handles speed variations and is equivalent to synchronous averaging. For production balancing with stable rotation speed, **your method should perform comparably to or better than FFT**, particularly in noisy conditions.

## Gyroscope precession introduces speed-dependent phase offsets

The relationship between gyroscope-measured precession and actual heavy spot location is critical and potentially the most important refinement for your methodology. The vibration response (high spot) **always lags behind** the unbalance location (heavy spot) by a phase angle that varies with operating conditions:

| Operating Condition | Phase Lag |
|---------------------|-----------|
| Well below critical speed | ~0° |
| At critical speed | 90° |
| Well above critical speed | ~180° |

The heavy spot angle is calculated as: **Heavy Spot° = Measured Phase° - Lag Angle° + Sensor Angle° + Integration Angle°**, where Integration Angle = 0° for displacement, 90° for velocity, and 180° for acceleration. For gyroscope-measured angular rate (which is the derivative of angle, analogous to velocity), you should expect approximately **90° integration offset**.

Your observation that precession direction should be consistent across speeds is **approximately correct** for operation well below critical speed, but the phase will shift progressively as you approach resonance. If you observe systematic phase drift with increasing speed, this indicates you're seeing the dynamic response characteristic rather than measurement error. The practical solution is to either operate at a fixed speed well below critical (where lag approaches 0°), or calibrate the phase offset empirically using a trial weight run.

Classical gyroscopic precession theory predicts 90° phase offset between applied torque and resulting tilt, but this is often modified by bearing constraints, damping, and aerodynamic effects. Helicopter rotor research shows actual phase offsets ranging from 72° to 90° depending on specific dynamics. **Empirical calibration via trial weight** is the most reliable approach.

## Quality metrics in professional practice emphasize repeatability over R²

ISO rotor balancing standards (ISO 21940 series, formerly ISO 1940) **do not explicitly specify R² values** for measurement validation. Instead, they focus on:

**Repeatability assessment** using multiple measurement runs. ISO 21940-14 recommends plotting measured unbalance vectors from multiple runs, finding the mean vector, and drawing the smallest circle enclosing all points. The circle radius represents maximum possible error per reading. This graphical/statistical approach accounts for both systematic and random errors.

**Acceptance criteria** depend on whether you're assessing during balancing or performing a separate verification check:
- During balancing: |Measured residual| ≤ Permissible - Measurement error
- Verification check: |Measured residual| ≤ Permissible + Measurement error

**Balancing machine performance** is characterized by Umar (minimum achievable residual unbalance—the noise floor) and URR (unbalance reduction ratio—effectiveness of single-correction accuracy).

From general metrology practice applied to balancing, **R² ≥ 0.95** is typically required for acceptable calibration relationships. Your R² > 0.9 threshold is slightly relaxed but reasonable for field validation. The GitHub rotorbalancer project reports standard deviations in the 10⁻³ percent range for repeated measurements, indicating excellent repeatability is achievable with proper implementation. Hobbyist practitioners typically consider **±30° phase tolerance** acceptable for diagnostic comparisons, and **5× vibration reduction** (e.g., 0.5 IPS to 0.1 IPS) represents excellent balancing.

## ISO 21940-11 provides the framework for balance quality grades

The current standard is **ISO 21940-11:2016** (replacing ISO 1940-1:2003), which defines balance quality grades from G 0.4 (precision gyroscopes) to G 4000 (large marine diesels). The grade number equals the permissible specific unbalance multiplied by angular velocity, expressed in mm/s.

The permissible residual unbalance formula is: **Uper (g·mm) = 9549 × G × m / n**, where G is the balance quality grade, m is rotor mass in kg, and n is maximum service speed in RPM.

Relevant grades for thin disc applications include G 6.3 for fans, pumps, and general machinery; G 2.5 for precision machinery and turbomachinery; and G 1.0 for grinding drives and high-precision applications.

For single-plane (static) balancing of disc-shaped rotors (L/D ratio < 0.5), **the full permissible unbalance applies to the single correction plane** without allocation between planes. ISO guidance confirms that static unbalance can be corrected in any single plane, though couple unbalance may remain. For thin discs where L/D < 0.33 ("narrow rotors"), special allocation rules apply if two-plane balancing is used.

## Common pitfalls practitioners actively monitor

**Mounting quality** dominates measurement accuracy. Poor accelerometer mounting reduces resonant frequency and limits useful measurement range. Professional practice uses stud mounting with thin grease layers for best stiffness. For MEMS sensors, foam or servo tape under the accelerometer helps dampen high-frequency noise without excessively reducing low-frequency sensitivity.

**System resonance** corrupts measurements when the test fixture's natural frequency coincides with rotation frequency. The solution is variable-speed capability to identify and avoid resonance regions, or designing the fixture with resonance well above maximum test speed.

**Aliasing** occurs when sampling rate is insufficient. The Nyquist criterion requires sampling at >2× the highest frequency of interest; practical implementation needs **10× rotation frequency** for good resolution. For 3000 RPM (50 Hz), minimum 500 Hz sampling is advisable. Most MEMS accelerometers (500-1000 Hz capability) are adequate for typical balancing applications below 5000 RPM.

**Spectral leakage** in FFT analysis occurs when rotation frequency doesn't align with FFT bins. Solutions include windowing functions, longer sample periods for finer frequency resolution, or your angle-binning approach which naturally handles this.

**Vibration rectification error** in MEMS accelerometers causes DC offset shift under AC vibration, potentially corrupting low-frequency measurements. High-quality accelerometers and proper filtering mitigate this.

**Ambient light interference** affects IR optical tachometers. Mounting the IR LED below the detection plane and using shielded sensors reduces this. Through-beam optical sensors (separate emitter and detector) are more robust than reflective tape approaches.

**Bearing/support noise** can mask the imbalance signal. DIY builders report that replacing metal bearings with nylon V-blocks dramatically improves signal cleanliness by eliminating bearing-induced vibration.

## Your methodology assessment summary

Your five-point methodology maps well to established practice:

1. **Measuring wobble as sqrt(gx² + gy²)**: Theoretically correct and directly analogous to industrial synchronous detection output. The magnitude represents total tilt rate independent of instantaneous azimuthal position.

2. **Fitting wobble vs RPM²**: Valid for confirming imbalance signature. Expect good R² (>0.9) when operating well below critical speed. Deviations indicate approach to resonance or fixture problems, not methodology failure.

3. **Computing precession direction as atan2(mean_gy, mean_gx)**: Provides raw phase measurement. **Key refinement needed**: Account for speed-dependent lag angle (0° to 180°) and sensor integration offset (~90° for rate measurement). Calibrate empirically with trial weight or operate at consistent speed well below critical.

4. **Sinusoid fitting to angle-binned accelerometer data**: Comparable to or better than FFT for phase determination, particularly at low SNR. Angle-binning provides natural synchronous averaging and handles speed variations gracefully.

5. **Discrete speed steps with steady-state analysis**: This is standard professional practice. The influence coefficient method uses exactly this approach—discrete runs at stable speed for initial, trial weight, and verification measurements.

The main enhancement to consider is implementing the **influence coefficient method** for systematic correction: measure baseline, add known trial weight at known position, measure change, calculate required correction. This empirically captures all phase offsets, dynamic effects, and machine-specific characteristics without requiring theoretical correction factors.
