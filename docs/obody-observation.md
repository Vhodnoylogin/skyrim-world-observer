# Read-only OBody observations for SE

The SE collector requests the official OBody plugin API v1 once at SKSE
`PostPostLoad`. Its process-lifetime readiness and actor listeners collect native
`OnActorGenerated` and `OnActorPresetChangedWithoutGeneration` callbacks. Both
return `Response::None` and leave the payload unchanged. They never generate an
actor, assign a preset, call a host request/claim function, or schedule those
actions. Registration is retained across provider readiness transitions.

An optional snapshot member selects copied receipts and current assignments:

```json
{"kind":"world_observer","obody":{"refs":["0x14"],"afterSequence":0,"maximumEvents":64}}
```

`refs` is optional; when supplied it contains 1..16 valid FormIDs. It filters
actor/sender receipts and selects actors for current assignment reads. Lifecycle
receipts and events without a sender remain visible so epoch boundaries are not
hidden. Omitting `refs` reads receipts without querying all actors. The response
is non-consuming. `afterSequence` is a nonnegative uint64 cursor;
`maximumEvents` is 1..256, default256. Unknown options are rejected. The outer
request keeps the existing task deadline, session and world-load guards.

The current assignment comes from `GetPresetAssignedToActor` for each explicit
reference, after native readiness permits access, in the same SKSE main-thread
task as the other requested observations. The provider's borrowed preset name
is copied immediately. An empty successful result means no assigned preset;
it is separate from an unavailable getter. No morph value, including zero,
is used to infer presence or absence. The assignment is sampled provider state;
it does not establish effective SKEE coefficients, physical effects, or rendered
appearance. Missing actors and unready/absent providers have explicit reasons.
Readiness is callback-derived: this API has no independent `IsReady` getter.

## Meaning and bounds

- Native `responsiblePluginInterface == nullptr` identifies OBody's internal
  stock path. The special owner `_Papyrus` identifies its Papyrus path; a
  nonnull responsible interface with another or missing owner is external.
  Only an internal native **OnActorGenerated** receipt sets
  `stockInternalGeneration:true`.
- `OnActorGenerated` means the provider has applied **or queued** the morphs.
  It does not prove application or rendering finished. The preset-changed
  callback has a raw flag identifying unassignment; its text can name the
  previous preset. Keep the callback name, flags and payload together.
- The passive SKSE ModCallback sink accepts only
  `CLAWDistribution_OBodySourceCandidate`,
  `CLAWDistribution_OBodySourceReady`,
  `CLAWDistribution_OBodySourceAvailable`, and `Obody_ApplyMorph`.
  These names were found in actual provider/subject source. It copies their
  text, numeric argument and sender FormID. ModCallback has no authenticated
  plugin identity: another plugin can emit the same name. Candidate/Ready are
  receipts, not evidence of the host's internal Request/Claim completion or
  resulting GenActor call. It makes no consuming diagnostic call.
- A mutex protects one256-entry ring shared by all callback channels. Sequence
  numbers never reset during the process. Each receipt has its copied world
  generation, provider readiness epoch, readiness/loading state and monotonic
  receipt time. Readiness transitions invalidate the prior provider epoch;
  they do not fabricate a stock generation. World boundaries are retained as
  events instead of erasing old receipts. Consumers must compare each event's
  generation against the outer snapshot's generation.
  World generation is explicitly the observer's last observed lifecycle state
  at receipt: SKSE can dispatch OBody's handler before the observer's handler
  for the same load message. Provider callbacks are not synchronized to the
  observer's SKSE-message delivery order. Provider readiness epochs remain the
  native source for provider continuity.
- Retention reports oldest/latest sequence, overwrite count, callback failures,
  truncation count, missing global range before retained data, pagination and
  a continuation cursor. Gaps are global before actor filtering; they cannot
  establish that a particular actor's event was lost or absent. On a fully
  drained filtered page, the cursor advances through the global watermark.
- Borrowed preset/event text is copied to at most512bytes and an owner to
  128bytes. Oversize input never causes an oversize owning allocation.
  Truncation and original byte count are explicit. C-string owner length is
  a lower bound if truncated. Invalid UTF-8 uses the outer SE serializer's
  disclosed replacement behavior. Nonfinite numeric ModCallback arguments
  become null with `numArgFinite:false`.
- No raw pointer, arbitrary event hook, private provider memory traversal,
  renderer completion, or historical coverage before listener registration
  is exposed. Callback order is receipt order under the ring lock, not a
  global physics/render phase. No receipt is not proof of no event.

There is no provider-wide revision/lock protocol. Current assignment and
callback snapshots can advance independently. The pinned source's readiness
notifications are dispatched from SKSE lifecycle messages; current assignment
reads run only through the existing SKSE main-thread observation task, check native readiness, copy the
view immediately, and reject changed readiness epochs. They never hold the ring
lock while calling OBody. A separate API-use gate covers each getter and its
immediate view copy; readiness invalidation takes the same gate before returning
from the provider's becoming-unready callback, while the pinned source still
permits API use. The getter does not dispatch readiness or actor callbacks.
This protects the borrowed view during that read; it does not freeze other
assignment changes. This is a sampled main-thread contract, not an atomic
cross-provider snapshot or independently verified loaded-binary source match.
The outer task's world/deadline guard is checked before each assignment, after
acquiring the API-use gate, and immediately after copying the getter result.
Once it fails, no further provider getter is called for that request; remaining
assignments are unavailable. A getter that has already started cannot be
interrupted safely.

## Official source and acquisition

Public source: [Aietos/OBody-NG](https://github.com/Aietos/OBody-NG), commit
`13513609145313f721970c2ebe113290f6add6e8`. The official header is
[`include/API/API.h`](https://github.com/Aietos/OBody-NG/blob/13513609145313f721970c2ebe113290f6add6e8/include/API/API.h),
Raw LF download SHA256
`f04945202d9daa6408d7555e46eb2a553a1041ab5d84a8f4efc288168563ea18`.
The historical Windows checkout hash is
`42a899e88ba53bcc673d2d8e9513e839f0d661ee1e002514fe3551827a98681d`;
it differs only by1419 CRLF line endings. These are distinct byte identities.
The build acquires/verifies the unmodified raw LF header in its external dependency area
as `OBodyAPI.h`; no downloaded header, source checkout or binary is vendored.
The API header includes its upstream GPLv3 license text. Preserve that notice
with the external acquisition and account for the selected dependency license
before any later binary distribution. This implementation task publishes no
third-party package or observer binary.

The callback responsibilities and borrowed-string lifetimes are documented in
that header. Source cross-checks: `src/main.cpp` handles interface acquisition
and readiness; `src/API/PluginInterface.cpp` implements the read-only assignment
getter; `src/Body/Body.cpp` emits native callbacks and `Obody_ApplyMorph`.
CLAW's selected event names are in `distribution/src/Plugin.cpp`; its optional
`distribution/obody/CLAWOBodySourceHost.psc` owns the consuming request/claim path.

`tests/obody_observation.cpp` exercises copied lifetime, exact responsibility,
unassignment, text/retention bounds, explicit gaps, multiple epochs, filters,
pagination, malformed queries, nonfinite arguments and concurrent receipt order
without a game or provider. Native compilation and offline tests do not qualify
the actual loaded provider, callback timing or in-game results; those remain
separate later validation.
