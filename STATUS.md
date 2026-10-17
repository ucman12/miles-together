# Miles Together: what it does and what it does not

Last updated 2026-10-17. "Tested on two PCs" means the creator played it on two Windows PCs
on one home network. "Tested on one PC" means it was checked on the host PC with a test
tool standing in for the second player.

## Versions

| Version | What it adds | State |
|---|---|---|
| 0.1.0 | Two-player free roam: each player sees the other's Miles | Live on Melty |
| 0.2.0 | Friend's marker on the minimap | Live on Melty |
| 0.3.0 | Shared time of day and weather | Live on Melty since 2026-10-17 (current public version) |
| 0.4.0 | Shared crime enemies, pause-menu options | In progress; not published |

Source: https://github.com/ucman12/miles-together

## What it does

### Live on Melty, tested on two PCs
- Two players, each in their own game and save, in the same New York.
- The other player's Miles moves, swings, climbs and fights, smoothed between updates.
- Suit changes show on the other player's screen, including suits their game had not loaded.
- The other player shows on your minimap as a person marker, even from far away.
- The other player stays with you after fast travel.
- If the game version doesn't match, the mod switches itself off and the game runs unchanged.
- Nothing is written to save files.

- Time of day and weather follow the host. When the host's changes, the joiner's game
  switches within a few seconds.

### 0.4.0 (in progress)
- The host's crime enemies are sent to the joiner, up to 12 at a time, within 120 m of either
  player. The joiner's game loads enemy types it doesn't have.
  - Tested on two PCs: no crashes, and enemy types missing on the joiner now load (in about
    0.07 s).
  - Not working yet: in the last test (2026-10-16) the joiner's log says the enemies were
    created, but the creator did not see them. Cause not found yet.
- Pause menu: **Go to your friend** puts you next to the other player. It works when perched.
  Tested on the joiner on two PCs; not yet tried from the host.
- Pause menu: **Start a crime (test)** makes the game start a random crime now. Tested on
  both PCs, but each game starts its own crime (see below).

## What it does not do (yet)

- **Enemies on the joiner don't show reliably.** See above.
- **The joiner's own crimes still run** and are not shared with the host. Only the host's
  crimes are shared.
- **No fighting together.** The joiner can't damage the host's enemies, and enemies can't
  damage the joiner. Enemy health and deaths are not shared.
- **No crime markers for the joiner.** They don't see the host's crimes on their map.
- **Nothing besides enemies from activities is shared:** cars, hostages, drones and bags.
- **No hideouts, bases, side missions or story missions together.**
- **No shared collectibles or rewards.** Each player keeps their own.
- **The other player's web lines, gadgets and effects aren't drawn.**
- **The friend isn't shown on the full pause-menu map**, only on the minimap.
- **Two players only.**
- **Internet play through Melty's relay is untested.** All tests so far were on one home
  network.
- **Steam Deck / Proton is untested.**

## Built but not yet tested (2026-10-16, uncommitted build, not installed anywhere)

- The joiner logs where each enemy copy is, how far it is from their Miles, whether the game
  deleted it, and a summary every 5 seconds. This is to find why enemies don't show.
- The host keeps an enemy shared for 3 seconds after it briefly drops out, instead of removing
  it at once.
- One set of crimes: while joined, the joiner's own random crimes are held off, and the joiner's
  Start a crime asks the host's game to start one. Protocol version 4, so this build and
  earlier 0.4.0 builds refuse each other.
- `tools/echo_peer.py --record` / `--replay`: record the enemies a host shares, then play them
  back to this PC's game as the joiner, so the joiner's side can be tested on one PC.

## Plan

1. Make shared enemies reliable: find why they don't show, and stop them dropping out for a
   second.
2. One set of crimes: switch off the joiner's own crimes. The joiner's Start a crime asks the
   host.
3. Fighting together: the joiner's hits damage the host's real enemies, and enemies that
   attack the joiner's Miles damage the joiner.
4. Share enemy health and deaths, so a crime finishes for both players.
5. Share the other objects activities create (cars, hostages, drones).
6. Hideouts and bases, then side missions (the host plays them and the friend fights alongside).
7. Collectibles stay separate for each player.

Detailed research notes, addresses and test results are in `MODLOG.md`.
