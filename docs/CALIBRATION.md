# Calibration Guide

CSI-based sensing measures how much the Wi-Fi channel *fluctuates* compared to
a learned "empty room" baseline. Because every room has a different RF
signature, a short calibration makes the difference between reliable detection
and constant false alarms. This guide explains placement, the calibration
procedure, and how to tune the thresholds.

---

## 1. How detection works (just enough theory)

For every Wi-Fi packet, the firmware extracts a compact CSI feature (mean
subcarrier amplitude and its spatial spread) and pushes it into a circular
buffer. The [motion engine](ARCHITECTURE.md#5-the-signal-processing-pipeline)
then:

1. **De-noises** the stream (median filter + moving average).
2. Computes the **temporal variance** over a sliding window - this is the raw
   "how much is the channel moving" feature.
3. Tracks a slow **adaptive baseline** of that variance while the room looks
   empty (the baseline is *frozen* during motion so a moving person is never
   learned as background).
4. **Normalises** the excess variance into a `0.00 - 1.00` **motion score**
   that is independent of absolute signal magnitude.
5. Smooths the score and runs it through a **debounced state machine**:

   | Score vs. threshold | State |
   |---------------------|-------|
   | below presence      | **Idle** |
   | ≥ presence (`0.12`) | **Presence** |
   | ≥ motion (`0.35`)   | **Motion** |
   | ≥ high motion (`0.70`) | **High Motion** |

Thresholds shown are the compile-time defaults from
[`app_config.h`](../firmware/main/app_config.h); all three are editable at
runtime under **Settings → Detection**.

> [!NOTE]
> CSI needs a steady stream of packets. On an idle network there may be none,
> so the firmware runs a lightweight **ICMP traffic generator** that pings the
> gateway at the configured **sampling rate** (default 20 Hz) purely to keep
> CSI flowing. This is normal and uses negligible bandwidth.

---

## 2. Placement

Placement matters more than any threshold. Aim for the person to cross the
**line of sight** between the ESP32 and the access point / transmitter.

**Do**

- Put the ESP32 **3-8 m** from your Wi-Fi router, with the monitored area
  *between* them.
- Mount it in the open (a shelf, a wall), antenna unobstructed.
- Keep it powered from a stable USB supply (brown-outs corrupt CSI).
- Keep it **stationary** - the device must not move or vibrate.

**Avoid**

- Enclosing it in metal, or placing it behind large metal objects.
- Right next to the router (too little multipath) or too far (too few packets).
- Sources of RF churn you don't care about: fans, pets in the beam, a busy
  microwave, moving curtains near an open window.

---

## 3. Calibration procedure

Calibration learns the empty-room baseline. Do it **after** final placement and
after the device has joined your normal Wi-Fi.

1. **Leave the area.** The space between the sensor and the router must be
   genuinely empty and still. If possible, step out of the room.
2. Open the dashboard on a device **outside** the monitored area (your phone on
   the far side of a wall is ideal).
3. Go to **Settings → Recalibrate now** (or `POST /api/calibrate`). This resets
   the state machine to Idle and reseeds the baseline from the current window.
4. **Wait 30-60 seconds.** Watch the **Dashboard**:
   - **CSI Variance** should settle to a small, stable value.
   - **Motion Score** should sit low and the state should read **Idle**.
   - The **baseline** value on the CSI-variance card should track just above
     the resting variance.
5. **Walk test.** Enter the monitored area and move around. The score should
   climb through **Presence → Motion**, and vigorous movement should reach
   **High Motion**. Leave again; after the fall-debounce delay the state should
   return to Idle.

If idle is stable and the walk test transitions cleanly, you're done. If not,
tune the thresholds below.

> **Auto-calibration** (Settings) keeps the baseline adapting slowly while the
> room is idle, which compensates for gradual RF drift. Leave it **on** for
> set-and-forget use. Turn it **off** if you are experimenting and want a fixed
> baseline. The baseline never adapts while motion is asserted.

---

## 4. Tuning the thresholds

All values are the normalised score, `0.00`-`1.00`. Change one thing at a time
and repeat the walk test.

| Symptom | Adjustment |
|---------|-----------|
| **False presence** when the room is empty | Raise **Presence threshold** (e.g. `0.12 → 0.18`). |
| **Misses a still person** / slow to detect entry | Lower **Presence threshold** (e.g. `0.12 → 0.08`). |
| Gentle movement already trips **Motion** | Raise **Motion threshold**. |
| Only very large movements register as **Motion** | Lower **Motion threshold**. |
| **High Motion** never triggers / triggers too easily | Adjust **High-motion threshold**. |
| Detection feels **twitchy** (flips state rapidly) | Raise thresholds slightly; the built-in debounce (rise/fall) and hysteresis already damp this. |
| Detection feels **laggy** | Increase **Sampling rate** (more packets ⇒ faster statistics), or lower thresholds. |

Keep the ordering **presence < motion < high motion**; the firmware clamps
values to valid ranges but does not reorder them.

### Sampling rate

Higher **Sampling rate** (Hz) gives more CSI packets per second and therefore
faster, smoother statistics, at the cost of a little more CPU and bandwidth.
Range is 5-100 Hz; 20 Hz is a good default. Very high rates on a congested
2.4 GHz band can be counter-productive.

---

## 5. Advanced: compile-time tuning

For behaviours not exposed in the UI, edit
[`app_config.h`](../firmware/main/app_config.h) and rebuild. The most useful:

| Constant | Effect |
|----------|--------|
| `NEXUS_CSI_WINDOW_SIZE` | Samples in the variance window. Larger = smoother but slower. |
| `NEXUS_LPF_ALPHA` | Score low-pass factor (higher = more responsive). |
| `NEXUS_BASELINE_ALPHA` | How fast the idle baseline adapts (higher = faster drift tracking). |
| `NEXUS_NORM_SENSITIVITY` | Normalisation denominator scale (higher = *less* sensitive overall). |
| `NEXUS_DEBOUNCE_RISE` / `NEXUS_DEBOUNCE_FALL` | Consecutive windows required to enter / leave a state. |
| `NEXUS_THRESHOLD_HYSTERESIS` | Gap subtracted from a threshold before dropping below it. |

Every one of these is documented inline in the header.

---

## 6. Re-calibrate when…

- You **move** the sensor or the router.
- You significantly rearrange the room (large furniture, metal objects).
- The RF environment changes (new strong neighbouring APs).
- You see a gradual rise in idle false positives (or just leave
  auto-calibration on to handle slow drift automatically).
