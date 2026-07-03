# Zodiac I/O Error-Checking & Error-Propagation — Spec

**Date:** 2026-07-02
**Status:** ready for implementation (TDD)
**Repo:** `/mnt/Passport/Libraries/Zodiac`
**Follows:** `2026-07-01-zodiac-hardening-and-tests.md` (trust boundary / malformed-input work). This spec extends the same "just works (tm)" hardening to the **output side and the syscall layer**, which the earlier pass did not cover.

## Purpose

The 2026-07-01 work hardened the *read/parse* trust boundary against malformed **input**. It did **not** audit whether Zodiac checks the return values of the syscalls it makes, nor whether errors propagate correctly back to the caller. An audit on 2026-07-02 found that:

- **The entire write path silently discards `fwrite`'s return value.** A disk-full (`ENOSPC`) or I/O error mid-save produces a **truncated, corrupt image** and `SaveToFile` still returns `zE_Success`. This is the highest-value gap: the failure is invisible until load time (or never).
- Buffered I/O is never flushed before success is declared, so even a *checked* `fwrite` isn't enough — the real error surfaces at `fflush`/`fclose`, which are also unchecked (and `fclose` runs in a destructor that can't report).
- Several syscalls (`fseek`, `ftell`, `malloc`, `realloc`) are called without checking their result; a non-seekable stream or OOM yields garbage offsets or a null-deref instead of a clean error.
- The read path checks short reads **inconsistently** (some call sites throw, adjacent ones don't).
- The facade's exception boundary is **partial**: it catches only `Zodiac::Exception`/`Zodiac::Code`, so a stray `std::exception` (`std::logic_error` from `SortTypeList`, `std::bad_alloc`, …) escapes the C-style, Code-returning API.
- `LoadFromFile`'s partial-restore path **rethrows a bare `Code`**, which also escapes the API.

## Design decision (locked)

**Keep the internal-exception model; enforce checks centrally, not per-call-site.** Zodiac already uses exceptions internally as an unwind mechanism, caught-and-converted to a `Zodiac::Code` at the one public boundary (`SaveToFile`/`LoadFromFile`). We do **not** convert the ~40 write call sites to check-and-return; that would be an I/O-model rewrite (an explicit non-goal). Instead:

1. **The single I/O sink (`zCFile::Write`) throws on any short/failed write.** Every existing unchecked caller is then covered for free, and no future caller can forget.
2. **The facade catch is made total** (`catch(std::exception&)` + `catch(...)`), so nothing crosses the boundary except a returned `Code`.

This is fix-in-place, consistent with the existing architecture and the standing non-goals ("no rewrite of the I/O model"; "fix the bugs at the seams, not the seams").

## Non-goals

- **No I/O-model rewrite.** The `asIBinaryStream` streaming-vs-random-access mismatch stays. We add checks and one flush call to the *existing* sink; we do not restructure the descriptor interface.
- **No new streaming/async/partial-write retry logic.** A short write is a hard error, full stop — not something to retry inside Zodiac.
- **No widening of the `int` return width of `Read`/`Write`.** These return `int` while `size` is `uint`, so a single >2 GB transfer is unrepresentable. Out of scope; noted at the end.
- **Do not rename `zCMemoryMap`** or otherwise touch things the prior spec froze.

---

## Owner decisions needed (defaults chosen so TDD is not blocked)

Two new `Code` values and one reclassification. **Error codes are part of the public API: append only, never reorder or renumber** (`GetErrorString`/callers key off the integer). The defaults below are what the tests in this spec assume; the owner may override the *names/values* without changing the *behavior* being tested.

| # | Decision | Default (assumed by tests) | Alternative |
|---|----------|----------------------------|-------------|
| D1 | Code for write/flush/seek/OS failure | **`zE_IOError = -24`**, bump `zE_Total = 25` | reuse `zE_EndOfFile` (-20) |
| D2 | Code for "app type has no Zodiac glue" (SortTypeList) | reuse **`zE_ObjectUnserializable` (-7)** | new `zE_MissingTypeRegistration` |
| D3 | Partial-restore-failed (engine left mutated) | **`zE_EngineCorrupted = -25`**, `zE_Total = 26`, returned (not thrown) | keep the current bare-`throw` escalation, but **documented** as the one intentional throw |

If D1/D3 add codes: append at the end of the enum in `include/zodiac.h`, add matching arms to `Exception::ToString`, and bump `zE_Total`. Nothing else may shift.

---

## Defects & required behavior

Each item: **Symptom → Current → Required → Fix site → Test.** Grouped by subsystem. Severity: **P0** silent data corruption, **P1** crash/undefined on error path, **P2** correctness/consistency.

### Group W — Write path never checks its result (P0)

**W1 — `fwrite` short count discarded at the sink.**
- *Current:* `zCFile::Write` (`src/z_cfile.cpp:65-78`) returns `fwrite(...)`'s byte count; **every** caller ignores it (`src/z_zodiaccontext.cpp:195,196,211,221,235,252,258,287,298`; all `Write` sites in `include/zodiac_addon.hpp`; `zCZodiacWriter::WriteHeader` `src/z_zodiacwriter.cpp:732`).
- *Required:* a write that transfers fewer bytes than requested must abort the save with a `Code` (**D1**), never return `zE_Success` over a partial image.
- *Fix:* in `zCFile::Write`, after `int r = fwrite(...)`, `if((uint)r != size) throw Exception(zE_IOError);`. Also treat `ferror(file)` as failure. (The internal-exception model carries it to the `SaveToFile` boundary.) Do the same in the test double — see T0.
- *Test:* **RED** `IOFault.ShortWriteAbortsSaveWithCode` — save through a sink that accepts the first N bytes then returns short; pre-fix `SaveToFile` returns `zE_Success` (assert the RED), post-fix returns `zE_IOError` and the produced buffer is *not* treated as valid.

**W2 — sub-file-overrun branch silently truncates the write.**
- *Current:* `zCFile::Write:69-73` — when the write would cross the current sub-file `end`, it sets `errno = EIO` and **clamps `size` down**, then writes less and returns the short count (which W1's callers also ignore). This corrupts the image *even on a healthy disk* if offsets are ever wrong.
- *Required:* crossing a sub-file boundary on write is a logic error → throw `zE_BadSubFileAddress` (existing), do not clamp-and-continue.
- *Fix:* replace the clamp in `Write` with `throw Exception(zE_BadSubFileAddress);`. (Leave the symmetric `Read:54-58` clamp for now — see R2 — but the two should be considered together.)
- *Test:* covered indirectly by W1's harness if a boundary is crossed; a direct unit test is optional.

### Group F — Buffered output is never flushed before success (P0)

**F1 — `Finish()` is a no-op; no flush before `SaveToFile` returns.**
- *Current:* `zCZodiacWriter::Finish()` is `{}` (`src/z_zodiacwriter.h:37`). `fwrite` only fills libc's buffer; a delayed `ENOSPC` surfaces at `fflush`/`fclose`, both unchecked. So even with W1 fixed, `SaveToFile` can return success while the tail of the image is still buffered and will fail to flush.
- *Required:* before `SaveToFile` reports success, all data must be flushed to the OS and the flush must be checked.
- *Fix:* give the descriptor a `virtual bool Flush()` (or reuse an existing method) that calls `fflush(file)` and returns `ferror`/`fflush` status; `zCFile::Flush` throws `zE_IOError` on failure. Call it from `Finish()` (or from `SaveToFile` after `writer.Finish()`), inside the try so it maps to a `Code`. The in-memory double's `Flush()` is a no-op returning success.
- *Test:* **RED** `IOFault.FlushFailureAbortsSaveWithCode` — sink buffers writes and fails at flush; pre-fix success, post-fix `zE_IOError`.

**F2 — `fclose` error swallowed in the destructor.**
- *Current:* `~zCFile` (`src/z_cfile.cpp:41-47`) calls `fclose(file)` when it owns the file, ignoring the return; a destructor cannot report it and it runs *after* `SaveToFile` has already returned success.
- *Required:* for an owned FILE*, the close/flush error must be observable *before* `SaveToFile` returns. With F1 flushing inside the try, the destructor `fclose` becomes best-effort; that is acceptable **only if** F1 guarantees the data reached the OS first. Document this in a comment at the `fclose`.
- *Fix:* rely on F1's flush; add a comment at `~zCFile` explaining the close is best-effort because `Finish()` already flushed-and-checked. (No separate test; F1 covers the observable behavior.)

### Group R — Read path checks short reads inconsistently (P1)

**R1 — some context reads unchecked.**
- *Current:* `src/z_zodiaccontext.cpp` checks short reads at `:442`, `:488`, `:574` (`if(sizeof(x) != file->Read(&x)) throw zE_EndOfFile;`) but **not** at `:405`, `:406`, `:425`, `:437`, `:474`. A truncated context record there is read as garbage/zero and restore proceeds.
- *Required:* every fixed-size read that is not a legitimate EOF point must verify the byte count and throw `zE_EndOfFile` on short read.
- *Fix:* wrap the unchecked reads with the same check already used three lines down. (Mechanical; match the existing idiom exactly.)
- *Test:* **RED/GREEN** `IOFault.TruncatedContextRecordRejected` — take a valid image containing a suspended context, truncate it inside the context block, assert `LoadFromFile` returns `zE_EndOfFile` (not a crash / not success). If the pre-fix behavior is a crash, use a forked death test asserting clean exit; if it's silent success, a normal `EXPECT_NE(rc, zE_Success)`.

**R2 — `Read` sub-file-overrun clamp (`src/z_cfile.cpp:54-58`).**
- *Current:* clamps `size` to the sub-file `end` and sets `errno = EIO`, returning a short count. Unlike W2, a short read at a sub-file boundary can be a legitimate end-of-record. **Decision:** leave the clamp, but ensure callers that require an exact count use the `sizeof != Read` idiom (R1). Document that `Read` returns a possibly-short count *by design* and the caller must check.
- *Fix:* comment only; no behavior change. (Prevents a future "make Read throw" over-correction that would break intended partial reads.)

### Group S — Unchecked seek/tell syscalls (P1)

**S1 — `fseek`/`ftell` results ignored; non-seekable stream undetected.**
- *Current:* `zCFile::seek` (`:80-102`) calls `fseek` unchecked; `tell` (`:104-107`) returns `ftell(file) - begin` with `ftell`'s `-1L` error folded into unsigned arithmetic; `Read`/`Write` compute `size + ftell(file)` (`:52`, `:67`) with the same unchecked `ftell`. On a pipe/FIFO/socket (non-seekable) every `fseek`/`ftell` fails and Zodiac writes an image full of garbage offsets while reporting success. The format is inherently random-access.
- *Required:* a seek/tell failure is a hard I/O error (**D1**); a non-seekable descriptor must be rejected rather than producing a garbage image.
- *Fix:* check `fseek`'s return (`!= 0` → throw `zE_IOError`) and `ftell`'s (`== -1L` → throw `zE_IOError`). Optionally probe seekability once in the `zCFile` ctor (an `fseek(file, 0, SEEK_CUR)` / `ftell` check) and throw early. Keep changes inside `zCFile`.
- *Test:* **GREEN** `IOFault.NonSeekableDescriptorRejected` — construct a `zCFile` over a pipe (`popen`/`pipe(2)` fd wrapped in `fdopen`) and assert save fails with a `Code`, no crash. (If wiring a real pipe is fiddly under the harness, a double whose `seek`/`tell` report failure is an acceptable substitute.)

### Group M — Unchecked allocations (P1)

**M1 — `malloc`/`realloc` in `zCFile` unchecked.**
- *Current:* ctor `malloc(sizeof(StackFrame)*stackSize)` (`:34`) and `PushSubFile` `realloc(...)` (`:135`, `:152`) never checked; the `realloc` also **leaks** the old block if it returns null, then the next line dereferences null.
- *Required:* an allocation failure must throw a `Code` (`zE_BufferOverrun` is the existing OOM-ish code, or `zE_IOError`), not null-deref; `realloc` must not leak on failure.
- *Fix:* check each result; for `realloc`, assign to a temp and only overwrite `stack` on success, else `throw`. Prefer `zE_BufferOverrun` to match `zCMemoryMap`'s existing OOM handling (`src/z_memorymap.cpp:48-49`).
- *Test:* low priority (hard to fault-inject `malloc` portably). Cover by code review; a test is optional and may be skipped with a `log`/comment noting the gap.

### Group P — Error propagation across the facade boundary (P0/P1)

**P1 — partial catch lets `std::exception` escape the C-style API.**
- *Current:* `SaveToFile` (`src/z_zodiac.cpp:71-80`) and `LoadFromFile` (`:117-136`, `:153-164`) catch only `Exception&` and `Code&`. `SortTypeList` throws `std::logic_error` (`:315-321`); `std::bad_alloc`, `std::out_of_range`, etc. can also arise. All escape unhandled out of a function that is supposed to *return* a `Code`.
- *Required:* the boundary is total — nothing propagates out except a returned `Code`.
- *Fix:* after the existing two arms, add:
  ```cpp
  catch(std::exception & e) { error_string = e.what();            error_code = zE_IOError; }
  catch(...)                { error_string = "unknown error";     error_code = zE_IOError; }
  ```
  in **all four** try-blocks (`SaveToFile` ×1, `LoadFromFile` ×2 — plus the `changedEngineState` handling of P3). Use `zE_IOError` (D1) or a dedicated internal code; the point is *a* code, not a throw.
- *Test:* the existing **RED** `UnregisteredType.SaveWithUngluedNonPodTypeReturnsCodeNotThrow` (`tests/test_defect_unregistered_type.cpp`) flips GREEN. Add **GREEN** `Facade.BoundaryIsTotalOnStdException` if a second trigger is cheap.

**P2 — `SortTypeList` throws a raw `std::logic_error` (reclassify).**
- *Current:* `src/z_zodiac.cpp:315-321` throws `std::logic_error("Missing entry for loading/saving …")` for a non-POD app type with no Zodiac glue — a *programmer* error (missing registration), not malformed data.
- *Required:* report it as a `Zodiac::Code` (**D2**) so callers can distinguish it; the total catch (P1) is only a backstop.
- *Fix:* replace the `throw std::logic_error(...)` with `throw Exception(zE_ObjectUnserializable, <name>)` (or the D2 alternative). Keep the descriptive text in `Exception::text` so `GetErrorString` still names the offending type.
- *Test:* same test as P1; additionally assert the returned code equals the D2 code (not just "non-success"), if the owner locks D2.

**P3 — partial-restore rethrow escapes `LoadFromFile`.**
- *Current:* `src/z_zodiac.cpp:122-133` — when `changedEngineState` is true and restore fails, it does `throw e.code;` / `throw c;`, sending a bare `Code` **out of** `LoadFromFile`, past the "returns a Code" contract. The engine is left partially restored (unusable).
- *Required (owner, D3):* decide whether "engine now corrupt" is (a) a returned `zE_EngineCorrupted` (recommended — uniform API; caller checks one channel) or (b) the one *documented* intentional throw. Default: (a).
- *Fix (default a):* set `error_code = zE_EngineCorrupted;` and `return`, do **not** rethrow; document at the return that the engine must be discarded. Under (b): keep the throw but add a doc comment and a `catch(...)`-free carve-out, and a test that expects the throw.
- *Test:* **GREEN** `Facade.PartialRestoreFailureReportsEngineCorrupted` — force a mid-restore failure (e.g. corrupt a table that is only touched after `LoadByteCode`/`ProcessModules` have already mutated the engine) and assert the chosen contract (returned code under (a); `EXPECT_THROW` under (b)).

---

## Test harness additions (for the TDD agent)

**T0 — Fault-injecting descriptor `zCFaultyFile` in `tests/support/`.** Subclass or wrap `zCMemoryFile` (`tests/support/memory_file.{h,cpp}`) with configurable fault modes. It must mirror the sub-file stack semantics already in `zCMemoryFile`, and — crucially — its `Write`/`Flush` must throw the **same** `zE_IOError`/`zE_BadSubFileAddress` that `zCFile` will throw once fixed, so a test written against the double matches production behavior. Suggested knobs:

- `failWriteAfterBytes(uint n)` — accept `n` bytes total, then every `Write` returns short / throws `zE_IOError`. (W1)
- `failOnFlush()` — buffer all writes, throw `zE_IOError` from `Flush()`. (F1)
- `failReadAfterBytes(uint n)` — short read past a threshold, for truncation tests. (R1)
- `failSeek()` / `reportNonSeekable()` — `seek`/`tell` signal failure. (S1)

Because `zodiac_addon.hpp` is an ODR-sensitive single-TU include, put `zCFaultyFile` in its own `.cpp`/`.h` under `tests/support/` like `zCMemoryFile`, and add it to the existing support target (CMake globs `CONFIGURE_DEPENDS`, so no CMake edit).

**Test files (one concern each, self-contained per existing convention — duplicate the `MakeZodiac`/`BuildValidImage`/`HeaderOf` helpers, don't share):**

| File | Covers | Kind |
|------|--------|------|
| `tests/test_io_write_fault.cpp` | W1, W2, F1 | RED→GREEN |
| `tests/test_io_read_fault.cpp` | R1 (truncated context / tables) | RED→GREEN |
| `tests/test_io_seek_fault.cpp` | S1 | GREEN |
| `tests/test_defect_unregistered_type.cpp` (existing) | P1, P2 | RED→GREEN |
| `tests/test_facade_propagation.cpp` | P1 backstop, P3 | GREEN |

**Build/run (unchanged from the project):** build into an exec-capable ext4 dir, never the source tree.
```bash
cmake -B ~/Developer/Build/Zodiac-Debug -S .
cmake --build ~/Developer/Build/Zodiac-Debug
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir ~/Developer/Build/Zodiac-Debug --output-on-failure
```
Everything must stay green under `-fsanitize=address,undefined` and identical under `-DZODIAC_INSTRUMENT_LIB=ON` (LibASan). The write-fault tests are **return-code** tests (normal `EXPECT_EQ(rc, zE_IOError)`), not death tests — the whole point is that the error becomes a code, not a crash. R1/P3 may be death tests only if the *pre-fix* behavior is a crash.

## Implementation order (dependencies)

1. **D1/D2/D3 codes** in `include/zodiac.h` + `Exception::ToString` (unblocks everything). Append-only.
2. **T0 `zCFaultyFile`** (unblocks W/F/R/S tests).
3. **Group P** (P1 total catch, P2 reclassify, P3 partial-restore) — smallest, highest-value; flips an already-written RED.
4. **Group W** then **Group F** (the P0 write/flush corruption — the reason this spec exists).
5. **Group R**, **Group S**, **Group M**.

## Out of scope (note, don't fix here)

- `Read`/`Write` return `int` vs `uint size` — a single >2 GB transfer is unrepresentable. Real images are far smaller; widening the ABI is an I/O-model change.
- The `asIBinaryStream` streaming-vs-random-access mismatch (frozen by the prior spec).
- A save-side re-entrancy guard: `zE_AlreadySaving = -23` exists in the enum but no `SaveToFile` guard uses it (cf. the load-side `m_loaded`/`m_inProgress`). Wiring it is cheap and adjacent, but it is a *concurrency* concern, not an I/O-error one — do it only if trivial while in `SaveToFile`, otherwise defer.
