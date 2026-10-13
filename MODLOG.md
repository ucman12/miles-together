# Miles Morales co-op

## Authorized goal and design intake

- Required primary host: Marvel's Spider-Man: Miles Morales, Steam app 1817190.
- Creator requests online two-player co-op, end to end, with all activities accessible simultaneously to both players.
- Preserve the chosen game and existing window arrangement. Do not substitute another Spider-Man game, another host, a browser demo, or independent solo sessions.
- Creator chose shared missions: both players participate in the same objectives and campaign. Separate simultaneous mission instances are out of scope.
- Design interview finished in three questions. Creator accepted both players retaining their own completion/rewards, MIT for original code, and remixes allowed. Character default: both use Miles's existing abilities, with distinct existing suits.
- No playable build, networking implementation, release, gameplay evidence, or publication exists yet.

## Recon completed 2026-10-12

- Install: `D:\SteamLibrary\steamapps\common\Marvel's Spider-Man Miles Morales`.
- Host executable: `MilesMorales.exe`, x64 native. Steam build ID: 23986141.
- Asset archives: `asset_archive/toc` and archive spans. Game files must not be redistributed.
- Save/config location reported by toolkit: `%USERPROFILE%\OneDrive\Documents\Marvel's Spider-Man Miles Morales`. Back up before modifications or modded launches.
- universal-modder cloned to `%USERPROFILE%\.codex\toolkits\universal-modder`. Read `skills/mod-any-game/SKILL.md` and native-engine reference; Windows entrypoint `bin/um.cmd` works.
- `um kb search` has no Miles Morales or Insomniac field note. `um scan` reports unknown native engine, no installed mod loader, and suggests proxy DLL plus native function hooks. This scan is not proof that co-op is possible or impossible.
- Melty host slug: `custom-marvels-spider-man-miles-morales`. `game_info` provides no automatic loader configuration or camera/session API. No account risk is flagged.
- `list_my_mods`: only unrelated Bullworth Beatdown live listing. Do not reuse it.
- `search_mashups`: no live co-op match. Closest general example Spider-Peak uses PEAK as primary and Miles Morales as companion, so its loader does not apply to this project.
- Read Spider-Peak's `mashup_info` for packaging example only; no remix or game swap requested.
- Overstrike upstream `Overstrike/Games/GameMM.cs` supports Miles Morales asset/suit formats, but declares `HasScriptsSettingsSection => false`. Its other-game script support is not evidence of a Miles Morales gameplay SDK.
- Upstream `norfmare/miles-morales-first-person-camera` documents an MIT native `version.dll` camera/body hook for a specific Steam executable hash. This establishes a native hook route, not an actor-spawn or multiplayer API. No code has been imported yet.
- hbgda upstream profile identifies the Spider-Man Multiplayer project under Spider-Man Remastered. Do not assume its binaries, hooks or license can be used for Miles Morales.

## Next step

Investigate entity spawning/control, simulation updates, damage/enemies, mission state/cutscenes and saves before promising full campaign support. New native networking and replication remain unverified. Use JSON sheets as source of truth and preflight every cell/reference before builds. Melty multiplayer join setup is launch integration, not game-state replication; never fake joining or gameplay test logs.

## Native observation work

- Installed Frida 17.23.3 in an isolated research virtualenv at `%USERPROFILE%\.codex\toolkits\mm-research-env` (system Python 3.10 with pefile and capstone). Set up toolkit window capture; ffmpeg/gfxcapture and WinDrive are available.
- Save snapshot: `%USERPROFILE%\.universal-modder\backups\miles-morales-coop-saves\20261012-213849.zip` (18 files, 1.5 MB).
- Confirmed game SHA-256: `0cfe5e529369bd3eac642b56ce9ad068b47bb1a294ac1ea1ef7ef4dcf22d3256`, version 4.630.0.0.
- Launched Steam app 1817190. Game PID 16760, original fullscreen 2560x1440 arrangement preserved. Completed profile 1 was selected; continuing enters free roam near Central Park. Backups predate launch.
- Native observation script `tools/trace_player.py` verifies file hash, on-disk bytes and live bytes before attaching Frida. It passively observes FollowCameraMover commit at RVA 0x232BCB0 and resolves the camera's current actor. It detaches after a bounded run. No camera, input, actor or save edits are made by the observer.
- First trace: 2511 callbacks, 38 local-actor samples, 0 errors, one actor. Movement trace: 5426 callbacks, 83 samples, 0 errors. Same actor moved from approximately (-7.207, 7.442, -1760.609) to (-13.198, 0.850, -1760.589) during short jump/walk input. Live local-actor observation is verified, not multiplayer.
- Research outputs and screenshots reside outside project at `%USERPROFILE%\.codex\research\miles-morales` and toolkit folder. Ordinary single-player screenshots are research evidence only; never upload them as co-op media.
- RTTI inspection located GameManagerMultiplayer descriptor RVA 0x75D8528, vtable RVA 0x3DE94B8 (42 methods) and HeroInputDriver vtable RVA 0x3E0AB18. No usable co-op entrypoints established by these names alone.
- Native strings include actors/GameManagerMultiplayer/GameManagerMultiplayer.actor and a multiplayer Miles model path. These may be legacy remnants; investigate their actual code/data rather than assuming they work.
- Downloaded official Ghidra 12.1.4 and Temurin JDK21 for bounded native analysis. JDK SHA256 validated before extraction. No mod DLL has been built or installed.

## Continued research after the interrupted turn

- Creator resumed with "ended by mistake. lets continue". Keep all prior design and permissions; no new interview is needed.
- Last lab launch PID 30576 was launched via Steam and closed after the probes finished. All Frida observation sessions detached after use. Re-discover the PID on later launches; never reuse observed heap pointers.
- Installed lz4 4.4.5 in the research virtualenv. Cloned official Tkachov/Overstrike and Tkachov/ALERT into the external toolkit folder. Downloaded official hashes.zip for asset-name lookup. These GPL development tools are not bundled in the mod project.
- Official Ghidra archive SHA256 verified: `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
- Ghidra project: `%USERPROFILE%\AppData\Local\MilesCoopResearch\ghidra-project\MilesCoop`. Ghidra refuses dot-prefixed ancestor paths for its project, so the project cannot live under `.codex`.
- Private decompilation folders: `decomp-manager`, `decomp-hero`, `decomp-spawn`, `decomp-hero-system`, `decomp-object-factory`, `decomp-client-registry`, under `%USERPROFILE%\AppData\Local\MilesCoopResearch`. They must never enter the project or release.
- `tools/prepare_analysis.py` now resolves chained PE unwind fragments. Without this, some apparent function bounds were only a prologue. Native exception metadata contains 388934 fragments, resolving to 256364 root extents; analysis is limited to selected method/function seeds, not whole-program autoanalysis.
- Verified component enumeration: camera target's +8 owner pointer is a GameObject; owner+0x80 is a 16-byte descriptor/component pair hash table, with ushort capacity 1024 at +0x88. Live enumeration found 290 populated entries/aliases. Descriptor name pointer +0x60 agrees with independent MSVC RTTI. HeroLocal appears under HeroCharacter, HeroLocal and HeroBase aliases.
- HeroRemote vtable: RVA 0x3E05E00; type descriptor 0x780D1E0; native constructor 0x9C5920. Finding it does not establish remote input, peer registration, damage or campaign behavior.
- HeroSystem singleton appears in native code at RVA 0x66FDDD0; vtable 0x3E05F68. Spawn candidate at 0x9C7DE0 requires a registered client/session record, HeroSpawnData, a loaded actor template, and substantial initialization. It is not a verified callable add-player API. Do not call it with guessed guest records.
- Native client lookup candidate 0x240B920 depends on a session component and a game client component's index; registry lookup 0x240A730 uses native session records. Guest identity/registration remains unresolved.
- Actor asset cache lookup 0x1AA7C80, context 0x6A49660, native ABI (pointer, uint64) -> pointer, exact bytes verified including a REX prefix. Live research lookup for actor ID `a896ee61f3c11fb8` returned a loaded resource on game thread 23988, with zero faults in 20 actor samples. This getter does not instantiate an actor.
- Actor instantiation-info candidate 0x1AA7550 builds an initialization record; object-create candidate 0x1E185B0 allocates a handle; initialize candidate 0x1AA63E0 has substantial component/lifetime work. These are statically examined, not runtime-verified creation/cleanup APIs.
- Selected actual asset definitions are present: GameManagerMultiplayer.actor (356 bytes), HeroCoopSpawnPoint.actor (356), characters/hero/hero_spiderman_milesmorales/hero_spiderman_milesmorales.actor (740), actors/hrm_bdy_firsthero/hero_spiderman.actor (12372), actors/hrm_bdy_firsthero/hero_milesmorales.actor (9076). Legacy multiplayer Miles model path from the executable is absent. Use existing player-owned assets rather than importing another game's model.
- Private read-only extractor validates the TOC's declared decoded length. Shipped TOC omits the zlib trailer; use streaming decompression and enforce exact declared length and DAT1 boundaries. DSAR extraction reads only intersecting LZ4 blocks.
- Actor wrapper is 0x7C207220, with DAT1 at +36. Actor component definitions tag 0x135832C8 contains 32-byte entries; component data blobs have their own boundaries/alignment. Treating actor metadata as a config section failed; changed to the documented actor layout, verified against upstream ALERT and two actual assets. `tools/read_config.py` now succeeds on the selected player and multiplayer-manager actors.
- Multiplayer-manager actor contains one default GameManagerMultiplayer component and no model. It does not supply a complete session setup by itself.
- Called Melty setup_multiplayer with an explicitly unimplemented draft and maxPlayers=2. It supplies a relay/join contract at `{localappdata}/MilesTogether` and a two-copy test plan. Saved it as `melty.recipe.draft.json`, explicitly unbuilt/unvalidated. Do not infer game networking from the valid contract. No host/join logs, two-copy gameplay, one-click result or Melty release test exist.
- Source is MIT and remix-enabled. Eight preflight gate tests pass. Production preflight still fails on unfinished gameplay cells. No compiler invocation, mod DLL installation, release upload, screenshot upload, create_mod, submit_release or publish has occurred.
- Latest production preflight: 6 sheets, 206 cells, 84 unfinished-cell/verification findings; saved to ignored `reports/preflight.json`. Eight gate tests passed. Asset/config readers succeeded on two selected actual actor definitions; observation/getter probes succeeded in the running host. These checks are not co-op gameplay tests.
- Game-folder write access confirmed using a unique temporary probe file, immediately removed. Network, game launch and game-window capture were also exercised. No sandbox/Windows permission prerequisite blocks the current work.
- Initialized a local Git repository in `E:\Melty`. No remote repository or automatic-publication configuration is attached.

## Remaining implementation blocker

There is no verified guest/session construction and remote controller binding yet. Native observations, asset parsing and symbol/vtable discoveries do not establish a second playable character. Next work must validate normal spawn/session registration with observed native inputs, then establish remote lifetime and control, before networking, enemy damage, mission/cutscene replication and idempotent progress merging. Preserve the requested full campaign/side-activity goal and both players' saves. Do not replace it with a visible ghost, separate solo play or another host game.
