# Integrate the APIs

Include `<yukiplt.hpp>` for the complete C++ API or `<yukiplt.h>` for
the complete C ABI. Keep the selected images loaded while inspecting,
installing and restoring them.

## Select an image and import

Use `find_images()` with an explicit path, basename, load bias or main-image
selector. `find_image_containing()` finds an image by a LOAD address. Discovery
does not pin mappings or guarantee visibility into every linker namespace.
Prefer the `*_named` entry points when installing into a discovered system
image: they verify the exact saved path as well as bias and program headers.

`inspect_imports()` and `inspect_versioned_imports()` return owning metadata
without reading GOT values or changing memory. An `ImportSelector` combines a
symbol name with any, unversioned or exact version matching. Optional hidden-bit,
declared-dependency and relocation-index filters all apply together. A declared
dependency does not identify the actual provider. Multiple distinct matching
slots are ambiguous; repeated relocation records for one slot are malformed.

The name/version APIs handle `JUMP_SLOT` only. For function imports use
`inspect_function_imports()` and `hook_function_import(s)()` with explicit source
and type masks. Portable source constants are `kSourceNativeDynamic` and
`kSourceNativeAndroidPacked`; REL and RELA constants remain distinct.
Both explicit function relocation types require an undefined `STT_FUNC` with
effective addend zero. `GLOB_DAT` also requires a nonzero current slot; a zero
`JUMP_SLOT` is allowed. An ineligible
matched import rejects the entire request before modification.

Use `hook_batch_nonnull_diagnosed_named()` when every replacement needs a callable
original. It keeps the name-based `JUMP_SLOT` selection rules, including defined
symbols, and rejects any zero original under the operation lock before publishing
originals or writing slots. Other name-based entry points continue to allow zero
`JUMP_SLOT` values.

## Install and retain recovery state

Finish binding imports before installation. Custom loaders must complete all
relocations; NOW flags alone do not prove readiness. Use an exact replacement
function signature and calling convention, including ARM/Thumb address bits.
Do not install into the image containing the executing copy of YukiPLT.

Store original addresses in stable, aligned memory through recovery and
caller-established quiescence. Non-null outputs must be
distinct and cannot alias target slots or existing owners. Use
`OriginalSlot<FunctionPointer>` for typed storage, or `load_original()` /
`yukiplt_c_load_original()` for raw outputs. These helpers perform an acquire
fence before the acquire load; an acquire load alone is insufficient on ARM.
Never clear or reuse a published original slot. Its value can be another
framework's replacement, and a failed installation can still publish it.

Check the error and retain every live handle, including `Indeterminate`
results. Read [recovery rules](lifecycle.md) before disposing of handles.
A one-image batch accepts 1–64 requests. It validates every request first,
then commits with CAS and reverses earlier writes on conflict. Other threads
can observe intermediate slots; the batch is not a global atomic switch.

## Plans, sessions and candidate selection

`build_hook_plan()` / `build_function_hook_plan()` copy metadata without
freezing GOT values or extending mapping lifetime. Prepare a session, then
install it and inspect each image result. Groups execute independently;
failure in one image does not roll back other images. Unhook attempts all
groups in reverse order and retains failed groups for retry.

Function requests can require an expected current slot value. Candidate
expansion supports exact, prefix, suffix and substring matching, followed by
an optional host callback over owned snapshots outside library/linker locks.
Convert chosen candidates into an explicit plan before installation.

## C callers

Initialize each structure's `struct_size` to its documented size, probe new
features with `yukiplt_c_has_feature()`, and provide buffers required by the
individual query. Handle tokens have unique ownership: do not copy them to
create another owner or pass them to another library copy. No STL objects or
exceptions cross the C boundary. See [ABI compatibility](abi.md).

## Owned C metadata

Include `yukiplt.h` and probe
`YUKIPLT_C_FEATURE_OWNED_IMAGES` / `YUKIPLT_C_FEATURE_IMPORT_SNAPSHOTS`.
Image lists and import snapshots own their strings without pinning mappings.
Initialize output tokens to zero. Read required record/text sizes from the
same object, allocate caller-owned buffers, and copy; short buffers return
`OUT_OF_BOUNDS` with required sizes and no partially copied records or text.
Text references are offsets into the copied buffer, with lengths excluding
NUL. Destroy the issuing object explicitly; copied data remains caller-owned.

Plain inspection validates only the existing JUMP_SLOT path. Request versioned
or explicit-source function inspection when needed. Unique selection still
rejects ambiguity; snapshot addresses are metadata, not installation authority.
Keep the issuing library loaded for all queries and object destruction. Tokens
are typed owners scoped to one library copy; a copied token is only a borrowed
view. Serialize queries and destruction on the same object. No exception crosses
the C boundary. The core and runtime remain C++ even for pure C source consumers.

## C plans and sessions

Probe `YUKIPLT_C_FEATURE_PLANS` and `YUKIPLT_C_FEATURE_SESSIONS`. Build an owned
plan with `yukiplt_c_build_plan` or `yukiplt_c_build_function_plan`, then call
`yukiplt_c_prepare_session`. Preparation copies the plan and allocates result
storage; the plan can then be destroyed. Query target text sizes and copy
individual target records, or read each group's target indices and state.

Install returns both an error and a summary. A partial failure retains the
session and every installed/recovery group; inspect all results. Destroying a
session with any live handle fails with `INVALID_ARGUMENT` and preserves it.
Retry clean failed groups, or explicitly unhook; unhook tries all groups in
reverse order. Invalidated hooks consume their handles but still report the
invalidation error. Abandon preserves the preceding recovery snapshot and does
not restore memory. Destroy is permitted after all handles are consumed, but
replacement/context/original storage still require the caller's quiescence.

Original outputs are borrowed stable storage. They are not owned by the C plan
or session and must not be reset or reused after publication. The `original`
field in a copied target record is diagnostic, not an atomic reader.

## C rules, reports and candidates

Probe `YUKIPLT_C_FEATURE_RULES` and create a `yukiplt_c_rules` owner. Register
plain or function selectors with stable original arrays, then explicitly refresh
or send custom-loader notifications. Each mutating operation takes a zero report
owner. A failure can still return a report; always query and destroy nonzero
reports. The report owns its events and survives later RuleSet operations.
`summary.retained` is operation-local; use statistics for all live handles.
Destroy refuses a RuleSet with any live/recovery handle. Pruning never reuses
original slots or removes abandoned history. Binding snapshots own their paths
and remain readable after pruning, but copied binding indices are historical.

Probe `YUKIPLT_C_FEATURE_CANDIDATES` to expand candidates from an explicit list
of images. Byte-based exact/prefix/suffix/substring filters have no pattern
syntax. Optional host predicates receive borrowed metadata outside all internal
locks, can reenter other APIs, and must explicitly accept/reject or return an
error. Failure returns no candidate list; callback side effects are not undone.
C adaptation and the core scanner share the same operation-wide materialization
budget. Copy selected metadata into caller buffers before building an explicit
function plan; a candidate address alone never authorizes a patch.

For explicit parent-snapshot reuse, cached restoration and fork coordination,
see [Cached maps and controlled fork](cached-maps.md).
