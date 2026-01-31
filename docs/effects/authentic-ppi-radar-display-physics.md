# Authentic PPI radar display physics for LED art projects

**The classic radar display's haunting glow comes from a cascade phosphor trick**: the P7 phosphor fires a bright blue-white flash when struck, then its inner layer absorbs that light and re-emits a slow yellow-green afterglow lasting up to **60 seconds**. This dual-layer design—not a single glowing substance—creates the iconic trailing persistence that Hollywood often gets wrong. For authentic simulation, you need exponential decay curves (not linear fades), multi-second persistence times, and the understanding that targets were painted once per antenna rotation then slowly faded until refreshed.

---

## How CRT phosphor displays actually worked

The Plan Position Indicator transformed radar into a map by synchronizing an electron beam sweep with antenna rotation. In the classic design, two fixed deflection coils at 90° received sawtooth currents modulated by the antenna's angular position—horizontal coil driven by I×sin(θ), vertical by I×cos(θ)—creating a radial line rotating from screen center outward. The electron beam, normally biased just below visible threshold, brightened only when radar echoes returned.

The critical visual behavior emerged from **phosphor persistence physics**. The P7 phosphor dominated WWII through Cold War radar specifically because of its cascade structure: zinc sulfide silver-activated on top (blue-white flash, sub-millisecond decay) over zinc-cadmium sulfide copper-activated below (yellow-green afterglow, inverse power-law decay exceeding one minute). When the beam struck, operators saw a bright blue-white flash that immediately transitioned to dimming yellow-green. This wasn't simple exponential decay—P7 follows an **inverse power law** (I ∝ 1/t^n) that persists far longer than exponential would predict.

**Typical timing relationships** for a marine radar rotating at 24 RPM with 1000 Hz pulse repetition: each full rotation takes 2.5 seconds, during which approximately 2,500 radial sweeps occur spaced 0.144° apart. Any given target bearing receives a bright refresh once every 2.5 seconds—the phosphor must hold the image between sweeps. The dwell time on any target (time the beam illuminates it per rotation) is typically only **20-30 milliseconds**.

---

## What operators actually saw on screen

The sweep line appeared as a **bright rotating radial** with a pronounced afterglow trail behind it—not a crisp line but a luminous wedge fading from blue-white at the leading edge through yellow-green behind. Targets appeared as bright blips painted along this line at ranges corresponding to echo delay time. These weren't points but **clusters of returns** with varying intensities, the central region brightest.

After the sweep passed, each target blip began its slow fade. With P7's roughly 60-second persistence, a target painted at maximum brightness would remain distinctly visible for the full 2-3 second rotation period, then receive another bright refresh. Moving targets left "comet tails" as their position shifted between sweeps while the afterglow of previous positions still glowed. This created what observers described as targets that "pulse" brighter each sweep while maintaining continuous visibility.

**Noise versus targets** displayed dramatically different behaviors. Random receiver noise appeared as "grass"—constantly shifting speckles that averaged out over the phosphor's integration time. Real targets, appearing consistently in the same position sweep after sweep, accumulated brightness while random noise self-cancelled. This anti-jamming feature was precisely why P7's long persistence was chosen: it acted as a physical averaging filter. The optimal signal-to-noise ratio for P7 displays was approximately **4:1 (12 dB dynamic range)**.

---

## Phosphor types and their persistence characteristics

| Phosphor | Initial color | Afterglow color | Decay time | Behavior |
|----------|---------------|-----------------|------------|----------|
| **P7** | Blue-white (440nm) | Yellow-green (558nm) | >60 seconds | Inverse power law; WWII/Cold War radar standard |
| **P1** | Green (525nm) | Green | 20-100ms | Exponential; early oscilloscopes |
| **P12** | Orange (590nm) | Orange | 2-5 seconds | Medium persistence radar |
| **P31** | Yellowish-green | — | <1ms | Post-1950s fast displays |
| **P19** | Orange (595nm) | Orange | >1 second | Very long persistence radar |

The P7's cascade mechanism worked because its outer layer emitted not just visible blue light but significant ultraviolet, which efficiently excited the inner layer. Operators often placed orange filters over P7 displays to suppress the distracting blue flash and emphasize the yellow afterglow. The persistence wasn't truly "60 seconds to black"—it followed inverse power decay where brightness drops rapidly initially then persists at very low levels for extended periods.

---

## Historical evolution from WWII to digital

**WWII-era displays (1940-1945)** showed what observers called "smudged ellipses" rather than crisp imagery. The original A-scopes (simple range-vs-amplitude traces) gave way to PPI displays invented at Britain's Telecommunications Research Establishment in 1940. Early PPIs used 7-12 inch CRTs with P7 phosphors, requiring completely darkened operations rooms. The Chain Home system used two-layer phosphor displays with yellow plastic filters for anti-jamming—operators would insert the filter when jamming occurred, making only the averaged yellow signal visible.

**Cold War advancement (1950s-1970s)** brought the SAGE system with 48-inch vector CRTs displaying 25,000+ characters per second, introducing synthetic overlays and the light pen. P31 phosphor emerged as the dominant fast-response choice where persistence wasn't required. Fixed-coil deflection replaced physically rotating yokes. The transition from circular to rectangular CRTs began.

**Digital transition (1980s-2000s)** saw raster-scan CRTs emulating PPI sweeps through software, with persistence effects computed rather than physical. The first 28-inch 2K×2K LCD air traffic control display arrived in 2000. Samsung ceased CRT production in 2012; the last major manufacturer (Videocon) stopped in 2015. Modern systems simulate phosphor effects digitally—some maritime radars specifically emulate the classic look through multiple opacity sweeps creating artificial afterglow.

---

## Sonar displays work in slow motion

Sonar PPI displays share radar's circular sweep concept but operate in a fundamentally different time domain. Sound travels at approximately **1,500 m/s** versus light's 300,000,000 m/s—making sonar roughly **200,000 times slower**. A 10 km round-trip takes 67 microseconds for radar but **13.3 seconds** for sonar.

This transforms the display behavior entirely. Where radar sweeps rotate continuously at 1-3 seconds per revolution, sonar displays show an **expanding ring** emanating from center after each ping—you can watch the wavefront propagate outward. The maximum ping rate is physics-limited: at 1,000 meters range, you cannot ping faster than ~0.75 Hz (once every 1.3 seconds) because you must wait for echoes to return before transmitting again.

Passive sonar abandoned PPI displays entirely because without active transmission, you cannot determine range—only bearing. Passive systems use **waterfall displays**: bearing-time recordings (BTR) with bearing on the horizontal axis and time scrolling vertically, or LOFARgrams showing frequency spectra over time. Contacts appear as vertical traces at their bearing that drift as relative motion changes the bearing angle.

---

## Simulating authentic phosphor decay in code

**The fundamental decay equation** is exponential: I(t) = I₀ × e^(-t/τ), where τ is the time constant. However, realistic P7 simulation requires **dual exponential** (blue flash + yellow afterglow) or even **triple exponential** for accuracy. Research confirms triple exponential models produce the smallest simulation deviations.

For frame buffer implementation, the standard approach uses ping-pong buffers:
```
new_frame = current_radar_data + (previous_frame × decay_factor)
decay_factor = exp(-frame_time / τ)
```
For 60fps with 2-second persistence, decay_factor ≈ 0.992 per frame.

**Critical implementation details:**
- Render in polar coordinates for natural PPI mapping—the sweep angle calculation is simply `mod(time × rotation_speed, 2π)`
- The sweep trail is a **gradient wedge**, not a line: `decay = exp(-angle_behind_sweep × decay_rate)`
- Targets should be clusters with spatial extent, not single pixels
- Include noise floor (fast-decaying random speckles) and clutter (slow-decaying range-dependent interference)
- P7 color simulation: start at RGB(200, 200, 255) blue-white, transition through RGB(150, 200, 100) to yellow-green RGB(100, 180, 50), ending at dim RGB(50, 100, 25)

**Common Hollywood mistakes to avoid**: linear fade-out instead of exponential, uniform green instead of blue→yellow transition, instant blip appearance instead of scan-rate-limited detection, point targets instead of return clusters, noise-free displays instead of realistic clutter, and same brightness for all targets regardless of radar cross-section.

---

## Practical implementation for LED displays

For POV LED projects, the essential parameters are:

- **Rotation period**: 2-3 seconds for marine feel, 4-5 seconds for air traffic control feel
- **Sweep persistence**: 0.5-2.0 seconds of visible trail behind the sweep line
- **Target persistence**: 3-15 seconds depending on rotation rate (should remain visible between sweeps)
- **Noise floor**: Very fast decay (0.01-0.1 seconds), randomly positioned each frame
- **Blip behavior**: Bright flash on sweep contact, immediate exponential decay, refresh on next sweep passage

Arduino radar projects commonly use HC-SR04 ultrasonic sensors with servo sweeps, sending angle/distance pairs to Processing for visualization. The key insight for authentic appearance is that **both spatial and temporal decay should be exponential, not Gaussian**—this creates the characteristic "comet tail" trailing appearance rather than a soft blur.

For actual P7 CRT authenticity, note that genuine P7 tubes (5FP7A military surplus) are "famously difficult to come by." Modern LED implementations can achieve remarkable authenticity through proper decay mathematics, dual-color simulation (blue flash → yellow persist), and realistic timing relationships. The Blur Busters CRT Simulator and RetroArch's crt-geom-deluxe shader provide reference implementations worth studying.

---

## Conclusion

The authentic PPI radar display is defined by its **temporal behavior** more than its static appearance—the cascade phosphor's dual-color decay, the once-per-rotation target refresh, the integration of signal over noise through physical persistence. For LED art capturing this authenticity: implement exponential decay (not linear), include the blue-to-yellow color shift of P7, time your rotation period to 2-5 seconds, ensure targets persist visibly between sweeps, and add realistic noise that self-cancels through accumulation. The eerie glow operators remember came from physics operating at the boundary of perception—signals held just barely visible in decaying phosphorescence, refreshed moments before fading completely.