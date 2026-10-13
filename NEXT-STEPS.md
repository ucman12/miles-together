# Next steps

Miles Together 0.1.0 is a private draft on Melty. It is built, uploaded and passed Melty's
own test. It is not published: that waits for a test with two PCs.

Studio: <https://melty.gg/studio/d2500e08-1d7d-4e9c-ab0e-cc0dff30f215>

## 1. Two-PC test

Both PCs on the same network. The second PC signed in to the other Steam account, with the
game on version 4.630.0.0.

**This PC (host)**

1. Unzip `winmm.dll` from `dist/MilesTogether-0.1.0.zip` into the game's folder:
   `D:\SteamLibrary\steamapps\common\Marvel's Spider-Man Miles Morales`
2. Start the game from Steam and load a save.
3. Open `%LOCALAPPDATA%\MilesTogether\melty.log` and note the address after
   `Melty join address:`. It was `<host LAN address>:47584` on 2026-10-13 and can change.

**Second PC (joiner)**

1. Copy the same `winmm.dll` into its game folder.
2. Create `%LOCALAPPDATA%\MilesTogether\melty.properties` with one line:
   `join=<the address from step 3 above>`
3. Start the game from Steam and load a save.

Once both are in the city you should see each other, each in your own suit, and on each
other's map.

## 2. What to look for

- Does the other Miles appear, and does he move smoothly while running and swinging?
- Does a suit change on one PC show on the other within a few seconds?
- Does either game stutter, freeze or crash?
- Take a screenshot or short clip on either PC. It can replace the current listing media,
  which shows a test tool playing the second player.

## 3. Afterwards

- Keep `%LOCALAPPDATA%\MilesTogether\MilesTogether.log` from both PCs. Bring both to the
  next session, whether it worked or not.
- To go back to the unmodded game, delete `winmm.dll` from the game folder. On the second
  PC also delete `melty.properties`.
- Tell Claude the result. If it worked, say "publish". If not, the logs say where it stopped.

## 4. Still open after publishing

- The internet path through Melty's relay has not been tried. The two-PC test above is on
  one network. A test from two different homes would cover it.
- Shared enemies, missions and web lines are the plan for later versions.
