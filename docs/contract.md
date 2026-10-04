# Observation contract v1

`inspect kind=world_observer` supports `action=capabilities` and `action=snapshot`
(default). Snapshot fields: refs (max16 FormIDs), nodes (max64 objects with ref,
name<=128bytes, firstPerson boolean), timeoutMs (100..3000, default1500).
Request text is limited to16KiB and at most4 queued/active tasks. FormIDs must be
unsigned32-bit integers or hexadecimal strings; zero and arbitrary selectors
are rejected. Optional physics requests are documented in [physics.md](physics.md).
Render requests are rejected.

The envelope contains schemaVersion, observerVersion, sessionId, loadGeneration,
sampleId, producerFrame, producerMonotonicNs, durationUs, phase and coherence.
The envelope phase is `skse_main_thread_task`; producerFrame can be null when unavailable.
Physics has its own body/contact phase and units. There is no post-physics/pre-render guarantee. Ref/node arrays include availability
and reasons. Unavailable is distinct from a deleted/disabled observed reference.
Quality is complete only when all requested observations are available.

Reference identity includes runtime FormID/handle and load generation; persistent
forms also include source plugin/localFormID when known. Handles are observations
within this process/generation, not identifiers portable across loads. Nodes return
local and world transforms: row-major3x3 rotation, translation in engine units,
scale, bounds and scene-graph collision-object presence. This last flag is not a
physics-body/contact assertion. NaN/Inf transforms are unavailable. No pointers
are serialized; invalid refs and missing3D/nodes return structured unavailable.
Third-person refs/nodes and physics share the typed loaded scene root, falling
back to the explicit third-person getter. First-person node selection continues
to use the first-person getter. This avoids inconsistent nonactor availability
between the reference, node and physics domains.

Load-generation messages invalidate queued work. Snapshots requested while
loading fail; generation changes before execution abandon the sample. Observations
after a failed load remain unavailable; lifecycle events include loadSucceeded
for the post-load notification. Data copied
inside one task is coherent within that main-thread operation, but other engine
domains may run asynchronously. There is no global physics/render snapshot.

The observer emits `world_observer.lifecycle` through DevBench after registration.
DevBench's event sequence/retention remains authoritative; our payload adds session
and generation. The trace client records poll timing/missed intervals, refuses
session changes and emits explicit load-generation boundaries. It never claims
continuous observation between samples, deterministic physics or visual correctness.
