# Miles Together

**Research in progress. This is not a playable co-op mod or a published release.**

The requested mod adds online two-player co-op to the player's installed PC copy of
Marvel's Spider-Man: Miles Morales. Both players participate in the same campaign
missions and side activities, and retain completion and rewards in their own saves.
The target includes the campaign from beginning to end. Host-game integration,
shared combat and mission logic must work before anything is listed as playable.

The native observation tests pass: a hash-checked hook observes Miles's live actor
and movement, enumerates 290 populated component/alias entries, and queries an already
loaded actor asset on the game thread in the installed Steam build. A second playable actor, networking,
combat replication, synchronized mission state and safe progress merging are still
unimplemented. A successful observation is not a co-op gameplay test.

## Source of truth

The JSON sheets in `sheets/` record the design, compatibility, native hooks and every
touched gameplay system. Each declared row/column cell has an explicit verification
check. Unknown addresses and untested behavior deliberately fail preflight.

Before a mod build:

```powershell
python tools/preflight.py --report reports/preflight.json
```

The build must stop if this command fails. Run the build-gate tests with:

```powershell
python -m unittest discover -s tests -v
```

## Research tools

`tools/inspect_native.py` and `tools/inspect_rtti.py` locate bounded native-code
candidates in the player's executable. `tools/trace_player.py` verifies the game's
hash and hook bytes, temporarily attaches an observation-only Frida callback, and
detaches after a bounded run. These are developer tools, not an installer or a game.
`tools/inspect_assets.py` locates/extracts selected assets for private research.
`tools/read_config.py` reads selected config and actor component records without
modifying them. The generic asset parsers validate container and payload boundaries.
Private traces, game-derived disassembly/decompilation and captures belong outside
this project and must never be included in a release.

The tested executable is Steam version 4.630.0.0 (build 23986141), SHA-256
`0cfe5e529369bd3eac642b56ce9ad068b47bb1a294ac1ea1ef7ef4dcf22d3256`.
No compatibility with another build is claimed.

## Publication requirements

Before release: implement and test two independently controlled actors sharing
combat and objectives; verify every supported campaign/side-activity transition;
verify progress for two distinct saves; package a complete native loader/runtime;
validate Melty's install recipe and one-click check; test actual host/join with two
copies; capture this co-op version in the host game; pass Melty's release tests.
`melty.recipe.draft.json` records Melty's proposed launch/join contract. Its DLL and
ZIP package are unbuilt, and the recipe has not passed validation or one-click checks.
Creator approval is required for final publication. No packages or gameplay media
have been uploaded, and no Melty listing exists for this project.

Original code: MIT, with remixes allowed by the creator. See `LICENSE` and
`THIRD-PARTY-NOTICES.md`. The game and its content remain their owners' property
and are read from each player's own installed copy.
