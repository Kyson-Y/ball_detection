# 513X low-speed synchronous-start tuning

Date: 2026-07-27
Scope: suspended-wheel speed loop only

## Goal

Command the same low wheel speed through the closed-loop interface, start both
wheels together, and keep both wheels continuously rotating. This is not an
open-loop low-duty test.

## Fault and correction

Rest-to-8 rpm already started both wheels at 10 ms with zero start skew. The
failure was a 20-to-8 rpm transition: the right feedforward slope was too high,
the controller accumulated a large opposing integrator during deceleration,
and the right wheel periodically stopped and triggered a 60% recovery boost.

The right feedforward was refitted from measured 8/20/60 rpm holding output to
`371.0 + 2.17*rpm`. The speed controller now recognizes a same-direction,
non-zero down-step, clears an opposing old integrator, and holds reverse
integration until measured speed enters the existing 3 rpm target neighborhood.
Normal PI then resumes. The common rest-start boost is unchanged.

## Verified results

| Test | Left | Right | Synchronization / continuity |
| --- | ---: | ---: | --- |
| Rest -> 8 rpm | start 10 ms; tail 8.023 rpm | start 10 ms; tail 8.049 rpm | 0 ms skew; 499/500 both moving |
| 20 -> 8 rpm | t90 360 ms; tail 7.849 rpm | t90 390 ms; tail 7.935 rpm | 30 ms skew; 499/499 both moving; 0 recovery boosts |
| 8 -> 20 rpm | t90 270 ms; tail 20.192 rpm | t90 260 ms; tail 20.055 rpm | 10 ms skew |

Battery telemetry stayed near 16.57 V. Device active/sticky issues, deadline
misses, internal drops, encoder-late count, and final actuator output were zero.
Some captures contained 1-2 host-side CRC gaps; valid control frames and device
Health were used for analysis. The clean 8-to-20 capture had zero CRC/gaps.

## Frozen baseline

- Profile: `513X-4S v13`
- Minimum suspended-wheel continuous speed: `8 rpm`
- `5 rpm`: pulsed crawl only, not smooth continuous PI
- HEX SHA-256: `4BD5761A8C53A2F0DD0D4DED0D87065E6D5699498A11C3645727876C5E59CDF0`
- Final actuator output: zero
- Remaining validation: on-floor minimum speed, straight-line tracking, load,
  current, temperature, and battery-voltage sweep
