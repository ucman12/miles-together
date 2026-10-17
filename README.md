# Miles Together

Online two-player free roam for the PC version of **Marvel's Spider-Man: Miles Morales**.
Each player runs their own copy of the game and their own save, and sees the other
player's Miles in the city: running, swinging, climbing and fighting, in the suit that
player is wearing.

Published on Melty: <https://melty.gg/m/miles-together>

## What a player gets

- Two players in one New York. One presses Play in Melty, the other joins from the
  mashup's page; Melty hands the joiner the host's address.
- The other player's Miles is the game's own Miles model, moved by the poses their game
  sends 20 times a second and smoothed in between.
- Your friend shows on your minimap as a person marker, even when far away. The full
  map in the pause menu does not show them yet.
- Time of day and weather are shared: the host's lighting is the one both players see,
  and the joiner's game follows when the host changes it.
- Suits follow: change your suit and your friend sees it, mask and hood included, even
  when it is a suit their game had not loaded.
- Nothing is written to save files. On a game version the mod was not built for it
  switches itself off and the game runs unchanged.

## What this version does not do

- Enemies, crimes and missions are not shared. Each player fights in their own city.
- The other player's web lines, gadgets and effects are not drawn.
- Two players per game.
- Friends reach a host through Melty's relay. Where the relay cannot carry a player,
  both need to be on the same network.

Shared enemies, missions and saves are the goal for later versions.

## Requirements

Marvel's Spider-Man: Miles Morales on Steam, version 4.630.0.0 (build 23986141,
`MilesMorales.exe` SHA-256 `0cfe5e52…f22d3256`, recorded in `sheets/compatibility.json`).
Both players need the same game version and the same Miles Together version.

## How it works

The release is one file, `winmm.dll`, placed beside the game. The game imports
`winmm.dll`, so Windows loads the mod with it; every real winmm call is passed on to
Windows' own copy. The mod then:

1. checks the executable against `sheets/compatibility.json` and the byte signatures in
   `sheets/native_functions.json`, and does nothing if anything differs;
2. hooks one per-frame function with MinHook;
3. each frame reads the local hero's transform, 241 joints and suit part ids;
4. sends them over one TCP connection (`src/net.cpp`, packing in `src/codec.cpp`);
5. spawns the game's own Miles actor as the other player's double, dresses it with the
   game's skin manager, and writes the received joints into it.

The game's models, textures, animations and sounds are never shipped. They are read
from each player's own installed copy while the game runs.

## The sheets are the source of truth

Everything the mod touches is a row in `sheets/*.json`: native functions and data,
structure offsets, assets, network messages, settings, the Melty contract, the test
checks and the systems that tie them together. `tools/gen_sheets.py` turns each row into
one piece of `src/generated/sheets.h`; the C++ uses no address or offset that is not in
a sheet. Change the sheet first, then the code.

`tools/preflight.py` lays the sheets over each other and lists every unfilled cell,
every unresolved reference, every native row not verified in the running game, and every
row no system uses. A build starts only when it is clean. `--stage release` also requires
every system's `verification` to be `passed`.

## Build

Needs Python 3.10+ and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).

```powershell
$env:MILESTOGETHER_LLVM_MINGW = "C:\path\to\llvm-mingw-...-ucrt-x86_64"
python tools/build.py                 # preflight, generate, compile, codec test, package
python tools/build.py --stage release # the same, and every system must be verified
python -m unittest discover -s tests -v
```

The package is `dist/MilesTogether-<version>.zip`: `winmm.dll` plus
`MilesTogether/README.txt`, `LICENSE.txt` and `THIRD-PARTY-NOTICES.txt`.

## Playing without Melty

Copy `winmm.dll` into the game's folder on both PCs. The mod keeps its files in
`%LOCALAPPDATA%\MilesTogether`:

- `melty.properties` holds `join=<host address>` on the joining PC, and `join=` (empty)
  on the host;
- `melty.log` gets `Melty join address: <address>` from the host and
  `Melty joined: <address>` from the joiner;
- `MilesTogether.log` is the mod's own diary of the session.

## Developer aids

- `tools/echo_peer.py <host:port>` joins a hosting game and plays the host's own poses
  back to it, so one PC can exercise the real network path.
- A file named `mirror.txt` in `%LOCALAPPDATA%\MilesTogether` shows a mirrored double
  beside the player when nobody is connected.
- Melty's test runs use the same mirror and report the checks in `sheets/test_checks.json`.
- A second copy of the game on the same PC can join the first, but the game itself does
  not let a second copy past its main menu, so real two-player play needs two PCs.

`MODLOG.md` is the journal: every verified address, every dead end, and how each was
found. Decompiled game code, extracted assets and captures stay outside this project.

## Licence and credits

Original code: MIT, by ucman, written with Claude. Remixes are welcome. See `LICENSE`
and `THIRD-PARTY-NOTICES.md`. Not affiliated with Sony Interactive Entertainment,
Insomniac Games, Nixxes or Marvel; the game and its content remain their owners'
property.
