# RGB444 vs RGB888 — does the AGI need 8-bit colour?

The question the packing-profile decision was parked on, now answerable because
the model finally produces input-dependent behaviour.

**Short answer: yes, RGB888 measurably changes what the model does — and the
difference is in stereo behaviour specifically, which is the whole point of the
rig. It costs 3.8× the compute per cycle and nothing in bandwidth.**

## Setting it up honestly

A naive `PROFILE=32` run is rigged against RGB888. `perform_iann()` decodes
`in_sz` bits per word, and `in_sz` was fixed at 16. A profile-32 word puts its
payload at bits 28..2, so the model would read bits 15..0 — part of one colour
channel instead of all three — while the engine dutifully delivered four times
as much information.

So the comparison needs the model widened to match:
[PR #6](https://github.com/bcroner/RZNAI_AGI/pull/6) makes `in_sz` a
compile-time knob. `hidden_sz` is `in_sz * In_Q_ct * 2`, so `in_sz = 32` doubles
the hidden layer from 224 to 448 units.

```bash
set PROFILE=16 & set INSZ=16 & .\harness\build_harness.bat "" 20000
set PROFILE=32 & set INSZ=32 & .\harness\build_harness.bat "" 20000
```

## Information reaching the network

20 000 real foveal stereo inputs, `perform_iann()` called directly:

| configuration | distinct inputs | outputs | layer-0 patterns | final patterns | output bits that move |
|---|---|---|---|---|---|
| profile 16 / `in_sz` 16 — **RGB444** | 502 | 8 | 10368 | 10367 | 1, 2, 3 |
| profile 32 / `in_sz` 32 — **RGB888** | 2002 | **11** | **14334** | **14334** | **0, 1, 2, 3** |
| profile 32 / `in_sz` 16 — naive | 2002 | 8 | 10597 | 10597 | 1, 2, 3 |

RGB888 carries **4× the distinct input values**. The third row is the control
that proves the knob was necessary: same 2002 distinct inputs, but the model
behaves exactly as it did on the narrow packing, because the extra payload never
reaches it.

Note which bits move. Under RGB444 **bit 0 never changes** — that is
`out_read_from_recall`, the model's decision to consult memory. Under RGB888 it
varies, so the model actually chooses when to recall instead of having the
choice made for it by the alternation cap.

## Live behaviour — 20 000 cycles, 64×48 stereo

| | RGB444 | RGB888 |
|---|---|---|
| `hidden_sz` | 224 | 448 |
| sensory / recall cycles | 10000 / 10000 | 10003 / 9997 |
| **asked for left camera** | **32** | **5828** |
| **asked for right camera** | **9968** | **4175** |
| Knowledge Bank entries | 3796 | **10500** |
| wall clock | 18.5 s | 70.2 s |

**This is the finding that matters.** Under RGB444 the model asks for the left
camera 32 times in 10 000 — 0.3%. It is effectively stuck on one eye. Under
RGB888 it alternates, 58% / 42%.

For a stereo rig that is the difference between a model that samples both
cameras and one that does not. The engine was feeding both eyes correctly in
both cases; only under RGB888 does the model act on the distinction.

It also learns 2.8× more distinct state transitions (10 500 vs 3796 Knowledge
Bank entries) from the same 19 frames of video.

## What it costs

**Nothing in bandwidth.** Engine-side, 640×480 over 30 frames:

| profile | words emitted | vs dense |
|---|---|---|
| 16 (RGB444) | 1 100 831 | 16.7× |
| 32 (RGB888) | 1 096 164 | 16.8× |

RGB444 is *marginally worse* — 0.4% more words — because its 12-bit payload
needs two words for a spiral index above 4096. Colour depth is free on the wire.

**3.8× in compute.** 18.5 s → 70.2 s for 20 000 cycles, and every weight and
target array doubles. That is `hidden_sz` going from 224 to 448, not the
packing.

## Recommendation

**Switch to profile 32 with `in_sz = 32`** unless the robot's compute budget
cannot absorb roughly 4× per cycle.

The reasoning is not "more bits are better". It is that the model's stereo
behaviour changes qualitatively: under RGB444 it stops distinguishing the two
cameras, and a stereo vision system whose model only ever looks through one eye
has given up the thing the second camera was for. RGB444 also pins the model's
recall decision, handing that choice to the alternation cap rather than the
model.

If compute is the binding constraint, the better trade is to keep `in_sz = 32`
and reduce `In_Q_ct` or `hidden_ct` instead — those shrink the network without
taking colour discrimination away from it. Worth measuring before committing.

## Reproducing

```bash
set PROFILE=32 & set INSZ=32 & .\harness\build_experiment.bat
.\build\rzn_experiment.exe 20000 1

set PROFILE=32 & set INSZ=32 & .\harness\build_harness.bat "" 20000
.\build\rzn_harness.exe -w 64 -h 48
```

`PROFILE` selects the engine's packing, `INSZ` the model's decode width. They
should match: 16/16 or 32/32. Mismatched pairs are the naive control above.
