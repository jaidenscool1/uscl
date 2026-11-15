# uscl-melonds: Nintendo DS (melonDS)

| | |
|---|---|
| **Console** | Nintendo DS (DSi later) |
| **Emulator** | [melonDS](https://github.com/melonDS-emu/melonDS) (GPL-3.0), pinned base `497aa6cdd26f45f8064b78916b00a773eab4ded7` (2026-11-04) |
| **Our changes** | `patches/0001`–`0002` (GPL-3.0), applied with `git am`: the wireless link starts itself over uscl-bridge, and the GDB stub listens on 127.0.0.1 only. `hgss_world.py` (Pokémon HeartGold shared overworld) talks to the GDB stub |
| **Online** | shared overworld: per-game agent over UDP; battles and trades: melonDS's LAN link (ENet, UDP 7064) on 127.0.0.1, which uscl-bridge carries over Steam |
| **Status** | two copies on one PC: shared overworld, talk menu, battles and trades from the overworld with the link started by the agents (patched melonDS, 2026-11-15); two PCs over Steam untested |

## Why melonDS
- **Every USCL OS:** builds on Windows, Linux and macOS (CMake + vcpkg presets, Flathub on Linux, official builds for
  all three).
- **Accurate DS wireless:** local multiplayer between instances, and a LAN mode across PCs (ENet over UDP), which the
  Steam tunnel can carry like Dolphin NetPlay or Azahar rooms. HeartGold's battles and trades are local wireless.
- **A debugger port with memory read/write, registers and breakpoints (GDB stub, `ENABLE_GDBSTUB` on by default):**
  the per-game agent reads and drives the game through it.
- **License:** GPL-3.0. A patch folder like `uscl-rpcs3/patches` works the same way; the patches stay GPL-3.0. melonDS
  never links Steamworks: only `uscl-bridge` talks to Steam, and the agent is a separate process on local sockets.
- Not chosen: DeSmuME (Lua scripting, but weaker wireless and Windows-first frontend), BizHawk (Lua, Windows-first
  .NET, not on macOS), no$gba (closed, Windows only).

## Bring your own files
ROM, and optionally BIOS (`bios7.bin`, `bios9.bin`) and firmware, dumped from your own DS. melonDS runs DS games
with its built-in FreeBIOS and a generated firmware; whether local wireless needs real dumps is still open. Keep them
outside the repo (`local system files/` is excluded in `.git/info/exclude`).

## Two test instances on one PC (the first slice)
Two copies of melonDS, each in its own folder with a `portable` subfolder (melonDS then keeps its config and saves
there, never in the player's real config). Each `portable/melonDS.toml`:

```toml
[JIT]
Enable = false          # the GDB stub needs the interpreter

[Instance0.Gdb]
Enabled = true

[Instance0.Gdb.ARM9]
Port = 3333             # second copy: 3343
BreakOnStartup = false

[Instance0.Gdb.ARM7]
Port = 3334             # second copy: 3344
BreakOnStartup = false
```

Each copy needs its own HeartGold save past the intro (each player's own game). Then load the game in both,
walk out onto the same map, and start one agent per copy:

```
python uscl-melonds/hgss_world.py --relay 47600
python uscl-melonds/hgss_world.py --gdb 3333 --listen 127.0.0.1:47610 --peer 127.0.0.1:47600 --name A
python uscl-melonds/hgss_world.py --gdb 3343 --listen 127.0.0.1:47611 --peer 127.0.0.1:47600 --name B
```

With `--bridge <uscl-bridge>` the agent starts the player's own session itself (the relay and `uscl-bridge host
hgss`). **In the game, hold L+R for a second:** the game's own keyboard opens with your 6-letter session code; give
it to friends. To join a friend, type their code and press OK: the agent leaves your session and runs `uscl-bridge
join <code>`, which finds that session among your Steam friends' sessions.

**Talking to another player** (face them, press A) opens the game's own menu: BATTLE / TRADE / CANCEL, and TRADE
opens POKéMON / ITEM / CANCEL. The other player gets the request as an in-game YES/NO question with your trainer name, and both see the answer.
When a BATTLE is accepted, both players are warped to the Colosseum desk in Cherrygrove's Pokémon Center and the
attendant's menu opens. After the battle, back at the desk, each player is warped home. An accepted TRADE works the
same way through the Union Room desk: inside, talk to each other and pick TRADE. ITEM goes there too (HeartGold has
no item trades: give the Pokémon the item to hold).

**The wireless link** (battles, trades) needs the patched melonDS. The agent sets it from the player's session: our
own session hosts it on 127.0.0.1:7064, a joined one connects to 127.0.0.1:7064, where uscl-bridge listens and
carries it to the host (`hgss` preset). No Multiplayer menu. Without `--bridge`, `--link host|join` sets it (one-PC
tests: B joins A directly). Official melonDS prints `wireless link: needs the USCL melonDS build`; its Multiplayer →
LAN menu still works by hand.

The relay passes each player's position to everyone else. Over Steam it runs on the host only: `uscl-bridge host
hgss` (UDP 47600) on the host, `uscl-bridge join <session>` on each friend's PC, and every agent keeps
`--peer 127.0.0.1:47600` (on a friend's PC the bridge listens there and carries it to the host). The other players
walk and run with the game's own step animations (the same movement commands scripts use for NPCs); a player more
than 3 tiles away is moved there directly.

**Stop agents with `--stop <file>`** (create the file): they remove their breakpoints first. melonDS keeps
breakpoints after the debugger is gone, so a killed agent can leave the game stuck at the next script start
(black screen on a door).

**One agent per melonDS run.** On Windows, melonDS's stub never notices a client disconnecting (the socket stays
in `CLOSE_WAIT`) and then accepts no new client; restart melonDS after an agent exits.

Each prints its own map/tile and the other's, and `B appeared on map …` when the other player is drawn.
`python uscl-melonds/hgss_world.py --selftest` checks the agent against a fake GDB stub (no emulator needed).

## Patches
| | |
|---|---|
| `0001` | `--uscl-link host\|join` at launch, GDB `monitor uscl-link host\|join\|off` while running: LAN link on 127.0.0.1 (host bound there, no discovery broadcasts; join retries until the host is up). Shortcut: two players per link (LAN clients connect to each other by address, and through the bridge everyone is 127.0.0.1) |
| `0002` | GDB stub binds 127.0.0.1 instead of every interface |

```
git clone https://github.com/melonDS-emu/melonDS.git && cd melonDS
git checkout 497aa6cdd26f45f8064b78916b00a773eab4ded7
git am ../patches/*.patch
```
Regenerate the whole folder after a change: `git format-patch 497aa6cd -o <repo>/uscl-melonds/patches`.

## Building melonDS on Windows
melonDS's Windows preset (`release-windows-x86_64`) uses clang + vcpkg; its first configure builds Qt 6 and the
other dependencies (1.2 h, cached afterwards). MSVC's `cl` is not supported. Things that bite:

- Visual Studio needs the **C++ Clang Compiler for Windows** component (clang-format/clang-tidy alone aren't it).
- vcpkg builds the dependencies with the newest Visual Studio installed. With VS 2022 + VS 18 (2026), Qt came out
  against VS 18's C++ library (MSVC 14.51), so melonDS must link in VS 18's environment (its `vcvars64.bat`), with
  clang, CMake and Ninja from VS 2022. VS 18's library wants clang 20 and VS 2022 has 19.1.5: configure with
  `-DCMAKE_CXX_FLAGS=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` (built and played fine, 2026-11-15).
- Then `cmake --preset release-windows-x86_64 ...` and `cmake --build --preset release-windows-x86_64`; the exe is
  `build/release-windows-x86_64/melonDS.exe`. Put it next to a `portable` folder.

Alternatives: MSYS2 MinGW (`release-mingw-x86_64`). Linux and macOS: melonDS's `BUILD.md`.
