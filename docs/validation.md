# Validation

Version0.1.0, checked 2026-10-04 on Windows x64 / Skyrim VR1.4.15 / SKSEVR2.0.12 /
DevBench1.22.0. MSVC19.51 and Windows SDK10.0.26100 compiled the plugin using
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
Physics velocities/contacts, render capture and continuous observation are absent.

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
