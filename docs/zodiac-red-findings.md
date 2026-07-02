# Zodiac red-hunt findings (2026-07-01)

Goal of this pass: accumulate failing gtests that expose real defects, as input to
a fix-planning spec. **No fixes applied here** — tests assert *correct* behavior and
go red where Zodiac is wrong. Baseline 26 tests untouched and still green; `src/`
not modified.

Harness note: only `string` has working Zodiac add-on glue in the test harness
(`tests/support/addons.cpp`). `zodiac_addon.hpp` does not compile against the pinned
stock SDK, so container types (array/dictionary/grid/any/handle/weakref/datetime)
could not be exercised at all — see finding **S1**.

## Runtime reds (7 distinct root causes)

| # | Test | Symptom | Likely root cause / anchor |
|---|------|---------|----------------------------|
| R1 | `StringContent.EmbeddedNul` | `"a\0b"` (len 3) restores as `"a"` (len 1) | `SaveString(const char*)` truncates at NUL; string table is NUL-terminated and `LoadString` returns a C-string. Content with embedded NUL cannot survive. (`addons.cpp` ZodiacSaveString; reader `LoadString`) |
| R2 | `Cycles.DeepChain` | 50k-node chain → **ASan stack-overflow (crash)** | Graph walk in `Save`/`LoadScriptObject` recurses per node; unbounded depth. Needs an explicit work-stack. |
| R3 | `Enum.MemberAndGlobalRoundTrip` | `SaveToFile` returns `zE_BadTypeId` (-8) for any module containing an `enum` | enum typeIds not handled in the type/save path. |
| R4 | `EdgeBehaviors.DoubleLoadRejected` | 2nd `LoadFromFile` into same zodiac returns `zE_Success` (should be `zE_DoubleLoad`); **also leaks 556 B** | missing double-load guard; second load overwrites bookkeeping and leaks the first load's objects. |
| R5 | `ContextRoots.ContextOnlyObjectSurvives` | object reachable **only** via a suspended context's stack → **SEGV** in `CallObjectMethod` on resume | context-stack objects are not discovered/serialized as graph roots. |
| R6 | `ContextRoots.SaveContextWithoutBytecodeRejected` | saving a live context with `SAVE_BYTECODE=false` returns `zE_Success` (should be `zE_CantSaveContextWithoutBytecode`) | missing guard; produces an unusable image (context refers to bytecode never written). |
| R7 | `ContextRoots.ContextGlobalAliasing` | a context-local handle and a global aliasing one object → **assert** `dst && *(void**)dst == nullptr` at `z_zodiacreader.cpp:859` | cross-root aliasing double-populates a load slot. |

Crashers (R2, R5, R7) abort the process, so run them in isolation; the other four
fail cleanly. Full run minus the three crashers: **49 passed / 4 failed of 53**.

### R8 (found during fix execution, 2026-07-01)

| # | Test | Symptom | Likely root cause / anchor |
|---|------|---------|----------------------------|
| R8 | `EdgeBehaviors.SaveWithoutBytecode` | a *single* successful bytecode-less round-trip leaks **556 B / 2 allocs** (ASan, leak-detect on) — trace roots at `asCBuilder::RegisterClass` during the load-side recompile | **FIXED (2026-07-02, Part F follow-up).** Root cause was `RestoreGlobalVariables`: a handle global (`Box@ box`) already holding an app-created object (the load-side rebuild ran the module's `setup()`) was overwritten by the restored object without releasing the prior value. `PopulateTable`/`DocumentGlobalVariables` cannot register handle globals (their `dst` is the pointer slot, not the object), so the old object was never unified and leaked — pinning its class type (the 520 B indirect alloc). Fix releases the prior handle value before `LoadScriptObject` overwrites the global slot. Whole suite now leak-clean under ASan (72 tests). |

## Second red-hunt pass (2026-07-02) — context serialization + add-on depth

Motivation: the context (call-stack) serialization API moved under Zodiac when
Andreas revised the owner's upstream submission, and the Part A add-on coverage
only tested each container one level deep (top-level `container@` global). This
pass drove harder on both. **22 new tests, 11 red / 11 green** (all under
ASan/UBSan; baseline 72 untouched). Crashers fail cleanly under `ctest`
per-test isolation. New tests live in `tests/test_context_frames.cpp` and
`tests/test_container_deep.cpp`.

**Resolution (2026-07-02):** all 11 reds fixed — R10 (any-glue, Part G, `5f9a7ad`),
R11/R12/R13 (context cluster, `8bbd7a1`), R9 (test double-deref, Part H, `f2d4a35`).
Suite now **94/94 green under ASan/UBSan** (1 intentionally disabled). R9 turned
out to be a test-body defect, not a library bug (see its row).

| # | Test(s) | Symptom | Likely root cause / anchor |
|---|---------|---------|----------------------------|
| R9 | `ContainerDeep.NestedIntArray`, `NestedStringArray`, `ScriptClassWithArrayMember`, `ScriptClassWithStringArrayMember`, `ScriptClassWithDictionaryMember` | A container reached as an **array element** or a **script-class member** (not as a top-level `container@` global) restores **corrupt**: `LoadFromFile` returns `zE_Success` but the element/member pointer is garbage → **SEGV** on first use (`CScriptArray::GetSize`). Top-level `container@` globals, `array<Node@>`, `array<dictionary@>`, `dictionary`-holding-`array` all PASS. | **FIXED (2026-07-02, Part H) — test defect, not a library bug.** The restore path was already byte-correct. `array<int>`/`dictionary` are ref types, so as a non-handle element/member they are stored as object pointers and **both accessors already dereference them** (`CScriptArray::At` scriptarray.cpp:963; `asCScriptObject::GetAddressOfProperty` as_scriptobject.cpp:803-805). The tests applied the handle-slot idiom `*static_cast<T**>(accessor(...))` — a second deref reading *into* the returned object → garbage → SEGV. Crashes on any correctly-built object, serialized or not; the passing `@`-handle siblings are correct because there `At()` returns the slot. Fix removes the extra indirection (asserted values unchanged); an attempted src/ fix was reverted (it broke the writer, which relies on the auto-deref). |
| R10 | `ContainerDeep.AnyHoldsString`, `AnyEmpty` | `any` holding a **string** → **SEGV in `z_zodiacwriter.cpp:746 SaveString`** during save. `any` holding **nothing** → `CScriptAny::Retrieve` assert (`refTypeId` from a typeId-0 payload). `any` holding int64 / a script handle / an `array@` all PASS. | **FIXED (2026-07-02, Part G).** Save used `Retrieve(&value,...)` into an 8-byte union, object-copying a >8-byte value type (string) into 8 bytes → stack smash → garbage pointer to `SaveString`. Now uses `GetObjectAddress()` (mirrors the dictionary path): handle→slot, value-type→object pointer, exactly what `SaveScriptObject` wants; load reconstructs value-type payloads into engine storage + `StoreMove`-by-reference. Empty-any (`asTYPEID_VOID`) now emits a typeId-0 sentinel instead of routing through `Retrieve`/`Store` (which assert on 0). `zodiac_addon.hpp` only; no src/ change. |
| R11 | `ContextFrames.ArrayLocalSurvivesSuspend`, `MixedLocalsSurviveSuspend` | Saving a suspended context that has an **`array<int>` stack local** fails at **save** with `zE_BadTypeId` (-8). `dictionary` / `any` / `grid` context locals all PASS. | **FIXED (2026-07-02, context cluster, 8bbd7a1).** A dead anonymous list-pattern temp (`GetAddressOfVar==nullptr`) had its list-pattern typeId routed through `SaveTypeId`, which rejected it. Save now emits a placeholder record (`typeId=0`) for such slots; load skips `typeId==0` records. |
| R12 | `ContextFrames.ThisAliasesGlobalObject` | Suspending inside a **class method** whose `this` also aliases a **global handle**, then restoring, corrupts a refcount on resume: `asCAtomic::get` assert `value < 1000000` (garbage refcount ⇒ use-after-free class). | **FIXED (2026-07-02, context cluster, 8bbd7a1).** `CtxCallState::PushFunction` now loads the frame `this` via `LoadScriptObject(... | asTYPEID_OBJHANDLE, false)` on a nulled slot guarded by `if(objectId)`, honoring an already-loaded aliasing object; `CtxStackState::SetToContext` keeps the outermost already-prepared function (`i==0`) without releasing it. |
| R13 | `ContextFrames.PreparedContextRoundTrip` | A context that was `Prepare()`d but **never Executed** (`asEXECUTION_PREPARED`) restores in state `asEXECUTION_SUSPENDED`, not `PREPARED`. It still runs to the right result, but the state is not preserved. | **FIXED (2026-07-02, context cluster, 8bbd7a1).** Save/load now special-case `asEXECUTION_PREPARED` (write the function id, `Prepare()` on load) instead of pushing a frame + `FinishDeserialization`. (Test helper `RunToFinish` also updated to drive PREPARED contexts, not only SUSPENDED — a genuine helper bug.) |

### Reassuring PASSES from this pass (not bugs — coverage that now exists)

Add-on depth: `grid<Node@>` handle identity (element aliases a global), `grid<string>`,
`dictionary` holding an `array@`, `array<dictionary@>`, `any` holding an `array@` handle,
`dictionary` handle value aliasing a global.
Context depth: a **6-frame** deep call stack resumes with every frame's local intact;
**re-save of a restored context** (save→load→save→load→resume) is stable; `dictionary`,
`any`, and `grid` context stack locals all survive suspend (only `array` — R11 — does not).

## Structural / static findings (not expressible as a runtime red)

| # | Location | Issue |
|---|----------|-------|
| S1 | `include/zodiac_addon.hpp` vs pinned SDK | Header does not compile against the stock 2.38.0 add-ons (ContextMgr internals now protected; dictionary/weakref/handle API drift). Consequence: **the entire container serialization layer is unusable** — only `string` is paired. Blocks all array/dictionary/grid/any/weakref round-trip coverage. |
| S2 | `z_zodiacreader.cpp:759` | `assert(m_loadedObjects[address].asTypeId = stored_id)` — assignment (`=`) inside an assert, not comparison (`==`). Side-effecting; behaves differently under `NDEBUG`. |
| S3 | `z_zodiacwriter.cpp:580` | `GetByteLengthOfType` returns `0` for a non-POD registered value type (`//what to do??`). Latent: non-POD app value types would get a 0 byte length. |

## What PASSED (reassuring — not bugs)

Ref aliasing, self-reference, 2-cycles, rings; script-class inheritance
(`Base@`→`Derived`), interfaces; namespaced types; multi-module; primitive / bool /
double / const / string globals; global↔global aliasing; delegate members,
delegate globals, self-bound delegate cycles, delegate bound to another global;
imported/bound cross-module functions; custom app **value** type (`Vec2`);
private/protected members; save-without-bytecode (with recompiled module on load);
multi-context + nested call-stack contexts; string context locals; STRIP_DEBUGINFO;
empty/long/UTF-8/duplicate string content; save→load→save idempotence.

## New test files added

`test_string_content, test_cycles, test_inheritance, test_namespace,
test_globals_primitive, test_enum, test_multi_context, test_idempotence,
test_interface, test_multi_module, test_delegate_cycle, test_edge_behaviors,
test_context_roots, test_global_aliasing, test_custom_value_type,
test_delegate_global_alias, test_imported_function, test_strip_debuginfo,
test_context_string_local`.
