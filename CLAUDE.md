# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Zodiac serializes an entire AngelScript engine state — bytecode, modules, type info, script objects, globals, and suspended contexts — to a flat file and restores it. It is a single static library (`src/*.cpp` + `include/zodiac.h`) that the parent Kreatures engine normally compiles in-tree; this repo also builds and gtest-es it **standalone**.

Everything is gated on `HAVE_ZODIAC`. If that define is missing, every header/TU compiles to nothing and tests pass vacuously — `tests/test_canary.cpp` `#error`s to catch this.

## Build & test

The build/binary dir **must live on an exec-capable ext4 path** (e.g. `~/Developer/Build/Zodiac-Debug` or `/tmp`). The repo lives on `/mnt/Passport`, a **no-exec FUSE ntfs mount** — a test binary built under the source tree cannot be executed and the failure is silent. Never `cmake -B` into the source tree.

```bash
cmake -B ~/Developer/Build/Zodiac-Debug -S .
cmake --build ~/Developer/Build/Zodiac-Debug
ctest --test-dir ~/Developer/Build/Zodiac-Debug --output-on-failure
```

Run a single test (gtest filter on the discovered binary):
```bash
~/Developer/Build/Zodiac-Debug/tests/zodiac_tests --gtest_filter='ObjectGraph.*'
# or via ctest by test name:
ctest --test-dir ~/Developer/Build/Zodiac-Debug -R MalformedInput
```

- The `zodiac_tests` target builds with **`-fsanitize=address,undefined`** in the default config. "Green" means green *under sanitizers* — this is how the memory-safety work is verified. Pass `-DZODIAC_NO_SANITIZERS=ON` to disable.
- The `angelscript` and `as_addons` libs are deliberately **not** instrumented (the AS bytecode VM trips UBSan on type-punning/unaligned access). ASan still works cross-module against them.
- CMake options: `ZODIAC_BUILD_TESTS` (default ON), `ZODIAC_BUILD_EXAMPLE` (default OFF; `example/` is intentionally unwired), `ANGELSCRIPT_SDK` (cache path).

### AngelScript SDK dependency (patched — not vanilla)

`ANGELSCRIPT_SDK` defaults to `/mnt/Passport/Libraries/svn/angelscript-code/sdk`. This must be the **patched** SDK, not a stock download: `zodiac_addon.hpp` references patched `CContextMgr` internals (`m_threads`, `m_getTimeFunc`) that only exist in the patched `contextmgr` add-on, so the patched SDK is required to *compile* the standalone tests. This is a **compile-time-only** concern for the multi-context serialization test — the library itself does not use contextmgr at runtime. No prebuilt AS lib exists; CMake compiles the SDK sources into the `angelscript` static target.

The patched SDK's `scriptarray` is engine-coupled and cannot compile standalone, so the **stock 2.38.0 scriptarray** is vendored under `third_party/as_stock/` and shadowed onto the include path ahead of the SDK. The engine's patch (documented in the SDK's `CHANGES_FROM_STOCK.md`) is a **POD value-type optimization**: arrays of POD value types store elements *inline* instead of as pointers, so `At(i)`/`GetBuffer()` walk a contiguous raw buffer (stock stores value-type elements as pointers). **This layout difference does not affect Zodiac's round-trip:** `zodiac_addon.hpp` serializes arrays element-by-element through `array->At(i)`, never grabbing the raw buffer wholesale, so `At()` abstracts the inline-vs-pointer difference and stock and patched produce identical output. It would only bite a code path that read the array buffer directly — which Zodiac does not.

## Architecture

Public API is `include/zodiac.h` (namespace `Zodiac`): interfaces `zIZodiac`, `zIZodiacReader`, `zIZodiacWriter`, `zIFileDescriptor`, the `Code` error enum, and `zCreateZodiac(engine)`. Implementations live in `src/` (`z_` prefix, `zC` classes):

- **`zCZodiac`** (`z_zodiac.*`) — the facade. Owns the engine pointer, the type registry, callbacks, and the save/load entry points. Registered types map across **three distinct type-id spaces** (AS `typeId` / z-typeId / `GetTypeId<T>()` per-`T` counter) — these seams are intentional; fix bugs at them, don't collapse them.
- **`zCZodiacWriter`** (`z_zodiacwriter.*`) — walks the engine, unifies objects by address into a node graph, emits tables.
- **`zCZodiacReader`** (`z_zodiacreader.*`) — the load path and the trust boundary. `Verify()` validates every file-supplied offset/length/index through one overflow-safe `InFile()` range checker before any table pointer is dereferenced. This is a security boundary: malformed/corrupt input must throw a `Zodiac::Code`, never crash or mutate the engine.
- **`z_zodiacstate.h`** — the on-disk record structs (`zCHeader`, `zCEntry`, `zCTypeInfo`, `zCProperty`, `zCModule`, `zCFunction`, `zCGlobalInfo`, `zCTemplate`). This is the wire format.
- **`z_zodiaccontext.*`** — suspended-context save/restore, adapted to trunk AS's context API (2-arg `PushFunction` + `GetAddressOfVar`, replacing the owner's never-upstreamed `SetVarContents`).
- **I/O:** `zIFileDescriptor` is the stream abstraction. `zCFile` (`z_cfile.*`) wraps a `FILE*` and exposes `GetFileDescriptor()` (→ `fileno`) so the reader's `zCMemoryMap` (`z_memorymap.*`) can `mmap(PROT_READ)` a real file, falling back to malloc-slurp when there's no fd (fd `< 0`, e.g. the in-memory test double). `zCMemoryMap` is **misnamed** but the name stays (renaming is churn). The `asIBinaryStream`/streaming-vs-random-access mismatch is known and left as-is.

## Conventions & scope discipline

- **Tests never live in `src/`.** No `#ifdef TEST` blocks. All test code is in `tests/test_*.cpp`; shared fixtures in `tests/support/` (`TestEngine`, `zCMemoryFile`, malformed-buffer builders). `zodiac_addon.hpp` defines non-inline free functions, so it is included in **exactly one** TU (`tests/support/addons.cpp`) — an ODR hazard; use the thin wrappers elsewhere.
- Type-ids are **registration-order dependent** (`GetTypeId<T>()` uses process-local statics + a global counter). Keep each test self-contained by registering types within its fixture; never assert on absolute z-typeId values across tests.
- The current effort (`docs/specs/2026-07-01-zodiac-hardening-and-tests.md`) is **fix-in-place hardening, not a rewrite.** Explicit non-goals: don't collapse the three type-id spaces, don't rewrite the I/O model, don't fix the fn-ptr `reinterpret_cast` type-erasure in `zodiac.h`, don't rename `zCMemoryMap`, don't build hot-reload. Read that spec before making structural changes.
- Do not modify `projects/qt/*` (a 2021 relic).
