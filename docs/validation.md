# Validation

Version 0.1.0, checked 2026-10-04 on Windows x64 / Skyrim VR 1.4.15 / SKSEVR 2.0.12 /
DevBench 1.22.0. MSVC 19.51 and Windows SDK 10.0.26100 compiled the plugin using
the pinned CommonLibSSE-NG3.7.0 external cache. Native CTest passed request bounds,
invalid forms, nonfinite data, queued cancellation, loading and stale-generation
checks. Three Python tests passed trace continuity, missed intervals, session
replacement, failure recording, file overwrite refusal and size bounds.

Default clean acquisition/build also passed without a prebuilt dependency cache:
all five dependencies fetched as hash-checked exact-commit archives and compiled.
An initial missing rapidcsv public include was corrected in our CMake integration.
After live qualification only the discovery regex was narrowed to match the
already enforced lowercase0x prefix; engine-read behavior was unchanged.

Live run `20261004-125015-729b5c` passed in an isolated copied MO2 profile, with
the project's independently qualified launcher/guardian. The own DLL was staged
temporarily, backed up before staging and removed during restoration. All739
recorded restoration paths were independently verified without mismatches;
source fixture saves were preserved. Raw logs, private fixture and trace stay
outside Git. No observer is permanently enabled in the play profile.

The live qualification verified:

- DevBench extension discovery and read-only/domain capabilities.
- Player reference identity, loaded3D and first/third-person right-hand transforms
  returned together in one main-thread task with frame/sample/generation metadata.
- A physical emulated-controller10cm movement produced approximately7.000
  Skyrim engine units of displacement in both observed hand trees.
- Missing reference/node yielded partial/unavailable rather than false zero data.
- Invalid timeouts99,3001 and4294968796 were rejected; subsequent reads worked.
- A3-second trace produced15 increasing samples and an explicit end record.
- A second save load changed generation1->2 within the same observer session,
  and emitted pre/post-load events with loadSucceeded=true.

Two earlier attempts are retained as failures: the first was refused at plugin
query because SKSEVR reports1.4.15.1 rather than CommonLib's1.4.15.0 constant;
the query now accepts these two encodings of the supported runtime. The second
registered successfully but the larger HIGGS scenario timed out on a Papyrus
GetGrabbedObject call. The final observer run uses a pose-only fixture and does
not claim to have repeated grip/release qualification in that run. Earlier
launcher/physical-grip qualification is separate evidence.

No live induced failed-save-load, deadline race or queue-saturation experiment was
performed. Failed-load handling follows SKSEVR's documented success-pointer ABI;
deadline/cancellation conditions are covered by native contract checks and review.
This is one qualified environment, not a clean-machine runtime certification.
These 0.1.0 runs did not include physics body/contact observation. Render capture
and continuous observation remain unavailable. The 0.2.0 evidence below has a
separate scope.

## Subsequent full integration qualification

Final run `20261004-131617-d656c3` passed with the **default clean-build DLL**
SHA256 `e4b7959d5c53f7bf8073ecafac74c297263899e6b6e1e68d8cf3ed633c7cca32`.
It combined physical motion in both hand trees, exact-reference HIGGS grab,
one-second continuous hold, release/world presence, then all six observer checks
and a second save load. All739 restoration paths were independently verified with
zero mismatches; original saves matched their pins and the temporary DLL was removed.

Additional attempts exposed a runner readiness gap: a startup MessageBoxMenu could
arrive after the first ready check and pause the world. The runner now rechecks and
closes only the already authorized one-button Speech Broker startup message before
hand sampling and the grip edge; unknown prompts fail. Both-hand movement is awaited
within a bound. Two safety regressions check known-message exact-body acceptance
and refusal of unknown/multiple-choice prompts; all15 runner checks passed.
The final combined run observed and cleared that recognized message.

The observer movement fixture returns the hand to a neutral unconstrained pose
before its additional10cm movement assertion. An intermediate combined attempt
passed grip/release but failed the observer movement assertion while still near
the floor; it remains a recorded failure, not a pass obtained by weakening the
movement threshold. A control full run without the observer also passed. Review
found no observer VR subscription/hook or engine reads before its first snapshot;
equal shifts in existing SKSE plugin handles preserve VR callback order. Startup
timing can still change when another DLL is loaded.

## Version 0.2.0 physics qualification

The pinned external build passed the native contract test and three Python trace
tests. Native coverage includes request bounds, subscription lease/epoch changes,
stale-generation events, ring gaps and rejection of a defective writer-lock
protocol. The collector uses qualified native try-lock functions, not that
rejected prototype; [physics.md](physics.md) records the ABI scope.

Live run `20261004-231616-118ba4` used DLL SHA256
`6d0302e082af7ef2d3fd8f2f019794ff44019e893423f44023fc5f3d289cde28`
in the same qualified Skyrim VR / SKSEVR / DevBench environment. It passed all
14 physical fixture checks, physics capability discovery and selected rigid-body
observation. The surviving firewood reference was loaded and neither deleted nor
disabled. Its one selected body had UID 281, box-inertia motion type 3 and inverse
mass approximately 0.06666667. The collector returned finite center-of-mass,
linear/angular velocity and active-island state with no traversal truncation.

Subsequent queries recorded actual Havok contact-point callbacks, with producer
sequence counts 21, 129 and 237, no reported callback contention drops and no ring
gap in those samples. These observations establish passive contact collection
for this fixture. They do not establish final solver acceptance, complete
manifolds, absence of other contacts or uninterrupted observation.

The run remained **failed**: an additional required upward-velocity assertion
after a Papyrus `ApplyHavokImpulse` call timed out. DevBench reported call
completion, but the selected body retained resting velocities. A second run,
`20261004-231935-bab45d`, repeated the stimulus with explicit decimal JSON
arguments and also failed that assertion. These results do not establish a
numeric-marshalling defect or qualify the impulse as a working stimulus.
Both runs independently restored all 739 recorded paths without mismatches.

Run `20261004-232311-1e8ba5` used a different fixture stimulus: the documented
three-Float `ObjectReference.SetPosition` call raised the live object and gravity
produced measured downward motion. The same FormID acquired a new body UID
(281 to 283), correctly starting subscription epoch 2 after epoch 1. Its Havok
center-of-mass Z rose from approximately 99.754257 to 102.929886, then fell to
102.516899. Downward linear velocity samples were approximately -0.573766,
-2.837870 and -3.670606 Havok world units per second. This establishes body
motion observation and replacement of the watched body without treating an
old UID as the new body.

The new subscription collected 20 callbacks, sequences 13 through 32. Sequence
13 had positive separation approximately 0.097357 and was correctly marked
speculative. Sequence 16, bodies 283/258, had signed separation approximately
-0.017813, a mostly upward normal, separating velocity approximately -7.56735,
disabled=false and speculative=false. This is observed touching/penetrating
geometry at the contact callback; it still does not prove final solver acceptance
after subsequent listeners or a post-step manifold.

That complete scenario also remained **failed**: after an intentional second
save load, a read-only observer request was abandoned before starting during
loading. Its positive gravity/contact evidence is retained, but no full pass is
claimed. The independently verified restoration again covered 739 paths with
zero mismatches.

Earlier zero-body attempts observed a fixture that our executor had disabled and
deleted before its continuation steps. Diagnostics correctly showed absent 3D
and the deleted reference. Retaining the fixture until the owned game closes
resolved that test-lifetime error; those failures are not attributed to another
mod, VR casts or scene getters. The shared typed scene root and checked engine
RTTI traversal are consistency measures whose live positive scope is the
surviving reference above.

The preceding partial results do not establish a full pass. The following final
run supplies that separate evidence.

### Final combined physics pass

Run `20261004-232801-17bda0` passed with the unchanged DLL SHA256 above and the
independent executor package's extraction 11. Its 24 recorded steps comprised
21 passing assertions and three explicit observations, with no failed checks.
The scenario combined physical emulated-controller motion, exact-reference
HIGGS grab/hold/release, selected rigid-body observation, a raised-object gravity
stimulus, contact observation and a second save load. After that load, generation
2 reported the player as available. The executor permits bounded retry of an
explicitly read-only polling request abandoned during loading; it does not retry
mutations or accept a response after its deadline.

The selected body changed UID 281 to 283 and subscription epoch 1 to 2 after the
fixture was raised. Havok center-of-mass Z changed from approximately 99.754234
to 102.929619 and downward linear velocity reached approximately -2.556703 Havok
world units per second. The new subscription collected 22 callbacks, including
five with nonpositive separation, disabled=false and speculative=false. The
first such touching sample was sequence 15, bodies 283/258, with signed
separation approximately -0.0194032 and separating velocity approximately
-7.5555. Positive-distance speculative samples remain separately labelled.
No contact is promoted to a final-solver or complete-manifold assertion.

Immediate independent verification found zero mismatches across all 739
recorded restoration paths and the private fixture pins. The temporary plugin
was removed during restoration. Separate final-executor guardian experiment
`20261004-233010-420ef0` intentionally terminated the runner after readiness.
Its independent guardian recovered all 739 paths, including the staged observer;
immediate independent comparison found no mismatches and preserved save pins.
The expected failed scenario is recovery qualification, not another physics pass.

This qualifies selected rigid-body state, body replacement and passive contact
callbacks in one tested environment. Multi-part ragdolls, multiple worlds, live
ring overflow/load churn, character proxies, negative-contact completeness and
render capture remain outside this live qualification. Raw run logs, builds and
fixture saves stay outside Git.
