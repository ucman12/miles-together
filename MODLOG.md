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

## 2026-10-12 (Claude Code session): free-roam first version agreed, second Miles verified live

- Creator agreed to a smaller first playable: "free roam first". Two players, each in their own game and save, see each other's Miles in the open city. Shared enemies, missions and saves are later versions.
- Private work folder: `%USERPROFILE%\AppData\Local\MilesCoopResearch\work` (rx.py static helper, decomp.sh Ghidra wrapper, live.py + prelude.js Frida harness, boot.sh launch-to-free-roam, shot.sh, drive.sh). Screenshots in `...\MilesCoopResearch\shots`. None of it enters the project.
- VERIFIED LIVE (game thread = FollowCameraMover commit hook thread), all RVAs for the hashed Steam build:
  - Spawn primitive `0x24735F0 (uint64 actorAssetId, void* ctx=0, Mat4* transform) -> GameObject*`. It is what `PhotomodeSystem::SpawnDoppelganger` (handler `0xEA8830`) calls. Works for the loaded asset `actors/hrm_bdy_firsthero/hero_milesmorales.actor` id `0x93e0b3714a2afede` (Miles rig, AnimController, SkinManager, HeadManager, no HeroLocal).
  - A spawned object is inert until `ActivateObjects 0x1E24CC0 (objMgr=exe+0x83F8C00, {uint32* handles; int count}[] batches, nBatches)`. Flags at obj+8 go 0x20A -> 0x220B.
  - Handle = `((u16[obj+0x10] & 0x7ff) << 20) | (u32[obj+0xC] & 0xfffff)`; `ResolveHandle 0x1AA8E20(uint32*) -> obj`; local hero handle global at exe+`0x66FDF38`; obj[0] = actor (0x140-byte transform record), obj+0x30 = actor asset.
  - The base model is invisible. Visible suit = SkinManager parts (stride 0x6B8 at SkinManager+0x50, count +0x4C, config id at part+0x3C). Calling SkinManager vfunc 11 (vtable+0x58) with a list `{+0x100: items*, +0x108: count}`, items 0xC8 bytes with config id at +0x34, adds parts asynchronously. Copying the hero's part ids made the puppet render in the same suit.
  - Pose: actor+0xD8 = array of 241 joint matrices (4x4 floats, actor space). Copying the hero's array onto the puppet poses it, and the pose persists because the puppet runs no animation.
  - Move actor: `0x1E278C0 (actor, Mat4*, actor+0x70)`.
  - Add component: `GetOrCreateComponent 0x1AA5440 (obj, descriptor, initData)`. Component descriptors have the name pointer at +0x60.
- DEAD ENDS: `HeroLoadoutCloner` (descriptor exe+0x780EFB0) added with null init data crashes on its first update (null prius at cloner+0x40). Applying a loadout through the loadout manager (`0x246C580`, vfunc 17) did not add skin parts by itself.
- Probably useful, unverified: destroy by handle `0x24736F0 (uint32 handle)`; opacity `0xEA9900`.
- Melty contract (setup_multiplayer): mod reads `join=` from `%LOCALAPPDATA%\MilesTogether\melty.properties`, hosts on 0.0.0.0 when empty, writes `Melty join address: <ip:port>` / `Melty joined: <address>` / `Melty join failed: <why>` to `melty.log` there. Melty's relay carries friends to the listening port.
- Game crashed once during probing (cloner test). Saves untouched; backup from earlier still valid.

### Next
Per-frame tick choice, puppet destroy, suit change sync, compact pose packets, then the C++ `version.dll` proxy with TCP host/join and a Melty test mode.

## 2026-10-13: version 0.1.0 built and tested

### Route
- Loader: the mod is `winmm.dll` beside the game. `version.dll` was tried first and never loaded: the game process took Windows' own copy. The game imports winmm directly, so Windows loads the proxy at start. All 181 exports are forwarded with Windows' ordinals (`src/proxy.cpp`, `src/proxy_exports.inc`, `src/proxy.def`).
- Compiler: llvm-mingw 20261006 (ucrt, x86_64), SHA-256 of the zip checked against the release digest. Installed at `%USERPROFILE%\.codex\toolkits\llvm-mingw-dl`. MinHook 1.3.4 vendored in `third_party/minhook`.
- With this clang, `__try/__except` needs `-fms-extensions` and only catches faults raised inside a called, non-inlined function. The tick is therefore `GuardedTick()` calling `noinline Tick()`.
- Per-frame tick: `PhotomodeSystem::PreUpdate` at 0xEA4AA0, same thread as the camera commit, once per frame.

### More verified facts (all in the sheets)
- Destroy: `0x24736F0(handle)` queues the engine's deferred destroy; handle resolves to null about a frame later.
- The main menu has a hero object too (Miles on the train, made from the same hero actor). The Miles actor asset `0x93e0…` is not loaded there, which is how the mod tells menu from city.
- Mask, hood and body are body groups. A freshly dressed double shows none of them: call `apply_body_groups` (0x5AC830) on the visible body actor (0x5AB670) with the entries of the look from `resolve_selected_look` (0x5AB7A0). Entries are 16 bytes: name pointer, name length, name hash.
- Skin parts load asynchronously, also for a suit the receiving game never loaded. The double is respawned when the remote suit changes.
- 7 of the 241 joints carry non-unit scale, 4 sit up to 35 m from the actor (reach targets). The codec handles both.
- Gameplay keys are scan codes in `HKCU\Software\Insomniac Games\Marvel's Spider-Man Miles Morales\Input` (W 0x11, Space 0x39, LShift 0x2A swing, LCtrl 0x1D drop from a perch). SendInput with scan codes works for them; you cannot walk off a perch, press drop first.

### Dead ends
- Two copies of the game on one PC: the second needs `steam_appid.txt` (test only, removed again) and a renamed start-up mutex `i31_GameInitMutex`. It runs and the mod joins, but the game ignores all menu input in the second copy, with or without the mod, even while the first copy is suspended. A single copy started the same way is fine. So one PC can prove host, join and hello, not two players in the city.
- vgamepad could not attach to the installed ViGEmBus, so a virtual pad was not available.
- `HeroLoadoutCloner` and the loadout manager (see the 2026-10-12 entry).

### What was tested
- `tests/codec_test.cpp` in every build; `python -m unittest discover -s tests`.
- Mirror double (Melty test mode): all four checks pass and are written to the checks file.
- Echo peer over real TCP: suit, pose, away, ping; a suit not loaded locally; suit change; checkpoint reload; a 5-minute soak at 52 microseconds per frame on average.
- Two real copies: host line, join, hello, reconnect after a stall. `setup_multiplayer` read both logs: joined, matches host. It warns the address is private and that Melty's relay carries friends to it.
- NOT tested: two PCs, and the internet path through Melty's relay.

### State left on this PC
- Game folder: `winmm.dll` from the test build. Saves untouched (suit change during testing was not saved). Save backup from 2026-10-12 still at `%USERPROFILE%\.universal-modder\backups\miles-morales-coop-saves`.
- Melty draft: id `d2500e08-1d7d-4e9c-ab0e-cc0dff30f215`, slug `miles-together`.
- `tools/trace_player.py` was removed: it read sheets that no longer exist, and `tools/echo_peer.py` plus the mod's own log replace it.

### Melty draft and test (2026-10-13)
- Release 0.1.0 submitted as a draft (release id `0537d8e0-8ef7-41cd-8cb1-8e77070b9510`), package SHA-256 `3a4fe379…9f30bd3b`. `one_click_check`: yes. Listing has two screenshots and one clip.
- `test_release` 2d2f97ed: passed, 3 of 3 runs, all four checks each run. Melty installed the release itself into the game folder and parked it afterwards as `winmm.dll.melty-off`, so the game starts unmodded outside Melty.
- Quirks of testing this game through Melty: it reports the mod as loaded "in crs-video.exe" (the game's video helper also loads winmm.dll), cannot see the game window, and does not close the game when a run ends. The game had to be closed by hand after each run, and its menus driven (Enter, Enter) to reach the city. The first attempt failed on run 2 for that reason only.
- The screenshots and clip show the real build in the real game. The second Miles in them is driven by `tools/echo_peer.py` replaying the player's own movement over the network in another suit, not by a second person.
- `melty.json` at the project root holds the validated recipe. Nothing has been committed or published.

### Publishing on hold (2026-10-13)
- Creator: wait for a two-device test before publishing. They plan to use a Steam Deck signed in to their second Steam account.
- Steam Deck runs the game through Proton, which uses its own winmm unless told otherwise. Launch option needed: `WINEDLLOVERRIDES="winmm=n,b" %command%`. Not tested.
- This PC's firewall already allows the game inbound on TCP and UDP for all profiles, so this PC can host and the Deck can join at `<host LAN address>:47584`.

- Later the same day: the creator will use a second Windows PC instead of a Steam Deck. Steps are in `NEXT-STEPS.md`. Work committed locally on branch `free-roam-0.1.0`; nothing pushed, nothing published.
