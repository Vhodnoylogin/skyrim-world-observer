# CBPC provider observation: unavailable

`{"kind":"world_observer","cbpc":true}` in the SE candidate returns
`cbpc.status:"unavailable"`, three availability flags false, and snapshot
`quality:"partial"`. This is capability discovery, not a physics collector.
It must never be treated as a zero-contact result or an empty valid inventory.

The reviewed CBPC source is Acro748/CBPCSSE commit
`fabd56ce8f8ac10cde17325f0bd61ecc8cbcda20`, available from
`https://github.com/Acro748/CBPCSSE/tree/fabd56ce8f8ac10cde17325f0bd61ecc8cbcda20`.
Keep its checkout, build dependencies and DLLs outside this repository. The
prospective subject provider DLL has not been matched to that source by a
reproducible build, so private offsets or hooks would have no qualified basis.

The public Papyrus interface can attach/detach capsules but cannot enumerate
the actual containers or establish a completed solver step. With
`isAffectedNode=true`, capsules are in `Thing::thingCollisionCapsules`, in
addition to the collider role. Attach appends; detach removes matching indices.
An attachment return value, requested geometry, configured NIF/HKX capsules,
node motion, CLAW cache, and Havok contact records remain separate evidence.
Limited CBPC player-contact ModEvents lack complete pair/step coverage.

A proper implementation needs a source-matched CBPC producer with a versioned
read-only interface that copies an immutable bounded buffer at a serialized
safe point after parallel workers and with mutation synchronization. The
consumer must not walk live nested vectors or hold provider locks during JSON
serialization. No second HTTP server is needed: Observer can expose the copied
buffer through the existing DevBench extension.

The minimal future producer contract must retain:

- Provider build, process/session, world/root/container epochs, configuration
  and mutation revisions, completed step sequence and declared phase.
- Both roles' actual node/bucket/id, duplicate count, enabled state, exact
  endpoints/radius, applied frame/scale, coordinate conventions and units.
- Per-pair evaluation/decision/response and pre/post state, skipped reason,
  evaluated-pair coverage, truncation and dropped records.
- Independent caller-provided input/config/resource identities and provider
  qualification reference. Provider state must remain distinct from predicted
  geometry and captured rendered skin.

A negative assertion requires the intended pair to have been evaluated in a
declared contiguous completed-step window without contact, no coverage gaps,
and a valid positive control. Disabled physics or an empty event ring does
not satisfy it. The reviewed stock public interface cannot supply this proof. Implement and
qualify the producer before marking capsule/contact testing available.
