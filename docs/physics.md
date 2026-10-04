# Physics observation (0.2.0)

```json
{"kind":"world_observer","physics":{"refs":["0xFF000001"],"afterSequence":0}}
```

Use actual loaded reference IDs from the fixture. Up to 16 refs must belong to
one physics world. Their third-person scene trees are traversed collectively
up to 128 nodes and 64 distinct `bhkRigidBody` parts. Missing bodies, nonfinite
state, exhausted capacity and busy world locks are explicit unavailable/partial
results. `truncated=true` prohibits claims that all parts were observed.
Per-ref diagnostics show loaded3D, visited nodes, collision objects and the
exact missing/unsupported collision-wrapper/body stage. Traversal reads the
pinned NiAVObject collisionObject member and uses checked engine C++ RTTI casts,
matching HIGGS's VR traversal. It does not assume virtual AsBhk cast helpers are
implemented for every VR wrapper.
The scene root is the typed loadedData.data3D pointer where present (both
loadedData and its node are at independently asserted offset 0x68 in SKSEVR 2.0.12
and pinned CommonLib 3.7). Diagnostics compare this against the default and
explicit third-person virtual getters. Without loaded data, the explicit
third-person getter is a fallback. Missing/deleted references are refused.
Character proxies/phantoms and constraints are not rigid bodies and unsupported.

`physics.bodies` returns source FormID/node, process/generation-local worldID and
body UID, motion type, inverse mass, active-island status, center of mass and
linear/angular velocity. Position/linear velocity use **raw Havok world units**,
not scene units or controller metres. Angular velocity is radians/second. Zero
inverse mass does not mean zero kilograms; fixed/keyframed bodies need interpretation
using motionType. Active describes the simulation island, not a contact count.

The first request registers one passive world contact listener and arms observation
of the selected body UIDs for 5 seconds. Repeated requests renew the lease. Changed
body selection, expired lease or world-load generation starts a new subscription
epoch. Observations cannot include contacts preceding arming. Listener objects
have process lifetime, capped at 32 worlds; the engine owns only pointers in its own
world listener array. Deleted worlds discard their arrays. No saved world pointer
is ever dereferenced and no callbacks access references/scene trees. A process
restart resets lifetime capacity and all world/body identities.

`physics.contacts` contains **actual Havok contact-point callback data**, not
bounding-box proximity or a collision-object-present flag: body A/B UIDs,
position, separating normal B->A, signed separation, callback type and monotonic
time/sequence. Separating velocity is null if Havok did not supply it. Disabled
and new-contact flags are raw values **at this listener's callback position**;
other mods/listeners can modify them subsequently. Positive separation is marked
speculative. `solverUsed`/`potential` are unavailable and
`finalSolverContactProven=false`: the observer does not infer solver acceptance
from touching geometry. It never changes delays, filters, contact flags, impulses
or any target mod's callbacks.

Each listener keeps 256 events. `afterSequence` filters the current listener's
sequence; maintain continuity using sessionId, loadGeneration, worldId and
subscriptionEpoch. Discard a previous cursor after any boundary. A cursor ahead
of the producer or overwritten entries are explicitly flagged. `callbackBusyDrops`
counts conservative process-lifetime callback drops when the ring mutex is busy.
The callback uses try-lock only; no waiting, game locks, heap allocation, JSON,
network calls, FormID resolution or retained engine pointers occur there.

Body state is copied in one world read lock, contact records asynchronously at
`havok_contact_point_callback`. It is not a whole-world, post-step or render
snapshot. Engine callback delays, disabled pairs and sleeping bodies may suppress
callbacks. **Empty events never prove absence of collisions.** A polling test can
assert an observed contact, motion, rest or release under declared tolerances;
negative-contact assertions need a future complete post-step/manifold provider.

The collector calls the native nonblocking world read/write try-lock and returns
busy rather than sleeping. The native read try-lock may retry a contended CAS
until it succeeds or sees a writer; this is not a guaranteed single CPU operation.
Four exact loaded-function SHA256 fingerprints fail closed on altered code, then
private 8-byte lock checks verify read/write release and reentrant ownership. No
custom lock protocol is used. Registration can allocate
one engine listener-array slot; normal requests do not allocate engine storage.

## ABI sources and licensing

Builds use MIT CommonLibSSE-NG 3.7.0 at pinned commit
`c4ab853d095e81e3390b282d7ba01ab2f24ebf25`, including hkpWorld/contact listener,
contact point/event, rigid-body/motion and BSAtomic types. External files remain
outside this repository. Header declarations and HIGGS's established VR usage
agree on bhkWorld worldLock offset 0xC598 and body callback structures.

- [Pinned lock declaration](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/c4ab853d095e81e3390b282d7ba01ab2f24ebf25/include/RE/B/BSAtomic.h)
- [Pinned contact event](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/c4ab853d095e81e3390b282d7ba01ab2f24ebf25/include/RE/H/hkpContactPointEvent.h)
- [CommonLibVR material layout reference](https://github.com/alandtse/CommonLibVR/blob/ng/include/RE/H/hkContactPointMaterial.h)
- [CommonLibVR contact properties layout](https://github.com/alandtse/CommonLibVR/blob/ng/include/RE/H/hkpContactPointProperties.h)

Properties flags are read at offset 0x13: solver results 8 bytes plus material
flags at0x0B. Those newer reference headers are **not vendored or compiled**;
the original minimal ABI read is our code. No newer GPL CommonLib library/source
is linked into the MIT build. Unverified helper meanings (wasUsed/isPotential)
are not guessed or reported as facts.

Independent loaded-code audit, 2026-10-04, confirmed writer ownership needs
count `0x80000001`. An initial unreleased custom-CAS prototype used only the
writer flag; review rejected it before any physics query. The collector now
uses engine TryLockRead/Write at RVA 0xC42350/0xC423C0 and UnlockRead/Write at
0xC42410/0xC42420, preserving the engine's memory fences. Qualification code
rejects the original defective writer-count protocol in the native contract test.
No executable bytes are distributed in this repository.

Audit baseline: SkyrimVR.exe SHA256
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`;
VR Address Library SHA256
`93c3bb9cf7ddc950a13f1ac400b80f4e22c8b3f2c5eab04d8a855cb79e1c6a9c`.
Body/contact live qualification remains distinct from this ABI review.
