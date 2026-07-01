# Zodiac

**Full-engine-state serialization for [AngelScript](https://www.angelcode.com/angelscript/).** *(work in progress)*

Zodiac snapshots an entire live AngelScript engine to a flat file and restores it later: compiled bytecode, modules, registered and script-declared type info, script objects (with their reference graph), global variables, and even suspended contexts. It's aimed at save-game / checkpoint use cases where you need to freeze the whole scripting VM and thaw it back to exactly where it was.

## Features

- **Whole-engine snapshot** — modules, bytecode, type info, globals, and the live script-object graph in one file.
- **Reference-graph fidelity** — objects reachable through multiple handles are unified by address, so aliasing identity and `null` handles survive a round-trip.
- **Suspended contexts** — a mid-execution context can be saved and resumed after load (bytecode must be included).
- **Custom type hooks** — register save/load callbacks for your own value and reference types, including application-registered types.
- **Extensible I/O** — read/write through a small `zIFileDescriptor` interface; back it with a real file, or your own in-memory buffer.
- **Add-on support out of the box** — `zodiac_addon.hpp` provides serializers for the standard AngelScript add-ons (`array`, `grid`, `dictionary`, `string`, ...).

## How it works

The public surface lives in [`include/zodiac.h`](include/zodiac.h) under namespace `Zodiac`. You create a `zIZodiac` around an existing `asIScriptEngine`, register serializers for any custom types, and call `SaveToFile` / `LoadFromFile` with a `zIFileDescriptor`.

On save, the writer walks the engine and unifies objects by address into a node graph, then emits a set of tables (strings, types, properties, objects, functions, bytecode, ...). On load, the reader validates every file-supplied offset, length, and index through a single overflow-safe range check (`Verify()`) *before* dereferencing anything — malformed or corrupt input is rejected with an error code, never a crash. Six lifecycle callbacks (`Pre`/`Post` × `Saving`/`Restore`, plus read/write save-data hooks) let you marshal application state alongside the script state.

Restored type-ids are mapped between AngelScript's `typeId` and Zodiac's own id space via `GetZTypeIdFromAsTypeId` / `GetAsTypeIdFromZTypeId`.

## Usage

```cpp
#include "zodiac.h"
#include "zodiac_addon.hpp"   // include in exactly one translation unit

// --- Save ---
auto zodiac = Zodiac::zCreateZodiac(engine);
Zodiac::ZodiacRegisterAddons(zodiac.get());          // array/grid/dictionary/...
zodiac->SetProperty(Zodiac::zZP_SAVE_BYTECODE, true);

// register your own types as needed:
zRegisterRefType(zodiac, MyType, "MyType", /*namespace*/ nullptr);

FILE * fp = fopen("save_file.zdc", "wb");
auto file = Zodiac::FromCFile(&fp);
if (zodiac->SaveToFile(file.get()) != Zodiac::zE_Success)
    fprintf(stderr, "save failed: %s\n", zodiac->GetErrorString());

// --- Load (into a fresh engine that recompiled the same modules) ---
FILE * in = fopen("save_file.zdc", "rb");
auto infile = Zodiac::FromCFile(&in);
if (zodiac->LoadFromFile(infile.get()) != Zodiac::zE_Success)
    fprintf(stderr, "load failed: %s\n", zodiac->GetErrorString());
```

A fuller, runnable example (script sources, a suspended context, custom save-data) lives in [`example/`](example/).

## Building

Zodiac is a single static library (`src/*.cpp` + `include/zodiac.h`); the parent engine normally compiles it in-tree. It also builds standalone with CMake and ships a GoogleTest suite.

```bash
cmake -B build -S . -DANGELSCRIPT_SDK=/path/to/angelscript/sdk
cmake --build build
ctest --test-dir build --output-on-failure
```

Notes:

- Requires an AngelScript SDK (headers + sources; no prebuilt lib needed — CMake compiles the VM). Point `ANGELSCRIPT_SDK` at it.
- The test target builds under **AddressSanitizer + UndefinedBehaviorSanitizer** by default; a green `ctest` means green under sanitizers. Pass `-DZODIAC_NO_SANITIZERS=ON` to turn them off.
- Options: `ZODIAC_BUILD_TESTS` (default ON), `ZODIAC_BUILD_EXAMPLE` (default OFF).

Contributor-facing build/environment details (SDK patch requirements, sanitizer layout, architecture) are in [`CLAUDE.md`](CLAUDE.md).

## Status

Work in progress. The load path has been hardened into a real trust boundary against malformed input, and a standalone test suite covers round-trips (object graphs, aliasing, suspended contexts), the exception table, callback wiring, and malformed-input regressions.

## License

MIT — see [`LICENSE`](LICENSE). Copyright (c) 2021 SpehleonLP.
