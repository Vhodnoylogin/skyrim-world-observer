# SE independent observation candidate

This additive SE104 interface uses the same DevBench `inspect` extension and
existing bounded SKSE main-thread task. It changes no subject mod, input,
profile, save or world object. Discovery:

```json
{"kind":"world_observer","action":"capabilities"}
```

The new domains are source/build candidates. Compilation and synthetic tests
do not qualify a loaded provider, callback delivery, resource identity, game
compatibility or a subject mod. Preserve the earlier exact 0.3.0-se104 build's
separate reference/node qualification; do not transfer it to this DLL.

Example request (replace actual actor IDs and morph names for the fixture):

```json
{"kind":"world_observer","refs":["0x14"],"actors":true,
 "skee":{"refs":["0x14"],"morphs":["Breasts","Waist"]},
 "obody":{"refs":["0x14"],"afterSequence":0,"maximumEvents":128},
 "cbpc":true,"timeoutMs":1500}
```

`refs[].actor` records actual actor-base sex, current actor race, base race and
NPC weight. `rawNPCWeight` is the typed `TESNPC::weight` field;
`normalizedNPCWeight` is explicitly derived as raw/100, without clamping.
Nonfinite or out-of-range weight is unavailable. It does not prove the rendered
body's `_0`/`_1` mesh interpolation or actual loaded skin/armour resource paths.

`rootIdentity.sampledRootToken` is process-local, scoped by the envelope session
and load generation and by reference runtime handle/perspective. It changes
when an observed pointer/handle changes, when a null root is observed before
reloading, or after bounded identity-cache eviction. No pointer is serialized;
no root is retained between samples. Unobserved unload/reload with address
reuse cannot be detected, so this is not a continuous root-generation counter.
Always retain actor handle, session, world epoch, sample time and these limits.

`skee` is an independent direct provider read, never a CLAW cache. See
[the SKEE contract](skee-observation.md) for direct aggregate, presence and
zero-preserving keys, provider version checks, limits and non-atomicity.
`obody` exposes actual callback receipts and explicit assignment reads; see
[OBody](obody-observation.md). Assigned preset identity does not prove applied
or rendered morphs. Selected ModEvents have no authenticated plugin origin.
`cbpc:true` returns unavailable and partial quality. See
[the missing provider contract](cbpc-observation.md); no contact negative is
inferred from absence of records.

Request bounds remain16KiB, four pending tasks and100..3000ms. Additional bounds:
16 SKEE actors,32 morph names of128bytes,64 total actor/morph pairs; OBody
16 optional actor filters, maximum256 events. The complete serialized response
is capped at1MiB; overflow is an explicit rejection without a successful prefix.
Do not interpret available rows in an incomplete response as overall acceptance.
Provider getters cannot be forcibly interrupted: deadline checks prevent the
next call and reject a late result, but do not provide a hard provider CPU cap.

New/load world boundaries retain session+loadGeneration. Observe the actual
race, handle, sampled root and morphs before/after a normal transformation;
finite polls cannot prove an unobserved transition. For save/load, the ordinary
executor's explicitly configured owned-save route is distinct from the held
interactive companion, which currently forbids world-changing commands. This
observer adds no save/load escape. A future platform must use an explicit owned
transition, wait for verified gameplay readiness, invalidate all old handles,
tokens/cursors and fixture bindings, and re-identify the actor before continuing.

Offline checks exercise provider mocks, ring retention/cursors, text ownership,
zero versus missing, epoch guards, bounds and source-pinned acquisition. A future
Polygon-owned isolated qualification must exercise actual provider discovery,
present-zero and absent names, a real internal OBody generation and preset
change, real actor/race/weight/loaded-root observations, epoch transitions and
clean restoration. No launch authorization or queue/hold clearance follows
from building this candidate or reading these instructions.
