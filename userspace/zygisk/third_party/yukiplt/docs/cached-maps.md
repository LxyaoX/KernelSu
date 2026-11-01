# Cached maps and controlled fork

Use `MapsCache::capture()` while the host holds a stability window over the
relevant images and page permissions. `import_text(data, length)` accepts a full
maps snapshot saved by the host. Both own the parsed mapping data. The host
asserts that imported addresses belong to this process or its controlled parent
and still describe the relevant mappings. Parsing checks structure and bounds.
Input and materialized mapping/page data each have a 64 MiB logical limit and at most
1,048,576 mapping records are accepted.

Pass the cache explicitly to `hook_batch_cached`, `hook_imports_cached`, or
`hook_function_imports_cached`. Named variants bind the system path, and
`hook_batch_nonnull_named_cached` requires nonzero originals. These entry points
accept optional slot reports and operation diagnostics. `unhook_cached` and
`query_cached` use the same cache. A batch of one covers a single import.
System-image operations retain linker identity/path validation. Custom images
retain their unique instance-token and stable-mapping requirements.

`HookSession` and `FunctionHookSession` expose `install_cached` and
`unhook_cached`. RuleSet exposes cached refresh, loader notifications, image
release, rule removal and unhook, including report overloads. Refresh validates
retained bindings with the supplied cache. Every cached operation uses zero
maps reads, including failures. Missing coverage is an error.

## Permission lifetime

Serialize access to a cache, including capture, import, invalidation, moves and
destruction. Keep relevant mappings stable through their last cached access.
Coordinate other permission writers with this library. A page with a live
cache binding accepts the same cache for further operations. Different caches
can operate on disjoint pages. Live operations still read maps and update the
associated bookkeeping. A conflicting observation invalidates the cached page.

The registry retains cache state for live bindings. C cache destruction fails
while those bindings exist. C++ destruction releases the wrapper and performs
no implicit unhook. Retain the wrapper through cached recovery.

Capture and import explicitly replace the snapshot. Failure preserves the
previous snapshot. Active pages must remain covered. Clean pages adopt the new
baseline. Outstanding permission-restoration targets survive replacement,
and pages with restoration obligations outside this cache are marked unusable
until those obligations are resolved and the cache is explicitly refreshed.
Conflicting protections reject replacement. Before an external permission
or mapping change, call `invalidate()` and complete recovery using a fresh
snapshot or the live APIs. Abandonment with page residue and image invalidation
invalidate the affected cached pages.

An incomplete cached transaction retains its handle and permission obligations.
Retry with `unhook_cached`. The implementation repairs unknown residual
protections before accessing slots, after all page validation and allocations.
A slot already holding its original value still requires page restoration.
Cached recovery confirms residual targets with successful `mprotect`, including
when a refreshed snapshot reports the target protection.
Third-party slot values produce conflicts and remain untouched.

## Controlled fork

The host first drains all library calls, callbacks and object access. Call
`prepare_fork()` to initialize the registry, page size and C object store and
check that recovery is complete. Keep this quiet window until the appropriate
completion call. Library locks are released before fork.

After fork, the parent calls `after_fork_parent()`, including when fork fails.
The child completes its host allocator, loader and C++ runtime coordination,
then calls `after_fork_child()` before invoking the library. This is a host
integration contract for controlled Android/runtime call sites. The general
restriction on calls after a multithreaded fork before exec still applies
outside this protocol. [Linux fork contract](https://man7.org/linux/man-pages/man2/fork.2.html)

The child inherits existing handles and C object tokens, with independent
process-local ownership. It can restore a parent's clean installed hook with
`unhook_cached`. A plain installed handle can first bind to a cache during
restoration. An untracked indeterminate handle requires live recovery first.
The host coordinates each library copy separately. Repeated preparation,
incorrect completion calls and unacknowledged PID changes are rejected before
library locking. Inline result access and object destruction remain subject to
the host's quiet-window contract.

## C hosts

Probe `YUKIPLT_C_FEATURE_CACHED_MAPS` and
`YUKIPLT_C_FEATURE_CONTROLLED_FORK`. Create a `yukiplt_c_maps_cache`, capture or
import its snapshot, then pass its token to the cached batch, diagnosed function,
query, diagnosed unhook, session or rule entry points. Destroy it after all
bindings have been consumed. The three fork protocol functions have matching
`yukiplt_c_` entry points. Existing diagnostic layouts and ABI version 1 apply.
The diagnosed batch variants accept optional operation diagnostics. The nonnull
named batch accepts name-only `YUKIPLT_C_SELECTOR` requests and rejects additional
version, hidden-bit, dependency or relocation-index filters.

## Multithreaded fork

Probe `YUKIPLT_C_FEATURE_MT_FORK` and explicitly enable one backend per library
copy. `enable_atfork(config)` and `yukiplt_c_enable_atfork(&config)` accept the
same fixed C configuration layout. Set `struct_size`, choose
`YUKIPLT_C_FORK_NATIVE` or `YUKIPLT_C_FORK_HOST_DISPATCHER`, and set `timeout_ms`.
Zero selects 5000 milliseconds. Matching repeated enable calls succeed.
Control calls require an idle library and return `ForkInProgress` while another
operation, configuration change or fork cycle is active. Retry after it finishes.

The supported call site is ordinary bionic `fork()` from a normal thread outside
YukiPLT operations, predicates, signals and atfork callbacks. Begin library calls
in the child after `fork()` returns. The host coordinates the process-wide
loader, allocator, libc++/unwinder, thread teardown, other atfork handlers and
direct C++ object access. Establish their stable window before entering fork and
keep it through completion. Place YukiPLT's preparation before handlers that
freeze dependencies needed by an in-flight library operation. Maintain relevant
mapping and protection stability through every subsequent cached operation.
This is an Android runtime integration contract. Its verification depends on the
host runtime and the actual API/ABI combination.

The preparation handler closes admission and drains complete operations,
including Session/RuleSet report updates, C object leases and result copies.
Already-admitted synchronous nesting borrows its thread's stack-owned lease.
New calls return `ForkInProgress` during preparation. The admission token uses a
pthread key with a null destructor. Enabling creates the key, and successful
stopping closes admission, excludes token readers and deletes it. Allocation and
exception failures are reported at ordinary API boundaries.

Parent completion opens admission even when fork fails and preserves errno.
A successful child inherits independent ownership of handles, object tokens,
original storage and caches. It can use live and cached APIs, discovery, plans,
sessions and rules under their ordinary mapping and lifetime contracts.

Timeout, reentry, an outstanding recovery record or a protocol violation makes
the child return `ForkChildUnavailable`. Use `fork_status(status)` or
`yukiplt_c_get_fork_status(&status)` with `struct_size` initialized to obtain the
reason and process generation. Status is an observation of concurrent control
state. The rejected child can query this status and detach its dispatcher, then
continue with exec or `_exit`. Re-enabling and a further fork preserve rejection.
The parent keeps its recovery handles and can finish restoration.

C++ Session and RuleSet summary operations provide additional overloads with a
final `Error& admission_error`. Check it before interpreting summaries or stored
reports. It describes admission to this call. After successful admission, inspect
the existing summary and per-target operation errors. Diagnosed core operations
write a preparation-stage `ForkProtocol` diagnostic on admission failure.

### Native registration and system-loaded hosts

Select `YUKIPLT_C_FORK_NATIVE` for a system-loaded copy. The library registers once
through NDK pthread_atfork, whose registration belongs to its owning DSO on
Android API23+. `disable_atfork()` logically stops this registration and deletes
the admission key. Later enabling reuses the registration. Normal DSO finalization
removes its native callbacks. Complete stopping, object cleanup, recovery and
quiescence before unloading the owning load group.

### Resident dispatcher and anonymous hosts

Select `YUKIPLT_C_FORK_HOST_DISPATCHER` for an anonymous unloadable payload. The
resident host supplies `subscribe(context, callbacks, token)` and
`unsubscribe_and_drain(context, token)`. It copies the callback table on successful
subscription and returns a unique generation-bearing token. Error returns are
errno values. A failing or throwing subscription leaves no registration, and a
failing or throwing removal retains the registration and its token.

Each fork uses one fixed subscription set. Run prepare in reverse subscription
order and parent/child completion in forward order, with exactly one completion
for every selected preparation, including fork failure. Serialize subscription
changes with complete fork cycles. Child dispatch repairs its own waiters and
execution accounting before allowing ordinary subscription operations. Parent
and child subsequently own separate subscription sets.

Successful removal excludes future selection and waits for every selected cycle
in the current process to complete. Invoke registration and removal from ordinary
host context outside callback execution. Keep the payload code and context alive
until removal succeeds. Finish the usual API/object/replacement/runtime
quiescence before unmapping the payload. A resident executable reference dispatcher
and a system-unloaded payload fixture are included in the consumer sources. Real
anonymous-loader integration additionally supplies that loader's mapping,
constructor, unwinding and teardown guarantees.

Enable either the automatic protocol or the manual prepare/completion protocol
at a time. Stop automatic registration successfully before selecting manual
coordination. Coordinate separate library copies and shared pages in the host.
