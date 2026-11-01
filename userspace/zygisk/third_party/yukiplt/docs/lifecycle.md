# Lifetime and recovery

Keep the image, replacement code/context and original-output storage alive
until restoration finishes. Then establish quiescence before freeing code,
context or original-output storage: successful unhook does not wait for calls already executing or prevent
future calls through cached function pointers. Hook handles and sessions do
not implicitly perform fallible restoration in their destructors.

## Recover an incomplete operation

`Indeterminate` means rollback or page restoration could not be confirmed.
Retain the handle and inspect its recovery diagnostic, slot states and residual
pages. These records describe the last operation, not current memory mappings.
Resolve the underlying mapping, permission or ownership conflict, then retry
unhook. Live retry reads current maps. Explicit cached retry uses the supplied
snapshot and tracked protection state. Both restore only values/protections
still owned by the operation and preserve third-party slot values.

Successful unhook and confirmed `ImageInvalidated` consume the handle.
Preflight conflicts, maps errors, permission failures, allocation failures and
`Indeterminate` retain it. `abandon()` relinquishes bookkeeping without restoring
memory; it does not make replacement code safe to unload.

## System and custom images

System-image work validates identity inside `dl_iterate_phdr`. Named entry
points also require the exact saved path; all successful system installations
record the live path for subsequent restoration and live queries. A bare
`ImageView` cannot distinguish a different occupant already present before
installation. Even a named view cannot detect same-path, same-address reload.
Coordinate unhook and unload yourself.

For a custom image, assign a unique nonzero `instance_token` for every load and
keep mappings and metadata stable. Call `notify_loaded()` after relocation and
final page protections, and `notify_unloading()` before unmapping. Notifications
are synchronous and accept only custom images. Do not hold a loader lock or
call them from `dl_iterate_phdr`. If restoration fails, keep the image and
handles alive; resolve the failure and retry. `forget_image()` explicitly marks
an instance invalid and never substitutes for restoration of a live mapping.

## Rule sets

Register rules with caller-owned original storage. Call `refresh()` explicitly;
there is no background loading tracker. Enumeration and report preparation
finish before installation. Preparation failure installs nothing; later image
operations may partly succeed. Operation `retained` counts only this call's
enumerated targets. Use `statistics()` for all outstanding bindings.

Clean installation failure retries in place using the same original slot.
Released or invalidated bindings become new bindings on a later refresh or
load notification, consuming new original slots. Abandoned bindings do not
reapply. Use `remove_rule()` to stop a rule permanently, and still check any
restoration failure. Capacity exhaustion remains terminal; register a new rule
with larger storage to recover.

`prune_history()` removes released/invalidated records without live handles.
It keeps failed and abandoned records, never recycles original slots and does
not reset capacity. Copy needed reports first: successful pruning invalidates
binding references and indices. Allocation failure preserves the object.

## Thread and loader coordination

Free functions support concurrent calls. Serialize operations and result reads
on each handle, session or RuleSet. The lock order is linker then operation;
host callbacks run outside both. Avoid synchronous reentry through hooks on
allocators or other functions used inside YukiPLT operations. Keep calls outside
signal handlers and cancellation/longjmp boundaries. Child calls after a
multithreaded fork before exec use explicit automatic atfork activation together
with the host runtime coordination described
in [Cached maps and controlled fork](cached-maps.md).

Serialize other writers of page permissions against YukiPLT. Live transactions
read maps once, while cached transactions use their explicit snapshot and tracked
state. The host must exclude external `mprotect` and remapping during each operation.
CAS does not prove ownership history or prevent address/value ABA.

Each loaded copy owns a separate registry and operation lock. Do not let copies
hook the same slot; serialize their work on shared pages. Obtain entry points
from the intended library handle. Salted handle IDs detect misuse with high
probability and are not a security boundary.

Keep the core's owning load group alive through recovery. In bionic, a NODELETE
dependency first loaded by an unloadable host DSO is not an independent lifetime
guarantee. Unhook before unloading that host and establish quiescence separately.

## Reclaim idle core storage

After consuming every hook/recovery handle, call `yukiplt_c_trim_idle_storage()`
(feature `YUKIPLT_C_FEATURE_IDLE_STORAGE`) to release cached Registry storage,
including capacity retained after an allocation failure. C++ hosts may call the
same C entry point. The operation is serialized with installation/restoration;
any outstanding record, including an invalidated handle not yet consumed,
returns `InvalidArgument` without changing storage. Later installation remains
supported, and old tokens never become valid again. Generation exhaustion
refuses further installs with `IntegerOverflow` while allowing recovery.

Trim does not destroy snapshots, plans, sessions or rule sets; does not touch
original storage or abandoned memory effects; and does not establish quiescence.
Keep its code resident until all objects are destroyed and all API, callback and
replacement calls (including cached pointers) are excluded. Diagnostics use operation-owned context and do not require compiler TLS. The
linked C++ exception runtime may still use private emulated TLS. Core storage
reclamation cannot reclaim private compiler/runtime TLS blocks: for an unloadable
static-runtime host, end its participating threads before host unload. A host
that cannot satisfy its runtime lifetime requirements must remain resident.
