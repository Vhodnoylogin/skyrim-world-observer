# Skyrim World Observer

A read-only Skyrim VR observer for automated tests. One DevBench request copies
selected reference and skeleton-node state in one SKSE main-thread task. The
result identifies the session, world-load generation, sample, producing frame,
phase and units. Missing objects/nodes are explicit unavailable results.

This is an optional automation plugin, not a controller driver or game launcher.
Mods under test receive ordinary input and require no changes.

## Requirements

- Skyrim VR 1.4.15 and SKSEVR 2.0.12; this plugin refuses other game runtimes.
- DevBench >=1.5.0 for extension registration; tested host baseline is 1.22.0.
- VR Address Library required by CommonLibSSE-NG; use the existing installed
  VR address database for this game version.
- Microsoft Visual Studio Desktop C++ tools, CMake >=3.24, Git and Python3.11+.

External dependencies are downloaded into the **external build directory**, never
vendored into this repository. Pins and build instructions are in
[dependencies.json](dependencies.json). Game saves, credentials, logs, traces,
downloaded sources and DLL/build outputs belong outside Git.

## Build

```powershell
python tools/build.py --build-dir D:/Builds/skyrim-world-observer
```

The build tool discovers Visual Studio's bundled CMake if needed. For a verified
existing CommonLibSSE-NG3.7.0 build, pass `--dependency-cache <external _deps>`;
its source commit is checked. Default clean builds fetch hash-checked archives
of the exact dependency commits, without copying them into this repository.
Use an empty build directory for changing the dependency mode. Build outputs are
in `<build-dir>/Release` and the installable DLL is `SkyrimWorldObserver.dll`.

Install the DLL as a **separate MO2 mod**, in `SKSE/Plugins/`. Enable it only in
the intended test profile. Do not overwrite other mods. It creates no persistent
game settings, saves or world objects. The optional console client writes only
to the explicitly requested external trace file.

## Query

Find the running host from DevBench runtime.json. No token or fixed port is
stored in this repository.

```json
{"kind":"world_observer","refs":["0x14"],"nodes":[
  {"ref":"0x14","name":"NPC R Hand [RHnd]","firstPerson":true},
  {"ref":"0x14","name":"NPC R Hand [RHnd]","firstPerson":false}
]}
```

POST this to `/api/tool/inspect`; use `action:"capabilities"` for discovery.
Alternatively `python tools/observe.py --runtime <runtime.json> --query examples/player.json`
prints one snapshot. Add `--output <outside-repo.jsonl> --duration 5 --interval .2`
for a bounded trace. Recording never overwrites a file, reports missed scheduling
intervals and refuses mixed game sessions. It is sampled telemetry, not proof of
unobserved transitions between samples.

## Contract and limits

[docs/contract.md](docs/contract.md) describes bounds and quality. Reads are
consistent within one main-thread task; **physics and renderer are not frozen or
synchronized**. Raw addresses are not exported. Scene transforms are engine units
and must not be confused with OpenVR metres. Physics velocities/contacts and
render pixels are explicitly unavailable in0.1.0, pending safe phase-specific
collectors. No synthetic interaction or arbitrary script/memory access exists.

Queued requests are abandoned after their deadline. An already-started read may
finish after the client times out; its outcome is not reported as success. Loading
interrupts generation continuity. World-load messages are emitted through the
existing DevBench event bus, whose retention is managed by the host.

Our source is MIT licensed. Dependencies keep their respective upstream licenses.
This repository publishes source, not a redistributed third-party package.
Before distributing a compiled DLL, include notices for pinned CommonLibSSE-NG,
fmt, spdlog, nlohmann/json, rapidcsv and the MIT DevBench integration glue. Rapidcsv
uses BSD-3-Clause; the other listed pins use MIT/MIT-style notices.

## Verification

`ctest --test-dir <build-dir> -C Release --output-on-failure` checks request bounds,
invalid identifiers, timeout cancellation, stale generations and transform data.
`python -m unittest discover -s tests -p "test_*.py"` checks trace safety and loss
accounting. Compilation does not replace the separate live qualification reported
in the project journal; see [docs/validation.md](docs/validation.md).
