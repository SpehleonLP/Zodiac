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
