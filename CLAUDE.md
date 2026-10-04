# Skyrim World Observer: development contract

Read README.md and docs/contract.md before edits. This repository contains only
our observer and client. Fetch dependencies into an external build/cache directory
using pinned dependencies.json; never commit external programs, SDKs, downloaded
headers, game data, credentials, logs or builds.

Do not add a second HTTP server, game mutation, controller injection or a
test-aware mode to target mods. Use existing DevBench tools/events ABI. Engine
reads run in a bounded SKSE task; never dereference world pointers on its listener
thread. Keep timeout cancellation and load-generation checks. Unsupported physics
or render phases return unavailable, not invented zero values or atomicity.

Work on main. Verify/name the Git remote before committing/pushing. Public
issues and releases require the owner's task authorization. Do not deploy to a
personal play profile; use isolated test sessions and restore the environment.
