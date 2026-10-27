# Independent SE SKEE observations

The optional `skee` snapshot request reads actual provider-owned BodyMorph state
inside the observer's existing SKSE main-thread task. It does not call the subject
mod or use its cache. It calls only `HasBodyMorphName`, `GetBodyMorphs` and
`VisitKeys`; there are no setters, evaluation/application calls or Revert calls.

```json
{"kind":"world_observer","skee":{"refs":["0x14"],"morphs":["Breasts","WolfMorph"]}}
```

Each requested actor receives explicit provider availability and interface
version, direct provider aggregate, separate morph-name presence and copied
per-key values. A present zero-valued name and key remain present zeros. An absent
name can also have a provider aggregate of zero; the Boolean remains separate.
Key values are diagnostic observations and are never summed to obtain an
aggregate. Provider state is not evidence of rendered vertex application.

Each getter/visitor has its own monotonic start/end time. The result explicitly
reports `sequential_provider_reads`, `atomic:false` and no provider data revision.
One main-thread task does not freeze writes from other mods or turn the separate
getter locks into an atomic snapshot. Before/after actor handle, FormID, base,
race and loaded root checks protect this actor incarnation; the caller also
supplies the enclosing observer world-generation guard. The old loaded root is
retained during the calls to avoid freed-address reuse. A stale actor/root/world
or changed negotiated interface discards all values from that actor sample.
The guard is checked between native calls so an expired request or stale
identity does not start the next getter. The old root is retained only for this
sample, with no persistent pointer registry in the SKEE collector.

## Official dependency and acquisition

The official external header is
[`skee64/IPluginInterface.h`](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/IPluginInterface.h)
from `expired6978/SKSE64Plugins`, commit
`9ebcb733e17be695f994cd2e9cc383043446bc02`, SHA256
`875410dd22230f76a014a6841004e7d346f5fa1c748cfff695f9f495234da277`.
The build recipe stages and verifies it in the external build directory. The
header is not copied into this repository and its own declarations are used
directly; `ReadProvider` is our small test adapter, not a copied native ABI.
Upstream code retains its license; a header pin does not identify the loaded
provider DLL. `loadedBinaryIdentityVerified:false` discloses that distinction.

The observer sends the official `InterfaceExchangeMessage` (`0x9E3779B9`) through
SKSE messaging to target `skee`, with a real structure and `sizeof(structure)`
(eight bytes on x64). The synchronous exchange fills its borrowed interface-map
pointer; the observer queries the exact name `BodyMorph` and checks GetVersion
before downcasting. Only interface version5 is admitted by this implementation;
missing, older and unknown future versions return unavailable before reads.

[`main.cpp`](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/main.cpp)
defines the map and BodyMorph object as process-lifetime globals, populates the
map during plugin load, and normally registers its exchange listener during
PostLoad. Negotiate at PostLoad, then retry at PostPostLoad to remove listener
ordering dependence. Keep only the borrowed interface, never free it, remove it
from the map or call Revert. World loads change provider data, not its pointer
lifetime. `interfaceEpoch` tracks only observer-negotiated interface identity;
it is not a data revision or proof of provider reset/readiness.

## Bounds and refusal

Requests contain1..32 explicit morph names per actor, each1..128 bytes with no
embedded NUL. The outer SE request limits actor references to16. Collection
retains at most64 keys per morph and256 total key/value pairs per actor. Native
key strings are scanned for at most129 bytes; null, empty or longer keys are
reported invalid rather than silently shortened. Callback counts saturate at
uint64 maximum. Zero values are not filtered. The callback never calls back into
SKEE while the upstream visitor holds its provider lock.

Non-finite aggregates/keys are null with unavailable status. Overflow, invalid
key or capture-allocation failure makes the key collection, row and actor result
unavailable. Any retained prefix is diagnostic only and has an explicit count
and truncation flag. Separate finite fields can remain available individually;
an unavailable row must not pass a complete-observation gate. No key collection
is synthesized as an empty available map when it has overflowed.

The official `MorphKeyVisitor::Visit` returns void and cannot cancel traversal.
The provider's aggregate implementation can also iterate every stored key.
Consequently copied output, string scans and per-callback work are bounded, but
provider traversal and a single native call's elapsed time have no hard bound.
`providerTraversalBounded:false` is reported explicitly. The observer does not
throw through the foreign visitor ABI or invent a hard execution deadline.
Its outer request deadline/cancellation rules abandon queued work and reject
late completed results; they cannot interrupt an already executing foreign
native call. This limitation requires separate
live qualification before use with unusually large provider collections.

## Offline verification and integration

Compile `src/SkeeObservation.cpp` with `OBSERVER_WITH_SKEE_RUNTIME=1` only for the
SE plugin, using the verified external header include directory and existing
modern engine SDK. Call `NegotiateProvider()` at PostLoad/PostPostLoad and expose
`ProviderStatus()` in capabilities. Inside the guarded main-thread snapshot,
call `Observe(actor,morphNames,stillCurrent)` after resolving an actual actor;
retain the outer observer actor/session/generation/root identity envelope.

The standalone `tests/skee_observation.cpp` links the same `.cpp` without that
definition and requires only nlohmann/json. It checks present-zero versus absent,
direct aggregate provenance, exact read-only calls, unavailable/unknown providers,
stale world/root and interface guards, NaN/Inf, string/cardinality limits and
actor-wide exhaustion. Tests do not load a game, provider DLL or subject mod.
They cannot establish installed-provider ABI compatibility or live ordering.
