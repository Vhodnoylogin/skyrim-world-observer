# CBPC provider observation: unavailable

`{"kind":"world_observer","cbpc":true}` in the SE candidate returns
`cbpc.status:"unavailable"`, three availability flags false, and snapshot
`quality:"partial"`. This is capability discovery, not a physics collector.
It must never be treated as a zero-contact result or an empty valid inventory.

## Actual SE provider audit, 6 November 2026

The inspected provider is the SE selection from Nexus mod21224, file814665,
CBPC1.7.3. Keep the original archive/FOMOD selection outside this repository;
reacquiring that label alone does not establish identical bytes. Exact inputs:

- `cbp.dll` SHA256 `4996c85ee00e051a903c15746e8686a2357b70c9b27957f8ef3026dbc62d5387`.
- `cbp.pdb` SHA256 `90c38d4868647f2b869b9cbc79410e36873229ffd394074dfe54fd9e45ecf2f2`.
- `CBPCPluginScript.psc` SHA256 `efda86786b650274dc135e106921891251c19059efdb475a8f7dbc2113fb7bf4`.

PE CodeView and PDB agree on GUID `1480B5DC-4631-4A03-8C34-8E4E2193691A`,
age1. This matches debug metadata to the binary, **not** a reproducible source
build or a safe public C++ interface. Its two PE exports are `SKSEPlugin_Load`
and `SKSEPlugin_Version`. The inspected load path registers an SKSE-only
message listener; its callback handles lifecycle messages3/6/8, with no
consumer snapshot interface. Actual native Papyrus registration matches the
installed script's13functions: only `GetVersion`, `GetVersionMinor` and
`GetVersionBeta` read state, and they read versions. The remaining functions
change physics/configuration/colliders. The three version getters return
the string parts `"1"`, `"6"`, `"2"` respectively. Preserve this reported1.6.2
separately from the package/logger1.7.3 label; `GetVersion()` alone returns`"1"`.

No supported actor/container/node/collider readback ABI was identified in
these inspected interfaces. PDB `SimObj`, `Thing`, sphere/capsule layouts and
internal update functions do not establish reader lifetime or synchronization
with every writer. They must not become a private-memory adapter. A matching
PDB, a function called `Render`, or an SKSE task is not a safe post-solver fence.

Two retained60-sample active/stopped/restarted diagnostics did not isolate
CBPC influence from animation. Stop/Start and a per-actor refresh receipt do
not prove that the intended node was bound or consumed. Observer's
`physics:not_sampled` and `render:not_synchronized` remain accurate. Adding a
consumer-side query cannot create the missing producer evidence; no empty
collector or availability upgrade is shipped on the strength of this audit.

For reproducible static inspection, acquire `pefile==2024.8.26`,
`capstone==5.0.7` in an external Python environment and Microsoft's Visual Studio
LLVM tools (`llvm-pdbutil`); retain the actual tool hashes. Inspect the pinned
PE export/debug directories, PDB summary and the binary's actual load,
message-listener and Papyrus-registration functions. Do not load the DLL or
attach to a game. All binaries, PDB dumps and generated disassembly stay
outside Git; none is a runtime observation.

## Missing producer boundary

The reviewed CBPC source is Acro748/CBPCSSE commit
`fabd56ce8f8ac10cde17325f0bd61ecc8cbcda20`, available from
`https://github.com/Acro748/CBPCSSE/tree/fabd56ce8f8ac10cde17325f0bd61ecc8cbcda20`.
Keep its checkout, build dependencies and DLLs outside this repository. The
inspected provider DLL above has not been matched to that source by a
reproducible build, so private offsets or hooks would have no qualified basis.

The public Papyrus interface can attach/detach capsules but cannot enumerate
the actual containers or establish a completed solver step. In that older source, with
`isAffectedNode=true`, capsules are in `Thing::thingCollisionCapsules`, in
addition to the collider role. Attach appends; detach removes matching indices.
Those implementation details do not prove the inspected binary's layout or
synchronization. An attachment return value, requested geometry, configured NIF/HKX capsules,
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
