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

## Two findings that mattered more than the wiring — both now fixed

Everything below is kept as the investigation record: what was measured, what
it turned out to be, and how it was settled. Sections tagged RESOLVED or
"Status: fixed" carry the outcome. `main` at 52cc546 has all five PRs merged;
the model compiles, runs, recalls, and responds to what it sees.


### 0. Initialisation experiment — see `init_experiment.cpp`

```bash
.\harness\build_experiment.bat
.\build\rzn_experiment.exe 20000 [sensor-bit]
```

`instantiate()` seeds every weight and target from the fan-out index alone:

```cpp
input_weights[i][j] = (j % 2 == 0 ? -16384 : 16384);
input_targets[i][j] =  j % hidden_sz;
```

Neither depends on the source unit `i`, so every source contributes an
identical vector and *which* input bit was active cannot affect the result —
only *how many* were. Working it through by hand: `weight_sums[k]` becomes
`(active count) x (k even ? -16384 : +16384)`, so even units never fire and odd
ones always do, the output is always `14`, and
`sensor = (14 >> 1) & 1 = 1` — exactly the "asked for right 49999" observed
below.

The experiment rewrites weights and targets in place after `instantiate()` and
calls `perform_iann()` directly, so it measures the network without `cycle()`
in the way. **The model source is not modified.** 20 000 real foveal inputs
containing 502 distinct values:

| scheme | distinct outputs | layer-0 patterns | final patterns | output bits that move |
|---|---|---|---|---|
| shipped (control) | **1** (`14`) | 1 | 1 | none |
| targets vary by source | **12** | 10366 | 10366 | 0 1 2 3 |
| weights vary by source | 2 | 10368 | 1103 | 1 |
| both vary by source | 8 | 10369 | 10369 | 0 1 2 3 |
| both vary + graded magnitudes | 7 | 10370 | 10370 | 0 1 2 3 |

**Confirmed.** As shipped the network cannot respond to its input at all — one
output value, no bit ever moves. Making targets depend on the source is both
necessary and sufficient; varying weights alone leaves the network almost as
flat (2 outputs, only bit 1 moving, and not until trial 7830).

Two cautions about reading this table:

- Deriving a target and a weight sign from the *same* random value silently
  re-flattens the network: `hidden_sz` is even, so `r % hidden_sz` has the same
  parity as `r`, which locks every even-numbered unit to negative weights. An
  earlier version of this experiment did exactly that and reported a spurious
  collapse. `h2w()` exists to keep the two draws independent.
- Distinct output *values* is the wrong headline number on its own. `cycle()`
  acts on bit 1 (next sensor) and bit 0 (recall flag), so a scheme can produce
  several output values while the bits the model actually acts on never move.
  The last column is the one that matters.

### 0b. Resolved: the fix does work, and the probe was blind

An earlier revision of this file reported that `rzn_harness -fix-init` changed
nothing in a live `cycle()` run. **That was wrong, and the mistake was in the
measurement.** Tracing the actual output values inside `cycle()` shows:

```
as shipped   cycle 0 out=14   1 out=14   2 out=14   3 out=14  ...
-fix-init    cycle 0 out=11   1 out=11   2 out= 3   3 out= 3  ...
```

The fix changes the network's behaviour immediately. The sensor histogram
could not see it because `sensor = (output >> 1) & 1` observes **bit 1 only**,
and bit 1 is set in `14`, `11` and `3` alike. A one-bit probe reported "no
change" for a network whose output had changed completely. `-no-reinforce`
(zeroing `inc_amt`/`dec_amt`) likewise changes nothing measurable, so the
reward path is *not* what holds bit 1 fixed either.

### 0c. The learning rule has the same flaw as the initialisation

Every reinforcement site keys the direction on the fan-out index:

```cpp
line 826:  input_weights[i][k]     += (k % 2 == 0 ? -inc_amt : inc_amt);
line 832:  hidden[i]->weights[j][k]+= (k % 2 == 0 ? -inc_amt : inc_amt);
line 841:  output_weights[i][j]    += (j % 2 == 0 ? -inc_amt : inc_amt);
```

and the three disincentive sites (898, 904, 913) mirror them. None depends on
the source unit, the target unit, or *which* input caused the firing — so a
reward cannot strengthen the pathway that earned it. It just pushes weights
apart by fan-out parity, which is the same degenerate structure
`instantiate()` starts from. Fixing the initialisation is therefore necessary
but not sufficient: every reward nudges the network back toward parity.

### 0d. The recall path is unreachable

```cpp
line 706:  bool in_read_from_recall = false;   // never assigned again
line 707:  bool read_from_recall_input = false; // never assigned again
line 731:  if (!in_read_from_recall)            // therefore always true
```

`in_read_from_recall` is declared `false` and never written. The model always
reads from sensors and **never from recall**, so `read_from_recall_new()`,
`read_from_recall_next()`, `generateBFSs()`, `executeBFS()` and every
Knowledge Bank lookup are unreachable.

`out_read_from_recall = output & 0x1` computes the decision every cycle, and is
used only to gate `handle_output()`. It is never propagated to
`in_read_from_recall` — the missing line looks like
`in_read_from_recall = out_read_from_recall;` at the end of the loop body.

The consequence is larger than the constant output: `create_dict_entry()`
writes to the Knowledge Bank every cycle, and nothing ever reads it back. As it
stands the model has no working memory, so it cannot use anything it records
about the foveal stream.

**Status: fixed.** [PR #3](https://github.com/bcroner/RZNAI_AGI/pull/3) repaired
the memory-safety defects; [PR #4](https://github.com/bcroner/RZNAI_AGI/pull/4)
turned recall on. **The model can now read back the bank it writes.**

The two decisions that were open, and how they were settled:

1. **State-to-index mapping** — an open-addressed map keyed by state value,
   storing the key beside the value so states that hash alike resolve rather
   than being conflated. Power-of-two capacity doubling at half load; lookup
   stays O(1) and it holds as many states as the bank accumulates. `kbsts` is
   no longer load-bearing. The shared `Simp_Queue` is not used inside the
   search either — `simp_queue_dequeue` walks to the tail on every pop, which
   would make the BFS quadratic in the size of the bank.
2. **Sensory/recall alternation** — recall is a bounded excursion, never an
   absorbing state. At most `In_Q_ct - 1` in a row, so at least one genuine
   observation always remains in `Input_Queue`; and a recall yielding `0`
   returns to the sensors immediately. A robot must not go blind, and
   unproductive introspection should yield to perception.

Turning it on exposed five defects in code that had never run — the Knowledge
Bank's edges pointed from input states to 4-bit action values so the graph was
untraversable; state identity kept a flag bit so values doubled on every recall
round trip until they overflowed into the sign bit; bucket lookups were
unguarded against negative operands; the disincentive branch added and removed
from the same vector; and `get_rw`/`get_dv` were inverted so reward and
disincentive fought every cycle. All are in PR #4.

### Measured — 50 000 cycles on real foveal stereo

| sensor | sensory | recall | KB entries | recall probe |
|---|---|---|---|---|
| 64×48 | 25000 | 25000 | 25522 | route recovered |
| 96×72 | 22248 | 27752 | 10134 | route recovered |
| 160×120 | 25000 | 25000 | 15190 | route recovered |

`rzn_harness` ends with a recall probe: it seeds a goal from an edge the model
genuinely recorded and asks `generateBFSs()` to find its way back, exercising
`executeBFS()` end to end against a bank built from live camera data.

**Still open, and independent of all of this:** with the shipped initialisation
the model never *requests* recall, because its output is constant (see 0a–0c
above). Recall engages the moment the output varies — `-fix-init` demonstrates
exactly that, which is where the numbers in the table come from.

### 1. RESOLVED: the model's output now varies with its input

This section previously recorded that the model's output was constant. It is
not any more. [PR #5](https://github.com/bcroner/RZNAI_AGI/pull/5) fixed the
initialisation and the reinforcement rule together, which had to happen
together: initialisation set the degenerate structure up, and reinforcement
pushed it back.

Same 20 000 real foveal inputs, shipped initialisation, no test-only overrides:

| | before | after |
|---|---|---|
| distinct outputs | **1** (`14`) | **8** |
| layer-0 firing patterns | 1 | 10368 |
| final-layer patterns | 1 | 10367 |
| output bits that move | none | 1, 2, 3 |

In a live run the sensor choice — output bit 1 — now varies instead of being
pinned. 50 000 cycles, default initialisation:

| sensor | sensory | recall | asked left | asked right | KB entries | recall probe |
|---|---|---|---|---|---|---|
| 64×48 | 25000 | 25000 | 50 | 24950 | 10228 | route recovered |
| 96×72 | 25000 | 25000 | 79 | 24921 | 7155 | route recovered |
| 160×120 | 25000 | 25000 | 62 | 24938 | 7942 | route recovered |
| 320×240 | 25000 | 25000 | 40 | 24960 | 6920 | route recovered |

Weights and targets are now drawn from a deterministic mixer over
`(source, slot)` with **separate salts** for target and weight — `hidden_sz` is
even, so a shared draw locks target parity to weight sign and re-flattens the
network. Reinforcement strengthens a connection when it pushed its target the
way the target actually went. `-fix-init` is left in the harness as a
comparison lever; it is no longer needed to make the model respond.

**So the RGB444 question is finally answerable.** The transport was never the
constraint — profile 16 delivers what it claims — and the model now produces
input-dependent behaviour to measure against.

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
