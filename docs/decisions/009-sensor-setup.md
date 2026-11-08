# 009: Sensor setup, reading temp, pH, and EC

**Date:** 7Nov26
**Status:** Accepted

## Context

Need to get the three solution sensors reading: temperature, pH, and EC. This
covers which pins they land on, what order they get read in, how they're
calibrated, and how bad readings get caught before they can drive a dose.

Extends 003 (probe isolation), which covered physical placement and grounding.
003 said software would read one sensor at a time, which turns out to be
necessary but not sufficient - see the cross-interference decision below.

Everything needed is already bought (see BOM):

- DFRobot Gravity pH Kit V2 (SEN0161-V2), analog, probe + conditioning board
- DFRobot Gravity EC sensor K=1 (DFR0300), analog
- Waterproof DS18B20 solution temp probe, 1-Wire
- pH 4.01 and 7.00 buffers, one 1413 uS/cm EC standard, KCl storage solution

## Options considered

**Temp sensor:** analog NTC thermistor on the spare ADC channel, vs. the
DS18B20 1-Wire probe.

**Probe cross-interference:** leave both analog boards powered and read them
sequentially; power-gate the EC board so it's only energized during its own
read; or skip ahead to the isolated Atlas EZO boards.

**Aeration during reads:** leave the air pump running and filter the noise out,
vs. pausing it long enough for bubbles to clear off the probes.

**EC calibration:** single point against the 1413 uS/cm standard I have, vs.
buying the 12.88 mS standard for a two-point fit.

**Calibration storage:** hardcoded constants, a flash sector, the SD card, or
backup SRAM on VBAT.

## Decision

### Pins

| Sensor | Interface | Pin | Notes |
|---|---|---|---|
| pH | Analog | PC0 | ADC123_IN10 |
| EC | Analog | PC1 | ADC123_IN11 |
| Temp | 1-Wire | TBD, from spares | 4.7k pull-up to 3.3V |
| EC power gate | GPIO out | TBD, from spares | Small MOSFET, low-side |

Temp is digital, so it does **not** use an ADC channel. PC2 stays free for the
V2 light sensor as originally planned. The two new pins come out of the spares
list (PA0, PA1, PC3, PC4, PC5, PC9, PB8, PB9, PB11); picking them is a wiring
decision, not a design one.

Both DFRobot boards run on 3.3V, not 5V. PC0-PC5 are not 5V tolerant, and at
5V supply these boards can output above 3.3V. Running them off the same 3.3V
rail as VDDA also makes their output partly ratiometric, so reference error
partially cancels. **Open item:** verify DFR0300's minimum supply voltage on
the board itself. If it turns out to be 5V only, it needs a divider on its
output.

### Temp sensor: DS18B20

Chosen over an analog thermistor. It's digital, so pump switching noise can't
corrupt it, it's the standard waterproof probe for this application, it frees
the third ADC channel, and +/-0.5C is well inside what EC compensation needs
(2%/C x 0.5C is about 1% EC error, below the probe's own accuracy). Cost is
having to write a 1-Wire driver, which is a one-time annoyance.

Check continuity from the probe's stainless sheath to its ground lead before
installing. Most sealed probes float. If this one bonds the sheath to ground,
it puts a grounded electrode in the water and will disturb EC readings.

### Cross-interference: power-gate the EC board

The DFR0300 drives AC excitation current through the water to measure
conductivity. The pH probe sits in that same water measuring millivolts. The
EC board injects that current whenever it is **powered**, not only when its
output is being sampled, so reading the two sequentially off a continuously
powered pair of boards still gives a corrupted pH reading.

One GPIO plus a small MOSFET switches the EC board's supply. It gets energized
only for its own portion of the read cycle. Gate EC rather than pH, since EC is
the aggressor. Allow 1-2 s of settling after power-up for its op-amps and
excitation to stabilize.

The Atlas EZO boards solve this in hardware by being galvanically isolated,
which is one of the reasons they cost what they do. Deferred to V2 per 001;
power-gating gets the same result in V1 for the price of a MOSFET.

### Read cycle

Non-blocking state machine driven off HAL_GetTick, called once per main-loop
pass, same pattern as pump_control:

1. Wait out the cycle interval (5-10 min; reservoir chemistry doesn't move fast)
2. Preconditions: no pump running, tank not low
3. Stop the air pump, wait 20-30 s for bubbles to clear the probes
4. DS18B20 convert (750 ms at 12-bit), read scratchpad, verify CRC
5. Sample pH on PC0 with the EC board still off, median of N samples
6. Power up the EC board, settle, sample PC1, median of N samples
7. Power the EC board back down, restart the air pump
8. Convert, temperature-compensate, validate, publish

Aeration pauses because a bubble sitting on an EC electrode reads as low
conductivity, and median filtering can't remove a bubble that stays put. At a
10-minute cycle that's about 5% aeration downtime, which is acceptable.

Never read while a pump is running: electrical noise, plus the dosed slug
hasn't mixed yet.

### Calibration

**pH, two-point** from the 4.01 and 7.00 buffers. Store the measured voltage at
each; derive slope and offset from those rather than trusting DFRobot's library
defaults, which are per-board and per-probe:

    slope = (V_4.01 - V_7.00) / (4.01 - 7.00)
    pH    = 7.00 + (V - V_7.00) / slope

**EC, single-point** against the 1413 uS/cm standard. One-point calibration is
most accurate near the standard, and the target band for herbs is 1.0-1.4
mS/cm, right on top of it. The 12.88 mS standard would only matter for
concentrations this build never sees, so it isn't being bought.

**EC temperature compensation is mandatory**, roughly 2%/C, normalized to 25C:

    ec_25 = ec_raw / (1 + 0.02 * (temp_c - 25))

Every EC standard and every nutrient recipe is specified at 25C, so without
this the numbers aren't comparable to anything.

**pH temperature compensation** applies to the electrode slope only, which
scales with absolute temperature: slope(T) = slope_25 * (T + 273.15) / 298.15.
Worth about +/-1% over 18-28C. Note that the solution's actual pH also shifts
with temperature; that's a real change to measure, not something to compensate
away.

Calibration constants are hardcoded in a struct for V1, recompile to
recalibrate. The struct gets designed so it can move to a flash sector
unchanged later. Not building the persistence until the procedure has been run
by hand a few times and it's clear what's worth storing. SD card is for logs,
not for config that has to exist at boot.

### Error checking

Every reading carries a value, the tick it was taken at, and a status of OK,
STALE, or FAULT. The timestamp and status are what let dosing refuse to run on
bad or old data, which is the whole point. Checks, in rough order of how much
garbage each catches:

1. **Raw voltage window.** Outside about 0.1-3.2V means a disconnected or
   shorted probe. At 12 bits that's counts below ~124 or above ~3970.
2. **Sample dispersion.** Track the spread across the burst. High spread means
   noise or bubbles; discard the whole cycle rather than publish a bad average.
3. **Rate of change.** In 30 L with aeration, pH physically can't move more
   than about 0.1-0.2 per minute without a dose. This catches most
   plausible-looking garbage that the range checks let through.
4. **Physical range.** pH 0-14 hard and 3.0-9.0 soft, EC 0-5 mS/cm, temp 5-40C.
5. **DS18B20 specifics.** Verify the scratchpad CRC8, and treat exactly 85.0C
   as suspect since it's the power-on default, meaning the conversion didn't
   finish.
6. **Dependency propagation.** If temp is FAULT then EC is degraded, not OK,
   because its compensation is invalid.

One bad cycle retries silently. Three consecutive bad cycles latch a fault,
which lights the system red LED and inhibits all dosing. This is what the
system LED is actually for; the current code uses it to indicate a pump running
for bench debug.

### Calibration monitoring

**pH slope percentage** is the metric for probe health. Record the slope from
the first calibration with a fresh probe and express every later calibration as
a fraction of it:

| Slope vs. fresh | Meaning |
|---|---|
| >95% | healthy |
| 90-95% | normal aging |
| 85-90% | warn, order a replacement |
| <85% | replace, readings not trustworthy |

**pH offset at 7.00** drifting indicates a contaminated reference junction,
which is often cleanable while the slope is still good. Track it separately
from slope.

**EC cell constant** drifting more than about 10% between calibrations means
the probe is fouled. Clean with dilute acid and recalibrate; replace if it
doesn't recover. Expect to clean it monthly in nutrient solution.

**Open item: calibration age.** pH probes in continuous use want recalibration
every 2-4 weeks, but tracking that needs a clock that survives power cycles.
PC14/PC15 are being held for an LSE crystal on the assumption that RTC
scheduling is a V2 grow-light concern - calibration aging is a V1 reason to
want it sooner. Three paths: populate LSE plus a VBAT cell now, accumulate
run-hours in a flash counter and warn past ~336 hours, or just print age since
boot and accept the reset. Leaning toward the run-hours counter. Not decided.

### Supporting changes

Use CubeMX for the ADC rather than hand-rolling it. ADC init is fiddly enough
(prescaler, resolution, scan mode, per-channel sampling times) that the
generator earns its keep, and it starts pulling the .ioc back toward reality -
it currently knows about 7 pins out of 32. Enable the VREFINT channel in the
same pass: measuring actual VDDA corrects for a USB-fed rail that sags, which
is worth about 0.1 pH.

Enable USART2 at the same time. It's physically wired to the ST-LINK virtual
COM port and currently unconfigured, and there is no way to calibrate anything
without being able to see a number.

Move the main loop off HAL_Delay(50) to a tick-based schedule. Loop execution
time stops being negligible once ADC and 1-Wire work lands, and both the switch
debounce and the dose duration granularity assume a real 50 ms period.

## Why

Covered inline above, since most of these choices only make sense next to the
constraint that drove them. The short version: temperature isn't a display
value, it's an input to the EC calculation, so it has to be read first and its
failure has to propagate. The pH and EC probes share the water as a conductive
path, so they can't be energized at the same time. And validation is only
useful if the dosing logic can tell a fresh reading from a stale one, which is
why every reading carries a timestamp and a status rather than just a float.
