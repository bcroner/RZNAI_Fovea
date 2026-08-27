# Integration harness — foveal stereo into the real RZNAI_AGI

Links the patched `RZNAI_AGI.cpp` against this engine, replaces the simulation
stubs `in_0()` / `in_1()` with the foveal stereo stream, and drives `cycle()`
for a bounded number of cycles.

```bash
.\harness\build_harness.bat [path-to-RZNAI_AGI] [cycles]
.\build\rzn_harness.exe -w 64 -h 48
```

Defaults to `..\..\RZNAI_AGI` and 2000 cycles. **All four patches in
`../patches/` must be applied** or the model will not compile or will fault.

## How the wiring works

`read_sensory()` builds the final word itself:

```
word = (reading << sensory_bits | sensor_id) << 1 | source_flag
```

With `sensory_bits = 1` that is `reading << 2 | sensor << 1 | 0` — exactly the
low end of a profile-16 packed word. So a reading is one of this engine's words
with its sensor and source bits stripped:

```c
reading = word >> RZN_PAYLOAD_SHIFT     /* = word >> 2 */
```

carrying the payload and the tag, and `read_sensory()` rebuilds the rest. No
translation layer, no reinterpretation — the model's own packing produces
exactly the profile-16 layout.

`rzn_agi_bridge.c` renders frames on demand as the model drains the stream, so
`cycle()` never waits and never underruns.

## Measured: 50 000 cycles, 64×48 stereo

```
readings served     50000
  index words        4820
  left  colour      24055
  right colour      21125
frames rendered       193
engine words out    50125
underruns               0

Current_Input       12626 (0x00003152)
Input_Queue         00003152 00003152 00003152 00003596 00001026 00000d56 0000314e
```

Real foveal stereo data reaches the model and populates its input queue. Exit
code 0. **The wiring works.**

## Two findings that matter more than the wiring

### 1. The model's output does not vary with its input

The model chooses which camera to read next from its own output
(`sensor = (output >> 1) & sensor_mask`), which makes the requested-sensor
histogram a free probe of whether the network is responding to anything:

```
asked for left          1
asked for right     49999
```

It asked for the right camera on 49 999 of 50 000 cycles. Its output is
effectively constant, so **the input is not influencing the network's
behaviour**.

Some of this was `perform_iann()` unconditionally returning
`(1 << out_sz) - 1` — fixed in patch 0003 — but the output is still not
varying afterwards. With every weight initialised to ±16384 and every target
to `j % hidden_sz`, the network is close to symmetric by construction, so this
may be the initialisation rather than a further bug.

**What this means for the packing decision:** the question "can the AGI work
with RGB444, or does it need RGB888?" cannot be answered yet. It is not a
question about the transport — profile 16 delivers exactly what it claims —
it is blocked on the model producing input-dependent behaviour at all.

### 2. Sensor selection and a foveal stream pull in different directions

```
request vs word tag 24055 disagreements
```

Every left-camera word arrived while the model was asking for the right camera.

This is structural, not a bug in either side. The model expects to *choose* a
sensor each cycle; a foveal stream is inherently sequential — an index word
belongs to both cameras at once, and a delta burst names whichever camera
actually changed. The bridge serves the next word regardless of the request,
and the word's own tag stays authoritative, so no data is lost or
misattributed. But the sensor bit `read_sensory()` writes is the model's
request, not the camera the value came from.

Three ways to resolve it, in rough order of preference:

1. **Let the tag carry sensor identity and stop using the sensor bit for it.**
   The tag already distinguishes `LEFT` from `RIGHT` unambiguously. Cheapest,
   and it matches how the stream actually behaves.
2. **Buffer per camera** and serve whichever the model asks for. Keeps the
   model's selection meaningful, but re-orders the foveal stream and so
   partially defeats fovea-first ordering.
3. **Treat the request as advisory** and leave it as it is, accepting that the
   sensor bit is noise on colour words.

This is a design decision about the model's sensor interface, not something
the engine should settle on its own.
