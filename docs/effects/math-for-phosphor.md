Exponential decay (fast phosphors like P1, P3, P12, P13):
I(t) = I₀ × exp(-t/τ)
Inverse power law (medium/long persistence like P2, P7, P14):
I(t) = I₀ / (1 + γt)^n    where n = 0.5 to 2
Let me dig up the actual τ and n values for specific phosphors.10 stepsCRT phosphor persistence "P31" "P1" microseconds milliseconds decay specifications10 resultsCathode Ray Displays.helsinki.fiCRT Fluorescent Screen - Inst Toolsinstrumentationtools.comCRT Phosphor Videotubetime.usPhosphor - TekWikiw140.comCRTs Archives - Oscilloclock.comoscilloclock.comOptical Time-Domain Eavesdropping Risks of CRT Displays Markus G. Kuhncam.ac.ukphosphor Archives - Oscilloclock.comoscilloclock.comPhosphors – fluorescent lamps, LEDs, illumination, displays, CRT, image intensifierrp-photonics.compersistence – Oscilloclock.comoscilloclock.comSimulating CRT Monitors with FFmpeg (Pt. 2: Monochrome CRTs)int10h.orgCatalogued decay algorithms for distinct phosphor types.I now have a comprehensive set of data. Let me summarize what I've found:
Specific persistence times (time to decay to 10% of initial intensity):

P31: 0.038ms (38 microseconds) - fast phosphor
P4: 0.06ms (60 microseconds) - fast phosphor
P1: 20-24.5ms - medium phosphor
P11: ~2ms - short persistence
P12: medium-long (few seconds)
P28: 600ms - long persistence
P7: >1 minute - very long persistence (inverse power law)
P2: 30 seconds or more - long persistence

Decay types:

Exponential decay: P1, P3, P12, P13 (fast phosphors)

Formula: I(t) = I₀ × exp(-t/τ)


Inverse power law: P2, P7, P14 (long persistence)

Formula: I(t) = I₀ / (1 + γt)^n where n = 0.5 to 2


Combined: P4 (medium persistence)

Modern TV CRT phosphors (P22):

Red (Y2O2S:Eu3+): τ = 150μs, single exponential
Green (Zn2SiO4:Mn2+): τ = 10-15ms, non-single-exponential
Blue (ZnS:Ag,Cl): τ = 100-200μs, not single-exponential

P7 specific:

Cascade phosphor: blue-white flash (440nm) → yellow-green afterglow (558nm)
Blue-white: very short (sub-millisecond)
Yellow-green: >60 seconds (inverse power law)
Composition: ZnS:Ag (blue flash layer) on ZnS:CdS:Cu (yellow persist layer)
This should work well for Cory's needs.
Found some excellent concrete numbers. Let me compile this into a clean reference for you:

Phosphor Decay Algorithms - The Complete Picture
The Two Fundamental Decay Types
1. Exponential Decay (fast phosphors: P1, P3, P11, P12, P13, P31)
I(t) = I₀ × e^(-t/τ)
Where τ is the time constant (time to decay to 37% of initial brightness)
2. Inverse Power Law / Hyperbolic Decay (long persistence: P2, P7, P14)
I(t) = I₀ / (1 + γt)^n
Where:

γ = time scaling constant (typically 1/τ where τ is characteristic time)
n = power exponent, ranges from 0.5 to 2 (typically ~1 for radar phosphors)

P4 (TV white) uses a combination of both.

Concrete Time Constants
PhosphorPersistence (to 10%)Decay TypeTime Constant (τ)P310.038ms (38μs)Exponential~16μsP40.06ms (60μs)Combined~25μsP11~2msExponential~0.9msP120-24msExponential~10msP12few secondsExponential~1-2sP230+ secondsInverse powern≈1P7>60 secondsInverse powern≈1P28600msLong~250ms

P7 (The Radar Phosphor) - Dual Layer Cascade
P7 requires two separate decay calculations running simultaneously:
Layer 1 - Blue-white flash (ZnS:Ag):

Wavelength: 440nm
Decay: Sub-millisecond exponential
τ ≈ 0.1-0.5ms

Layer 2 - Yellow-green afterglow (ZnS:CdS:Cu):

Wavelength: 558nm
Decay: Inverse power law, >60 seconds
I(t) = I₀ / (1 + t/τ)^n where n ≈ 1

The blue layer's light excites the yellow layer (cascade), so they're not independent - the yellow layer's intensity at any moment depends on accumulated excitation from the blue layer's history.

Modern CRT TV Phosphors (P22 RGB)
From the Phosphor Handbook (Shionoya & Yen, 1998):
ColorCompositionτ (1/e time)Decay ShapeRedY₂O₂S:Eu³⁺150μsSingle exponentialGreenZn₂SiO₄:Mn²⁺10-15msMulti-exponential, long tail visible ~1sBlueZnS:Ag,Cl100-200μsNon-single exponential
Green is the limiting factor in CRT refresh rates - it's why green ghosting/trailing is visible.

Practical Implementation Notes
Converting "persistence" specs to time constant:

Persistence is usually quoted as "time to 10%"
For exponential: τ = persistence / 2.303
For power law with n=1: τ = persistence / 9

Multi-exponential for realism:
Research shows triple-exponential models fit real phosphor data best:
I(t) = A₁×e^(-t/τ₁) + A₂×e^(-t/τ₂) + A₃×e^(-t/τ₃)
Where τ₁ < τ₂ < τ₃ represent fast fluorescence, medium decay, and long afterglow components.

This should give you the actual numbers to plug into your decay calculations. The key insight for your radar effect: P7's characteristic look comes from that inverse power law - it decays fast initially then slows down dramatically, holding a dim glow far longer than exponential would predict. That's what creates the distinctive "trails that never quite disappear" look of real radar.
