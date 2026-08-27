# Patches for RZNAI_AGI

Four commits against [bcroner/RZNAI_AGI](https://github.com/bcroner/RZNAI_AGI)
at `cba1103`, split across two branches so the high-confidence work can land
without the inferred work.

```bash
git am patches/0001-*.patch patches/0002-*.patch   # branch fix/input-word-assembly
git am patches/0003-*.patch patches/0004-*.patch   # branch fix/runtime-crashes
```

| # | commit | confidence |
|---|---|---|
| 0001 | Fix input-word assembly | high — the layout is corroborated by four existing sites |
| 0002 | Integration knobs for sensors, driver, cycle limit | high — additive, defaults unchanged |
| 0003 | Fix crashes and out-of-bounds accesses | **mixed** — see below |
| 0004 | Fix `simp_vector_append` | high — unambiguous pointer bug |

---

## Before and after

**Before:** `RZNAI_AGI.cpp` did not compile.

```
RZNAI_AGI.cpp(402): warning C4552: '>>': result of expression not used
RZNAI_AGI.cpp(590): error C2059: syntax error: '|'
RZNAI_AGI.cpp(688): error C2660: 'read_from_recall_new': does not take 3 arguments
RZNAI_AGI.cpp(690): error C2660: 'read_from_recall_next': does not take 3 arguments
```

Made to compile, it faulted immediately (`0xC0000005`) on `cycle()`'s first
statement, because `Input_Queue` is never allocated.

**After:** compiles clean at `/W3`, and a 50 000-cycle run driven by real
foveal stereo input completes with exit 0.

---

## 0001 — the bit layout

Four sites already agree on the input-word layout; `read_sensory()` was the
odd one out, and now matches them.

| bits | meaning |
|---|---|
| 0 | source flag — `1` = read from recall, `0` = read from a sensor |
| 1 … `sensory_bits` | sensor id, or for recall words the rewards/disincentives bit |
| above that | the payload |

The evidence, all pre-existing:

- `cycle()`: `create_dict_entry(..., input >> 1, ...)` and
  `out_read_from_recall = output & 0x1`
- `cycle()`: `recall_rwdv = (output >> 1) & 0x1;` with the comment *"least
  significant sensory id doubles as recall identity selector, rw or dv"*
- `generateBFSs()`: `executeBFS(stm, stm->Current_Input >> 1, ...)`
- both recall functions build `(payload << 1 | rw_bit) << 1 | 1`

The `(x << n) & 0x0` idiom appeared ten times across four functions. Masking
with zero discards the shifted payload, so every input word the model could
construct was zero, from any source.

Also in this commit: missing `break`s in `read_sensory`'s switch (sensor 0's
reading was always overwritten by sensor 1's); an uninitialised return for an
unknown sensor; `ret_val |=| 0x1` (not an operator); `temp_input >> 1;` with
its result discarded, so all `in_sz` bits decoded bit 0; two
`while (...) ix;` loops that never terminate; a `kb_rw_path` / `kb_dv_path`
mix-up in the disincentives branch; and passing `recall_rwdv` to the two
recall calls that were short an argument.

## 0002 — integration knobs

`in_0()` / `in_1()` are documented placeholders but could not be replaced
without editing the file, and `main()` could not be replaced at all. Three
macros, each defaulting to the shipped behaviour:

| macro | effect |
|---|---|
| `RZNAI_AGI_MAX_CYCLES` | replaces the hard-coded `2000000000` in `terminate_program()` |
| `RZNAI_AGI_EXTERNAL_SENSORS` | omits the `in_0` / `in_1` simulation stubs |
| `RZNAI_AGI_NO_MAIN` | omits `main()` |

An unconfigured build is unchanged. `../harness/` uses all three.

## 0003 — runtime crashes, and what is inferred here

Two unambiguous problems:

- `instantiate()` never allocated `Input_Queue`. `new AGI_Sys()`
  value-initialises it to null, and `cycle()`'s first statement writes through
  it. Now allocated and zeroed.
- `perform_iann()` indexed `input_weights`, `input_targets` and `input_b` — all
  of which have `in_sz * In_Q_ct` (112) rows — by a hidden-unit number running
  to `hidden_sz` (224); and indexed `stm->hidden[...]`, which has `hidden_ct`
  (16) entries, by that same number.

**Inferred, and needing your review:** the corrected loops accumulate into
`weight_sums` across all sources and threshold once at the end. That is what
the variable is named for, but it is not what the original nesting did — the
original reset `weight_sums[j]` to zero inside the accumulation loop. Firings
are now assigned rather than only ever set true, so a unit that does not fire
this cycle stops firing instead of latching on permanently. Both are judgement
calls about what the network is meant to compute.

Also fixed here, unambiguously: the return value ignored `output_b` entirely
and unconditionally set every bit, so `perform_iann()` always returned
`(1 << out_sz) - 1` regardless of what the network computed; the output stage
summed weights over a loop unrelated to the firing unit; and four arrays leaked
on every cycle.

## 0004 — vector growth

```c
newv[i] = *v[i];      /* parses as *(v[i]) */
```

`v` is `__int32**`, so `v[i]` steps past the single pointer it addresses and
the result is dereferenced. Only `i == 0` is accidentally correct. Crashes the
moment a rewards or disincentives vector outgrows its initial 16 slots.

---

## Not fixed — flagged for you

- **`get_rw()` / `get_dv()` return `cycle % 32767` and `cycle % 65537` as
  `bool`.** That is true for every cycle *except* multiples of the modulus,
  which looks inverted: a 50 000-cycle run accumulated 681 rewards and 681
  disincentives. Both are marked `// simulation:`, so the schedule is yours to
  define.
- **`kbsz` is never incremented.** It reads 0 after 50 000 cycles of
  `create_dict_entry()` calls. Entries may well be in the buckets; the counter
  just is not maintained.
- **`handle_output()`'s switch has no `break`s**, the same fallthrough pattern
  fixed in `read_sensory`. Left alone because every case calls a stub, so the
  intended behaviour is not inferable from the code.
- **`simp_vector_append` is called with the wrong counters** in the
  disincentives branch:

  ```c
  line 785:  simp_vector_append(&(stm->rewards),  &(stm->rwtop), &(stm->rwcap), input);
  line 857:  simp_vector_append(&(stm->rewards),  &(stm->dvtop), &(stm->dvcap), input);
  ```

  The second should target `stm->dsnctvs`. As written, disincentives are never
  recorded, and two independent counter pairs write into the same array — each
  overwriting the other's entries. Found while tracing the constant-output
  behaviour; not fixed, because whether the fix is the array or the counters
  depends on what you intend the two vectors to be.

- **The model's output does not vary with its input.** Traced to
  `instantiate()` seeding every weight and target from the fan-out index alone,
  never from the source unit, which makes the network unable to distinguish
  *which* input bit was active. Measured and confirmed — see
  `../harness/README.md`. This is the finding that matters most for feeding it
  real vision.
