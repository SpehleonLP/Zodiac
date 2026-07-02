# Zodiac Red-Fix Plan — Spec

**Date:** 2026-07-01
**Status:** ready for implementation
**Repo:** `/mnt/Passport/Libraries/Zodiac` (standalone gtest build; engine compiles `src/` in-tree)
**Driver:** [`docs/zodiac-red-findings.md`](../zodiac-red-findings.md) — 7 runtime reds (R1–R7) + 3 structural findings (S1–S3), all committed as failing tests / documented gaps.

## Purpose

Turn the red-hunt findings into fixes so Zodiac is "just works" infrastructure. Each fix is **driven by an existing red test that must go green** (or a new test where the finding is a compile-level gap). This is fix-in-place — same non-goals as the prior hardening spec (no I/O-model rewrite, no type-id-space collapse, no fn-ptr-cast change), **plus one deliberate format change** (Part B, string length-prefixing) and the add-on modernization the reds exposed as the largest blocker.

## Decisions (confirm before Part B / Part A1)

1. **String format change (Part B).** Fixing embedded-NUL (R1) requires length-prefixed strings — an on-disk format change. There are no shipped saves (WIP), so we **bump the header format version and reject older images** rather than maintain back-compat. *Recommended: yes.*
2. **Retire the CContextMgr glue (Part A1).** The `CContextMgr` save/load block in `zodiac_addon.hpp` is the single biggest compat blocker (it reaches into now-`protected` members). The owner does not use `CContextMgr` — it existed only to hand-test multi-context save, which is already covered by the raw `SaveContext`/`LoadContext` path (`test_multi_context`, `test_context_roots`). *Recommended: move it to an optional `zodiac_contextmgr.hpp` gated on a patched-SDK macro (default off); the stock build does not compile it.*

Everything else below is closed-ended and needs no decision.

## Environment (unchanged from prior spec)

- Build dir MUST be on an exec-capable ext4 path (`~/Developer/Build/Zodiac-Debug`), never under `/mnt/Passport` (no-exec FUSE mount).
- Pinned SDK: `/mnt/Passport/Libraries/svn/angelscript-code/sdk` (stock 2.38.0 add-ons + patched `angelscript`).
- Test target builds under `-fsanitize=address,undefined`. "Green" means green under sanitizers.
- Each phase ends green under ASan/UBSan before the next begins. No new test code under `src/`.

---

## Part A — Add-on SDK compatibility (finding S1; unblocks the container layer)

`include/zodiac_addon.hpp` does not compile against the pinned stock SDK, so the test harness registers **only `std::string`** (`tests/support/addons.cpp`) and no container type is exercised at all. Modernize the header against the current add-on API, then wire it into the harness. Anchors are current SDK signatures (verified 2026-07-01).

### A1 — CContextMgr glue → optional, gated (per Decision 2)
- Move `ZodiacSaveContextManager` / `ZodiacLoadContextManager` (`zodiac_addon.hpp:33–116`) out of `zodiac_addon.hpp` into a new `include/zodiac_contextmgr.hpp`, compiled only when `ZODIAC_HAVE_PATCHED_CONTEXTMGR` is defined.
- Reason: they use `mgr->m_getTimeFunc`, `m_threads`, `m_currentThread` (all `protected`, `contextmgr.h:84–88`) and `CContextMgr::SContextInfo` (forward-declared only, `:23`). Stock has no public path to these.
- The stock build defines nothing → block absent → no compat burden. Multi-context stays covered by the raw-context tests.

### A2 — Dictionary glue rewrite (`zodiac_addon.hpp:273–378`)
- Stop writing protected members `dict->m_typeId` / `dict->m_valueObj` (`scriptdictionary.h:105,107` are `protected`). Use the public **`CScriptDictValue::Set(engine, void* value, int typeId)`** (`:79`) for object values.
- Replace removed **`CScriptDictionary::Insert(...)`** with public **`CScriptDictionary::Set(const dictKey_t& key, void* value, int typeId)`** (`:127`).
- Rework the save iterator: `CIterator` no longer yields a `CScriptDictValue`; per entry use `GetKey()` (`:176`), `GetTypeId()`/`GetAddressOfValue()` (`:181`) to serialize key + typed value. Note `dictKey_t` may not be `std::string` — serialize it as whatever the SDK aliases (`dictKey_t`), not a hard-coded string.
- Keep the "type punning" `int64_t value` primitive path but route object values through `SaveScriptObject`/`LoadScriptObject` as today.

### A3 — WeakRef glue (`zodiac_addon.hpp:~488–540`)
- `CScriptWeakRef::GetObjectType()` no longer exists → use **`GetRefType()`** (`weakref.h:41`) uniformly (the save glue currently calls both `GetObjectType()` and `GetRefType()`).
- Load path already uses the valid ctor `CScriptWeakRef(void* ref, asITypeInfo* type)` (`:19`) — keep.

### A4 — ScriptHandle glue (`zodiac_addon.hpp:446–483`)
- `CScriptHandle::GetRef()` is **non-const** (`scripthandle.h:40`); the save glue holds `CScriptHandle const*`. Take a non-const pointer in the save signature (or `const_cast` at the single call site) — do not add a spurious const overload to the SDK.

### A5 — Verify the remaining blocks compile & are active
- Confirm the `array` / `grid` / `any` / `scriptfile` / `datetime` / `scriptmath` blocks compile against the pinned SDK.
- Audit every `#ifdef <GUARD>` in `zodiac_addon.hpp` against the actual header guard macro (e.g. `SCRIPTWEAKREF_H` vs the SDK's real guard) so no block is silently skipped.

### A6 — Wire the real add-ons into the harness
- Replace the string-only stub in `tests/support/addons.cpp` (`RegisterEngineAddons` / `RegisterZodiacAddons`) with the full registration via the now-compilable `zodiac_addon.hpp`, kept in **exactly one TU** (ODR: the header defines non-inline free fns).

**Acceptance (Part A):** `zodiac_addon.hpp` compiles against the pinned SDK; harness registers array/dictionary/grid/any/handle/weakref. Add these **new** round-trip tests, all green under ASan:
- `array<int>` values; **empty array vs null `array@`**; `array<string>` (nested heap);
- `array<Node@>` where two elements + a global alias one `Node` (identity preserved through the container);
- `dictionary` mixed value types (int, string, handle) + empty;
- `grid` values; `CScriptHandle` to a script object; `CScriptAny` holding primitive/handle;
- `weakref<T>` to a live object (valid) and to a released object (expired → null).

---

## Part B — String content fidelity (R1)

`SaveString(const char*)` truncates at the first NUL and the string table is a single NUL-terminated blob, so `"a\0b"` restores as `"a"` (`StringContent.EmbeddedNul`).

- Change the string API to length-explicit: `int SaveString(const char* p, uint32_t len)` storing an explicit length per entry (length-prefixed records, or a parallel length table), and `LoadString` returning `{const char* data, uint32_t len}` (or filling a `std::string` of exact length). Keep a convenience `SaveString(const char*)` = `SaveString(p, strlen(p))` for **names** (type/namespace/module names — legitimately C-strings).
- Update the `std::string` glue (`tests/support/addons.cpp` `ZodiacSaveString`/`ZodiacLoadString`, and the header equivalents) to write/read the exact byte length.
- `Verify()`'s "string table NUL-terminated at final byte" rule is superseded by length-bounds validation: every `{offset, len}` string record validated via `InFile`.
- **Bump the header format version** (Decision 1); old images rejected with `zE_BadFileType`.

**Acceptance:** `StringContent.EmbeddedNul` green; all other `StringContent.*` stay green; malformed-input net extended with a crafted out-of-range string length.

---

## Part C — Enum support (R3)

A module containing an `enum` fails `SaveToFile` with `zE_BadTypeId` (`Enum.MemberAndGlobalRoundTrip`). The throw is in `SaveTypeId` (`z_zodiacwriter.cpp:790,858`) when an enum typeId has no `asITypeInfo` object treated as a saveable type.

- Enumerate enum types when building the type table (engine + per-module `GetEnumCount`/`GetEnumByIndex`), and make `SaveTypeId`/`LoadTypeId` resolve enum typeIds instead of throwing.
- Enum-typed storage is int-sized: the property/global **value** path only needs the enum typeId to round-trip through the type table (no per-value encoding beyond the int).

**Acceptance:** `Enum.MemberAndGlobalRoundTrip` green (enum member = 5, enum global `Blue` = 6).

---

## Part D — Missing guards (R4, R6)

Two silent-success trust holes.

### D1 — Double-load rejection + leak (R4)
- A second `LoadFromFile` into the same `zCZodiac` returns `zE_Success` and leaks 556 B (the first load's objects). Add an "already loaded" state on `zCZodiac` (distinct from the transient `m_inProgress`, `z_zodiac.h:72`); the second `LoadFromFile` returns **`zE_DoubleLoad`** before allocating anything. The guard eliminates the leak.

### D2 — Save-context-without-bytecode rejection (R6)
- `SaveToFile` with `SAVE_BYTECODE=false` while a context is handed to `SaveContext` returns `zE_Success`, producing an image whose context references bytecode that was never written. `SaveContext` (or the save entry) must return **`zE_CantSaveContextWithoutBytecode`** when a context is saved without bytecode enabled.

**Acceptance:** `EdgeBehaviors.DoubleLoadRejected` and `ContextRoots.SaveContextWithoutBytecodeRejected` green; ASan reports no leak.

---

## Part E — Bounded graph traversal (R2)

A 50k-node chain overflows the stack (`Cycles.DeepChain`, ASan stack-overflow) because the object-graph walk recurses per node.

- Convert the recursive walk in the writer's `SaveScriptObject` and the reader's `LoadScriptObject` (the property-chasing recursion) to an **explicit work-stack** (heap-allocated worklist), so depth is bounded by heap, not the C stack.
- Preserve the existing address-unification / `beingLoaded` bookkeeping semantics exactly — this is a traversal-shape change, not a graph-semantics change.

**Acceptance:** `Cycles.DeepChain` (50k) green; add a 200k-node stress that stays ASan-clean; all existing graph tests (aliasing, cycles, rings) stay green.

---

## Part F — Context serialization: reconcile with the trunk contract (R5, R6-adjacent, R7)

**Root-cause history (from the owner).** Context (call-stack) serialization is not a stock AngelScript feature — the owner *authored* it, submitted it upstream, and **Andreas significantly revised the API before it merged into trunk (2.38.0)**. `z_zodiaccontext.cpp` was subsequently adapted to the new *signatures* (the commit `harden: adapt suspended-context save/restore to trunk AS API`; comments cite "trunk PushFunction is 2-arg", `GetAddressOfVar` replacing the owner's original `SetVarContents`) — enough to compile and pass the **single-frame** case. But the reds show the revised *semantics* are not fully honored. So Part F is a **reconciliation against the actual trunk contract**, not a bolt-on; F1/F2 below are the observed symptoms and may be downstream of it.

### F0 — Audit `z_zodiaccontext.cpp` against the trunk `asIScriptContext` serialization API
Source of truth = `angelscript.h:996–1003` + the VM's deserialization invariants (there is **no** in-SDK reference serializer for contexts; the `add_on/serializer` is the object-graph CSerializer, unrelated). Specifically reconcile:
- **`GetArgsOnStackCount(stackLevel)` (`angelscript.h:1003`) — NOT called by zodiac at all.** Andreas's revised design surfaces arguments already pushed on the stack for an in-progress call; if zodiac neither saves nor re-pushes them, an object passed as an on-stack argument at suspend time is lost — a direct candidate for the R5 SEGV. Add save + restore of args-on-stack.
- **`objectRegister` AddRef.** `SetStateRegisters` stores the object register pointer raw without AddRef (zodiac's own note, `z_zodiaccontext.cpp:95–98`); reconcile ownership so the object register is not double-freed or leaked.
- **`Get/SetStateRegisters` vs `Get/SetCallStateRegisters`** frame classification (`isCallState`) — verify each frame is restored through the matching pair, matching the trunk VM's expectation.

### F1 — Context-stack objects as graph roots (R5)
- An object reachable **only** through a suspended context's stack (a local or an on-stack argument) is not discovered during save, so on restore it is absent and resuming SEGVs in `CallObjectMethod`. `SaveContext` must enumerate the context's stack variables *and args-on-stack* (F0) as graph roots for `SaveScriptObject`. Likely resolved by F0; verify.

### F2 — Cross-root aliasing double-populate (R7)
- When a context-local handle and a global alias the same object, the loader loads the object into an already-populated slot → assert `dst && *(void**)dst == nullptr` (`z_zodiacreader.cpp:859`). The load must consult `m_loadedObjects[address]` and, when already loaded, **assign the existing pointer (with AddRef)** instead of re-loading. Applies wherever a handle slot is populated (globals, members, context vars).

**Acceptance:** `ContextRoots.ContextOnlyObjectSurvives` and `ContextRoots.ContextGlobalAliasing` green; add tests for (a) an object passed as an on-stack **argument** at suspend, (b) two contexts sharing one object; existing single-context tests stay green.

---

## Part G — Static / latent fixes (S2, S3)

- **G1 (S2):** `z_zodiacreader.cpp:759` — `assert(m_loadedObjects[address].asTypeId = stored_id)` uses assignment; change to `==`. Confirm the intended invariant holds (it currently "passes" only by side effect).
- **G2 (S3):** `z_zodiacwriter.cpp:580` — `GetByteLengthOfType` returns `0` for a non-POD registered value type (`//what to do??`). Return the correct size (`type->GetSize()`), or route through the registered save/load; never silently 0. Add a test with a non-POD app value type (registered with a ctor/dtor) as a member + global.

---

## Sequencing

1. **Part A — add-on compat.** Biggest unblock, owner-requested, and it grows the test surface every later part benefits from. Land A1 (retire ctxmgr) first to make the header compile, then A2–A6, then the new container tests.
2. **Parts C, D, G** — small, localized, high-signal (enum, guards, static typos). Green R3, R4, R6.
3. **Part B — string format** — self-contained but format-affecting; do after the small fixes, with the version bump.
4. **Part E — bounded traversal** — the recursion rework; isolated to the two walk functions.
5. **Part F — context roots / aliasing** — the deepest change; do last, on top of a fully green base.

Each part is one or more commits; run the full suite (incl. the three current crashers, once fixed) under ASan/UBSan at each step.

## Definition of done

- All of R1–R7 green under ASan/UBSan from an ext4 build dir; the three crashers (R2/R5/R7) no longer abort the process.
- `zodiac_addon.hpp` compiles against the pinned SDK; container round-trip tests (Part A acceptance) added and green.
- Static findings S2/S3 fixed with covering tests.
- Header format version bumped for the string change; old images rejected cleanly.
- Baseline 26 + red-hunt suite all green; `example/` still builds; non-goals untouched; no test code under `src/`.

## Out of scope (unchanged non-goals)

I/O-model rewrite, type-id-space collapse, fn-ptr-cast type-erasure, hot-reload/schema-merge, `zCMemoryMap` rename. Where a fix touches these seams, fix the bug at the seam — do not rework the seam.
