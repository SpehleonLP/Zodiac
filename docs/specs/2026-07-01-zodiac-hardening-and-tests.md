# Zodiac Hardening & Test Harness — Spec

**Date:** 2026-07-01
**Status:** ready for implementation
**Repo:** `/mnt/Passport/Libraries/Zodiac` (standalone git repo; the engine compiles its `.cpp` in-tree, but this work builds & tests it **standalone**)

## Purpose

Zodiac is a serialization library for AngelScript (snapshots a whole script engine — bytecode, modules, type info, script objects, globals, suspended contexts — to a flat file, and restores it). A 2026-07-01 code review found the load path to be memory-unsafe against malformed/corrupt files, plus a cluster of correctness bugs that fire on *valid* input, and there is no automated test coverage (only `example/example.cpp`'s `main()`).

This spec fixes **all reviewed issues in place** and stands up a **standalone CMake gtest harness**. It deliberately does **not** re-architect the library.

## Decisions (locked by owner)

1. **Fix in place — no rewrite.** The name-keyed unification core and the callback hooks are sound and stay. Agents are weak at open-ended architecture, so this spec is itemized and closed-ended: implementers fix exactly the listed defects and do not restructure interfaces, the type-id system, or the I/O model.
2. **Standalone CMake build with a gtest target**, *in addition to* `example/` (untouched). The library has not been built standalone since 2021; do not depend on the full engine build to test it.
3. **Tests live in a dedicated `tests/` target, never co-located in the source files under test.** No `#ifdef TEST` blocks in `src/`.

## Non-goals (explicitly out of scope — do NOT do these)

- No rewrite of the I/O model (the `asIBinaryStream`/streaming vs. random-access-buffer mismatch is known; leave it). **Carve-out:** Part B0 swaps the *reader's backing* from malloc-slurp to read-only mmap behind the existing `GetAddress()/GetLength()` interface + one new fd accessor. This is a contained backing swap, not a rewrite of the streaming abstraction — the interface and all callers stay put.
- No collapsing of the three type-id spaces (AS typeId / z-typeId / `GetTypeId<T>()` counter). Fix the *bugs* at those seams, not the seams.
- No fix to the function-pointer `reinterpret_cast` type-erasure (`zodiac.h:146,152`; the `(zLOAD_FUNC_t)load_func` casts). Calling through an incompatible function-pointer type is technically UB but works on-target; changing the registration ABI is architecture. **Leave it, note it.**
- No hot-reload / per-module save-restore feature. That is a later arc; this spec only *unblocks* it (trust boundary + tests). Where a fix has a hot-reload implication it is noted, but the feature is not built here.
- Do not rename `zCMemoryMap` (it is a malloc-buffer, not an mmap — misnamed, but a rename is churn). Just delete its dead commented-out mmap body.

## Environment constraints (MUST honor — build will silently fail otherwise)

- **`/mnt/Passport` is a no-exec FUSE ntfs mount.** A test binary built into a directory under `/mnt/Passport` **cannot be executed**. The CMake **binary/build dir MUST live on an exec-capable ext4 path** — e.g. `~/Developer/Build/Zodiac-Debug/` (sibling of the engine's `Engine-Debug`) or `/tmp`. Never `cmake -B` into the source tree.
- **AngelScript SDK (patched):** `/mnt/Passport/Libraries/svn/angelscript-code/sdk/`
  - headers: `sdk/angelscript/include/` (provides `angelscript.h`)
  - library sources: `sdk/angelscript/source/*.cpp` — **no prebuilt lib exists**, so the CMake must compile these into a static lib target (or `add_library`).
  - add-ons: `sdk/add_on/` — Zodiac includes them as `add_on/<name>/<name>.h`, so **the include root for add-ons is the `sdk/` dir itself**. Use *this* tree, not a vanilla SDK: it is patched (see `CHANGES_FROM_STOCK.md`), and `zodiac_addon.hpp` depends on `CContextMgr` internals (`m_threads`, `m_getTimeFunc`) that only exist in the patched `contextmgr`.
  - add-on `.cpp` needed by the tests (compile into the AS support lib): `scriptstdstring`, `scriptarray`, `scriptdictionary`, `scriptany`, `scriptgrid`, `scripthandle`, `scriptfile`, `scriptmath`, `contextmgr`, `datetime`, `weakref`.
  - **Risk:** the engine substitutes a custom array factory (`Spehleon/lib/AngelScript/CreateCScriptArray.cpp`). The standalone harness should use the **stock** `scriptarray` add-on; if the `CScriptArray` round-trip test behaves oddly, this substitution is the first suspect — document, don't chase into the engine.
- **`HAVE_ZODIAC` must be defined** for the whole target (every Zodiac header/TU is gated on it). Missing define ⇒ everything compiles to nothing and tests pass vacuously. Add a compile-time canary (below).
- **GoogleTest:** system `-lgtest` is present (the engine links it). Prefer `find_package(GTest)`; fall back to `FetchContent` only if not found.
- **Sanitizers:** the test target MUST build with `-fsanitize=address,undefined -fno-omit-frame-pointer` in the default (Debug) test config. The whole point of the memory-safety work is verified by ASan/UBSan catching the malformed-input regression tests. A "green" run means green *under sanitizers*.

---

## Part A — Build infrastructure

Create at the Zodiac repo root:

- **`CMakeLists.txt`** — top level. Options: `ZODIAC_BUILD_TESTS` (default ON), `ZODIAC_BUILD_EXAMPLE` (default OFF). Locates the AngelScript SDK via a cache var `ANGELSCRIPT_SDK` defaulting to `/mnt/Passport/Libraries/svn/angelscript-code/sdk`.
  - Target `angelscript` (static): compiles `${ANGELSCRIPT_SDK}/angelscript/source/*.cpp`; PUBLIC include `${ANGELSCRIPT_SDK}/angelscript/include`.
  - Target `as_addons` (static): compiles the add-on `.cpp` listed above; PUBLIC include `${ANGELSCRIPT_SDK}` (so `add_on/...` resolves); links `angelscript`.
  - Target `zodiac` (static): compiles `src/*.cpp`; PUBLIC include `include/` and `src/`; PUBLIC `compile_definitions(HAVE_ZODIAC=1)`; links `angelscript` + `as_addons`.
  - `enable_testing()`; `add_subdirectory(tests)` when `ZODIAC_BUILD_TESTS`.
- **`tests/CMakeLists.txt`** — target `zodiac_tests` (executable): compiles `tests/*.cpp`; links `zodiac`, `GTest::gtest`, `GTest::gtest_main`; sets `-fsanitize=address,undefined` (compile + link) unless `ZODIAC_NO_SANITIZERS`. Register with `gtest_discover_tests` (or `add_test`).
- **`tests/support/`** — shared fixtures (see Part E): `TestEngine` (creates an `asIScriptEngine`, registers add-ons, tears down), `zCMemoryFile`, malformed-buffer builders.

Do not modify `projects/qt/*` (2021 relic; leave as-is). Do not add Zodiac tests to the engine's `Engine.pro`.

**Canary (first test file, `tests/test_canary.cpp`):**
```cpp
#ifndef HAVE_ZODIAC
#  error "HAVE_ZODIAC not defined — Zodiac compiles to nothing; the whole suite would pass vacuously."
#endif
```
plus a trivial `TEST(Canary, Builds){ SUCCEED(); }`.

**Acceptance for Part A:** from an ext4 build dir, `cmake -B ~/Developer/Build/Zodiac-Debug -S . && cmake --build ~/Developer/Build/Zodiac-Debug && ctest --test-dir ~/Developer/Build/Zodiac-Debug` builds and runs the canary green under ASan/UBSan.

---

## Part B0 — Reader backing: read-only mmap with malloc fallback

The reader's `zCMemoryMap` (`z_memorymap.h`/`.cpp`) is misnamed: despite the name it does **not** mmap — it `malloc`s a buffer the size of the whole file and copies the entire file into it (`z_memorymap.cpp:27-35`). The real mmap implementation is commented out directly above (`:11-25`); it was abandoned not because mmap is hard but because it calls `descriptor->GetFileDescriptor()` — **a method that does not exist on `zIFileDescriptor`.** You cannot `mmap` without a real fd, and the stream abstraction never exposed one (`zCFile` holds a private `FILE*`, `z_cfile.h:46`). The stale rationale comment (`/* Random reads worse than sequential so just copy it */`) is also backwards for a **demand-paged** read-only map of a file that is random-accessed all over (trailer header + offset chasing): the malloc path copies the whole file up front regardless of what is touched; mmap faults in only the pages actually read. This matters for the project's tight-memory budget — the malloc path doubles peak footprint (file + copy) and pays the full copy even for a partial/aborted load.

**Note:** neither side needs a *growing* buffer. The read size is known up front (`seek END; tell`), and the writer already grows naturally through `FILE*`/`fwrite` (`zCFile::Write`). A self-growing writable mmap (ftruncate + mremap; SIGBUS past the mapped end) is genuinely fiddly and is **not** needed here — do not build one.

**Changes:**

1. **Add an fd accessor to the interface.** In `zIFileDescriptor` (`include/zodiac.h`): `virtual int GetFileDescriptor() const { return -1; }` (default = "no fd / not a real file"). `zCFile` overrides it to return `fileno(file)`. Descriptors that aren't real files (the in-memory `zCMemoryFile` test double) keep the `-1` default.
2. **`zCMemoryMap` prefers mmap, falls back to malloc.** If `GetFileDescriptor() >= 0`, `mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0)` the **whole underlying file** and record the mapping for `munmap` in the dtor. Else (fd `< 0`, or `mmap` fails), keep the current `malloc` + `Read` slurp. Track which path was taken so the dtor does the right teardown (`munmap` vs `free`).
3. **Sub-file offset handling.** mmap offsets must be page-aligned, so **map the whole file from offset 0** and expose the base via `GetAddress()`; the reader already indexes everything relative to the buffer base (and the header trailer is found via `GetLength()`). Do not attempt to map a sub-range at an arbitrary `SubFileOffset()` — map the container, index within it.
4. **Keep the public shape identical.** `GetAddress()` / `GetLength()` are unchanged, so `Verify()` (Part B) and every table pointer in the reader are untouched by this swap. Delete the commented-out dead mmap block once the real one lands (supersedes the Part F cleanup for this file).

**Hot-reload fallback (design note, deferred — do NOT build here):** the temp-file-backed blob is the intended home for the hot-reload "last-good snapshot." Write the save to a temp file (grows via `fwrite`, zero heap cost), then restore by mmap-ing it read-only through the same `zCMemoryMap` path — demand-paged, near-zero resident RAM. This composes cleanly with the accessor above (a `tmpfile()`-backed `zCFile` already yields a valid `fileno`). Recorded here so the reader backing is built with that use in mind; the feature itself belongs to the later hot-reload arc.

**Acceptance:** the object-graph round-trip (Part E test 8) passes with the reader backed by a real file (mmap path) **and** by `zCMemoryFile` (malloc-fallback path); ASan/UBSan clean on both; dtor teardown correct for each path (no leak, no bad `munmap`).

## Part B — The trust-boundary rebuild (`Verify()`)

This is the central fix; the critical/high memory bugs are all symptoms of `Verify()` (`z_zodiacreader.cpp:63`) being an incomplete trust boundary. Rebuild it around **one overflow-safe range checker**, then validate **every** file-supplied offset, length, and index-typed field through it. Do not scatter ad-hoc checks.

Add a private helper (overflow-safe, 64-bit math, no pointer arithmetic in the predicate):
```cpp
// true iff [offset, offset+count*elemSize) lies fully within [0, fileLen), with no overflow.
static bool InFile(uint64_t offset, uint64_t count, uint64_t elemSize, uint64_t fileLen);
```
`Verify()` (and the ctor) must, in order:

1. **Before** computing `m_header`: require `m_mmap.GetLength() >= sizeof(zCHeader)` (fixes finding #1, `reader.cpp:25`). Throw `zE_BadFileType`.
2. Check the magic (existing) and the header self-consistency fields (`pointerSize`, `boolSize`, endianness) — reject mismatches with a clear code.
3. For **each table** (strings, string addresses, entries, modules, typeInfo, properties, globals, functions, templates, savedObjects, saveData, bytecode): validate its `Offset`/`Length` pair with `InFile(off, len, sizeof(elem), fileLen)` — validating the **start** offset, not just the end (fixes #7 overflow bypass and #8 unchecked starts). Replace the current `base + count > end` pointer-arithmetic checks.
4. Require the string table to be **NUL-terminated at its final byte** (fixes #6b run-off-the-end). Reject otherwise.
5. **Per-record field validation** (the currently-missing checks — findings #2, #3, #5):
   - every `zCEntry.typeId` → `< typeTableLength()` (i.e. valid `m_typeInfo` index)
   - every `zCEntry.owner` → `< addressTableLength()` (0/sentinel meaning "no owner" preserved — define and check the sentinel explicitly)
   - every `zCEntry`'s `[offset, offset+byteLength)` within the savedObject region
   - every `zCTypeInfo.propertiesBegin/Length` within the properties table
   - every `zCProperty` / built `Property.readOffset` → `readOffset + <read size> <= owning entry's byteLength` (validate where the `Property` table is built, `ProcessModules` `reader.cpp:451-469`)
   - every `name`/`nameSpace` string index → `< stringTableLength()` **using `>=` rejection** so `index == length` is rejected (fixes #6a and the `>`-vs-`>=` class, finding #9). Audit all comparisons in `Verify()` (`:86,:121,:151,:156`, etc.) and the two index guards at `:704` and `:1001`.
6. **Zero-count guards:** any loop of the form `for(i=0; i < len-1; ...)` must not run when `len==0` (fixes #4 `moduleDataLength()-1` underflow at `reader.cpp:379,420,427`). Prefer `for(i+1 < len)` or an explicit `if(len) ...`.

`Verify()` returns void and throws `Zodiac::Exception` / `Code` on any violation (as today). No partial state must be observable after a throw (the ctor already runs `Verify()` before allocating `m_loadedObjects`; keep that order).

**Every rule above gets a negative regression test** in Part E (a crafted buffer that trips exactly that check ⇒ expect a clean throw, ASan-clean, no engine mutation).

---

## Part C — Itemized memory-safety fixes

Most are subsumed by Part B; listed here with anchors so nothing is missed. All line numbers are pre-fix.

| # | Sev | File:line | Defect | Fix |
|---|-----|-----------|--------|-----|
| 1 | crit | `z_zodiacreader.cpp:25` | short-file header pointer underflow | Part B.1 |
| 2 | crit | `:725,:872` | `zCEntry.typeId` used as unvalidated index | Part B.5 |
| 3 | crit | `:754-761` | `zCEntry.owner` used as unvalidated index | Part B.5 |
| 4 | crit | `:379,:420,:427` | `moduleDataLength()-1` unsigned underflow | Part B.6 |
| 5 | crit | `:736,:887,:904` | property `readOffset` unbounded read | Part B.5 |
| 6 | high | `z_zodiacreader.h:54` | `LoadString` returns null / runs off unterminated table | Part B.4 + B.5; also make every `strcmp(LoadString(id), ...)` caller null-safe (`:531,:538,:552,:561,:582,:595`; writer `:186`) |
| 7 | high | `:72,:105,:126,:131,:136` | 32-bit `off+len` overflow bypass | Part B.3 (`InFile`) |
| 8 | high | `:78,:81,:95,:116,:145,:181,:205,:251` | section start offsets never validated | Part B.3 |
| 9 | med | `:704,:1001` (+ Verify comparisons) | `>` where `>=` needed before indexing | Part B.5 |
| 10 | med | `:377` | `bool loadedByteCode;` read uninitialized | initialize `= false` |
| 11 | med | dtor `:42-61` vs `:1016,:1049,:1054,:1062` | `m_loadedFunctions` entries AddRef'd, never Released (leak) | track each stored function/delegate's kind+refcount; Release all in `~zCZodiacReader`. Add a leak test (below) |
| 12 | med | `z_memorymap.cpp:33-34` | unchecked `malloc`, ignored short `Read` | check `malloc` != null; check `Read` returned `m_length`; throw `zE_EndOfFile`/`zE_BufferOverrun` on failure. Sanity-cap `m_length` |
| 13 | low | `:619` | `GetProperties` uses `propertiesLength` as begin offset (should be `propertiesBegin`) | fix field; latent today (callers pass `-1`) but correct it |
| 14 | low | `z_zodiacwriter.cpp:219` | POD-app-object branch writes `ref` (container) not `address` (member) | write `address`, matching sibling branches |

---

## Part D — Itemized correctness bugs (fire on valid input)

| File:line | Defect | Fix |
|-----------|--------|-----|
| `z_zodiac.h:35` | `SetPostRestoreCallback` stores into `m_postSavingCallback` | store into the post-restore member |
| `z_zodiac.cpp:191` | `GetAsTypeIdFromZTypeId` compares `c.asTypeId == zTypeId` (wrong field) | compare against the z-typeId field |
| `z_zodiac.h:84` / `z_zodiac.cpp:80` | `error_code` uninitialized; success path never sets it, yet it is returned | initialize to `zE_Success`; set explicitly on all paths |
| `z_zodiac.cpp:114-157` | `LoadFromFile` falls through after a caught error and returns `zE_Success` unconditionally | on caught error: stop, set the error code, return it; do not continue into `RestoreGlobalVariables` |
| `z_zodiacexception.h:19` | missing comma after `"None"` merges first two strings, shifts table, OOB read of last code | add the comma; verify the array has exactly `zE_Total` entries with a `static_assert` |
| `z_zodiacexception.h:50-51` | ctors disagree on sign (`Exception(Code)` negates, `Exception(string,Code)` doesn't) | make both use the same `ToString` indexing convention |
| `z_zodiacreader.cpp:310` | `DocumentGlobalVariables` derefs `GetGlobalVar()` without the null check its sibling (`:334`) has | add the null check (also the hot-reload "global missing from save" case) |
| `z_zodiac.cpp:285,326` | `SortTypeList` `needSort` never set true ⇒ sort never runs (dead) | delete the unreachable sort machinery; lookups stay linear (document). Do **not** "fix" it into running — that is behavior change out of scope |

---

## Part E — The gtest suite (`tests/`)

Shared fixtures in `tests/support/`:
- **`TestEngine`** — RAII `asIScriptEngine*` + `ZodiacRegisterAddons`; a message callback that fails the test on AS errors. Note the ODR hazard: `zodiac_addon.hpp` defines non-inline free functions, so include it in **exactly one** TU (`tests/support/addons.cpp`) and expose thin wrappers.
- **`zCMemoryFile : Zodiac::zIFileDescriptor`** — a `std::vector<char>`-backed descriptor mirroring `zCFile` (seek/tell/read/write + sub-file push/pop). ~100 lines. This is the enabler for disk-free tests.
- **`MalformedSave`** — helpers to build a minimal valid `.zdc` image in memory and then corrupt one field (truncate, set `moduleDataLength=0`, set `entry.typeId`/`owner` out of range, oversize a `readOffset`, un-terminate the string table, overflow an `off+len`). One helper per Part-B rule.

Test files (each a separate `tests/test_*.cpp`; **no test code in `src/`**):

1. `test_canary.cpp` — Part A canary.
2. `test_cfile_subfile.cpp` — `zCFile` over `tmpfile()`: write/seek/tell round-trip; nested `PushSubFile`/`PopSubFile` restores position; read past sub-file end truncates. (No AS needed.)
3. `test_exception_table.cpp` — every `Code` → expected string incl. the last enum; `static_assert` count. Reddens pre-fix on the missing comma.
4. `test_callback_wiring.cpp` — set all six callbacks; trivial save+load; assert each fires once in documented order. Reddens pre-fix on the post-restore/post-saving swap.
5. `test_type_registry.cpp` — bare engine + `zCZodiac`: register value/ref types; duplicate ⇒ `asALREADY_REGISTERED`; after a save, `GetZTypeIdFromAsTypeId`/`GetAsTypeIdFromZTypeId` invert. Reddens pre-fix on `:191`.
6. `test_saveload_empty.cpp` — one empty module round-trips; `SaveToFile` **returns** `zE_Success`; fresh `LoadFromFile` succeeds. Reddens pre-fix on uninitialized `error_code`/`loadedByteCode`.
7. `test_memoryfile_roundtrip.cpp` — re-run 2 & 6 against `zCMemoryFile`. From here, no disk.
8. `test_roundtrip_object_graph.cpp` — the example made assertable: `class A { B@ b; string s; array<int> a; }`, two handles aliasing one `B`, a null handle, a suspended context via `ContextMgr`. Save → fresh engine + recompile identical source → load → assert values, **aliasing identity preserved**, null preserved, context resumes. **Run this parameterized over both reader backings (Part B0): once with a real-file `zCFile` (mmap path) and once with `zCMemoryFile` (malloc-fallback path)** — both must pass, ASan-clean, with correct dtor teardown.
9. `test_malformed_input.cpp` — one case **per Part-B rule**: feed the corrupted buffer to `LoadFromFile`, expect a specific non-success `Code` thrown/returned, **no engine mutation**, and ASan/UBSan clean. This is the regression net that proves the trust boundary. (Pre-fix, these crash/OOB — expected to be written red then greened by Part B.)
10. `test_function_leak.cpp` — load a save containing delegates/functions; assert refcounts are balanced after the reader is destroyed (or run under ASan/LSan and assert no leak). Reddens pre-fix on finding #11.
11. `test_schema_drift.cpp` — **documents current behavior as the executable spec for the future hot-reload arc**: save `class C { int a; int b; }`; reload into (i) `{ int a; }` and (ii) `{ int a; int b; int c; }`. Assert the *current* outcomes (throw / uninitialized-`c`). The hot-reload work later flips these deliberately.

---

## Part F — Dead-code cleanup (low priority; do last, in its own commit)

Safe deletions (git has history):
- `z_memorymap.cpp:11-25` commented-out mmap body — **deleted by Part B0 when the real mmap path lands** (not here).
- `z_zodiac.cpp:133-155` `#if 0` restore-callback block.
- `z_zodiacreader.h:93,99` commented-out members; `reader.cpp:10-11` duplicate `#include <cstring>`/`<stdexcept>`.
- **Debug stubs (real, remove):** `reader.cpp:~908` `break_point` increment; `writer.cpp:~306` `if(i==3)` breakpoint block.
- `z_zodiacwriter.h:36` no-op `Finish()` (only if truly unused).
- `z_zodiacexception.cpp` is a 1-line include-only TU — leave unless the CMake needs it dropped.

Leave the `zCMemoryMap` name, the `asIBinaryStream` I/O model, the type-id spaces, and the fn-ptr casts **as-is** (non-goals).

---

## Sequencing & verification

Each phase ends **green under ASan/UBSan** from the ext4 build dir before the next begins.

- **Phase 0 — harness.** Part A + fixtures (`TestEngine`, `zCMemoryFile`) + tests 1, 2, 7's scaffold, and test 8 as the smoke round-trip. Proves the standalone build and gives a regression net *before* touching `src/`. (Tests 3–6 may go red here — that's the point; they pin the bugs.)
- **Phase 1 — correctness bugs (Part D).** Greens tests 3, 4, 5, 6. Small, low-risk, high-signal.
- **Phase 2 — trust boundary (Part B + Part C criticals/highs).** Rebuild `Verify()` + `InFile` + per-field validation; write & green `test_malformed_input.cpp` (test 9). This is the memory-safety payload. **Also land Part B0 here** (reader mmap backing + `GetFileDescriptor()` seam) — it shares the reader/`Verify` surface, and parameterizing test 8 over both backings guards it. Because B0 leaves `GetAddress()/GetLength()` unchanged, it can equally land in Phase 0 alongside `zCMemoryFile`; sequence it wherever the reader work is already open, but keep it in its own commit.
- **Phase 3 — remaining mediums/lows + leaks (Part C #10–14) + drift doc (test 11) + cleanup (Part F).**

**Definition of done:** `ctest` all green under ASan/UBSan from an ext4 build dir; `test_malformed_input` covers every Part-B rule; `example/` still builds; no test code added under `src/`; non-goals untouched.

## Risks / watch-items

- **Patched add-on tree** — the standalone harness must use the SDK at `.../svn/angelscript-code/sdk` (patched `contextmgr`), not a vanilla SDK, or `zodiac_addon.hpp` won't compile.
- **Custom array factory** — engine uses `CreateCScriptArray`; standalone uses stock `scriptarray`. If the array round-trip (test 8) misbehaves, suspect this first.
- **Suspended-context round-trip (test 8)** may expose further bugs in `z_zodiaccontext.cpp` not enumerated by the review; treat new findings as additions to Part C, and do not expand scope into hot-reload.
- **Type-id determinism** — `GetTypeId<T>()` uses per-`T` process-local statics + a global counter, so serialized type-ids are registration-order dependent. Keep each test self-contained (register within the fixture); don't assert on absolute z-typeId values across tests.
