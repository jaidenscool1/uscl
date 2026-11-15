# Pokémon HeartGold (Nintendo DS, melonDS)

| | |
|---|---|
| **Game code** | `IPKE` (ROM header: title `POKEMON HG`, maker `01`, unit code 0, ROM version 0, region byte 0): HeartGold, USA, revision 0 |
| **Dump** | sha1 `4fcded0e2713dc03929845de631d0932ea2b5a37`, the exact ROM that [pret/pokeheartgold](https://github.com/pret/pokeheartgold) builds (`heartgold.us/rom.sha1`), so the decomp's code and struct layouts apply unchanged. A second dump of the player's with the same header has a different hash and is untested |
| **Emulator** | melonDS (`uscl-melonds/`) |
| **Goal (player, 2026-11-13)** | friends in **one shared world**: everyone sees everyone, each explores on their own, and players battle each other in the open world |
| **Status** | 🧪 **first slice works (2026-11-13):** two melonDS 1.1 copies on one PC, each player sees the other as an NPC on the same map, following their steps |

## Why this needs its own layer
HeartGold has no shared overworld. Its multiplayer is local wireless (Pokémon Center 2F: Colosseum battles, Union
Room, trades), the Pokéwalker and Nintendo WFC (closed in 2014). Carrying local wireless over Steam (melonDS LAN mode
through the bridge, like Dolphin NetPlay or Azahar rooms) gives battles and trades, but only inside those rooms.
Seeing each other on the map needs an agent per player that reads the player's position and draws the others.

## Shared world: how it works
- Each player runs their own game and save. Nothing is in lockstep, so nobody waits for anyone.
- `uscl-melonds/hgss_world.py` (one per player) talks to that player's melonDS through melonDS's GDB stub
  (127.0.0.1, ARM9 port). About 20 times a second it reads the player's map, tile, position vector, facing and
  sprite, and sends them to the other agents over UDP.
- For each other player on the same map it creates an overworld object (the game's own `MapObject_Create`, called
  from the game's thread at a safe point), marks it with object id `0xF0 + n` and moves it to the other player's
  position each tick. When the other player leaves the map, it removes the object (`MapObject_Remove`). The game
  removes it by itself when the local player changes map.
- The puppet uses the other player's own sprite id (Ethan, Lyra, bike...). A sprite change re-creates it.

## RAM map (USA rev 0, from the decomp + this ROM's ARM9)
ARM9 is BLZ-compressed in the ROM (module params at ARM9 offset `0xBA0`, compressed end `0x020BA314`, BSS
`0x02111860`–`0x021E5900`). After decompressing, these were read from the code:

| What | Address / offset | How it was found |
|---|---|---|
| `sFieldSysPtr` (static `FieldSystem *`) | `0x021D4158` | literal pool of `sub_0203E2F4` (`sFieldSysPtr->processManager->isPaused = TRUE`) |
| `FieldSystem_Control` (per frame, player can move) | `0x0203E15C` (Thumb) | function order in `field_system.c` against the ARM9's `push {..lr}` prologues |
| `MapObject_Create(man, x, z, dir, sprite, movement, mapNo)` | `0x0205E294` (Thumb) | the small function between `MapObject_CreateFromObjectEvent` (`0x0205E1D0`) and `MapObject_CreateWithParams` (`0x0205E2B4`, which it calls) |
| `MapObject_Remove(obj)` | `0x0205E3CC` (Thumb) | its calls match the source (`…F5D4, F498, F444, F348, F174`, then `MapObject_Clear` `0x0205ED80`); `sub_0205E104` calls it |
| `FieldSystem` | `+0x20` `Location *` (`mapId` at +0), `+0x3C` `MapObjectManager *`, `+0x40` `PlayerAvatar *` | `include/field_system.h` |
| `PlayerAvatar` | `+0x30` `LocalMapObject *` | `include/player_avatar.h` |
| `LocalMapObject` (0x12C bytes) | `+0x00` flags (bit 0 active), `+0x08` id, `+0x0C` mapId, `+0x10` spriteId, `+0x28` facing, `+0x58/0x64` previous/current tile x,y,z, `+0x70` position (fx32 x,y,z) | `include/map_object.h` |

| `FieldSystem_CreateTask(fs, func, env)` / `TaskManager_Call` | `0x020504F0` / `0x02050530` (Thumb) | `task.c` order: `Task_New` `0x020504C0` (two `Heap_AllocAtEnd`), then these |
| `CallTask_NamingScreen(task, type, species, maxLen, partyIdx, defaultStr, retVar)` | `0x0203F6E0` (Thumb) | after `Task_NamingScreen` `0x0203F580` and `SetName` `0x0203F650`; calls `NamingScreen_CreateArgs` `0x020830D8` |
| `NamingScreen_DeleteArgs(args)` | `0x0208311C` (Thumb) | called at the end of `Task_NamingScreen`; typed text still in `args + 0x1C` (`nameInputFlat`, empty when unchanged) |
| `Battle_Init` (`return TRUE;`) | `0x0203E3A8` (Thumb) | `movs r0,#1; bx lr` after a `pop {r3,pc}`; used as a do-nothing parent task (a task ends only on exactly `TRUE`) |
| `BeginNormalPaletteFade(mode, typeMain, typeSub, color, steps, framesPerStep, heap)` | `0x0200FA24` (Thumb) | `asm/unk_0200FA24.s` |

Constants: movement `0` = standing still (the most common object-event movement), facing 0 up / 1 down / 2 left /
3 right, sprite `0` Ethan, `97` Lyra. Tile to position: `x * 16.0 + 8.0` in fx32 (`x * 0x10000 + 0x8000`).

## Battles in the open world (planned, PLAN.md "Nintendo DS" phase D3)
The game's real link battle, so rules, items and results are the game's own:
1. First prove it by hand: two melonDS on one PC (melonDS local multiplayer), both walk into the Pokémon Center 2F
   Colosseum and battle. Then the same with melonDS LAN mode over loopback, then over the bridge.
2. Then start it from the overworld: player A faces player B's puppet and presses A; the agents agree on the battle
   over their UDP channel and put both games into the Colosseum battle (the game's own link-battle field task, found
   in the decomp), then return each player to where they stood.

Rejected: rebuilding a battle from the other player's party data inside one game (an AI trainer is not a player).

## Open questions
- Does melonDS's GDB stub keep full speed (it needs the JIT off: interpreter only)? Measure on the slice.
- Puppets are teleported each tick; walking animation needs the game's movement commands (phase D1).
- melonDS LAN mode fixes UDP 7064 (ENet) and 7063 (discovery) in `src/net/LAN.cpp`, so a bridge forward needs a
  small melonDS patch for a configurable port (or a second loopback address) before more than two players.
- Does HeartGold's local wireless work in melonDS with FreeBIOS and its generated firmware, or does it need the
  player's own BIOS/firmware dumps?

## Log
- 2026-11-13: ROM header and hash checked; melonDS picked; addresses above found; `hgss_world.py --selftest` PASS
  (fake GDB stub: reads the player, injects `MapObject_Create`, restores registers, moves and removes the puppet).
  Not yet run against a real melonDS: no melonDS binary on this PC and no clang/MSYS2 to build one.
- 2026-11-13, later: official melonDS 1.1 Windows build (sha256 matches the release digest), two portable copies
  (GDB 3333 / 3343, own saves). Two new saves, both players in the house. **Each agent created the other player
  (`… appeared on map 63 at [3, 0, 3]`) and moved it as the other walked (`B map 63 tile [4,0,5] → [3,0,5] →
  [3,0,6] → [3,0,7] (shown)`), and the player confirmed seeing them.** Bugs fixed on the way: (1) a new GDB connection
  halts the CPU until `c`; (2) in SYS mode the stub's `G` also writes `sp_usr`/`lr_usr`, which overwrote the injected
  call's sp/lr and crashed both games (black screens); the self-test's fake stub now models this; (3) the stub stops
  answering while emulation is paused, so the agent now waits instead of timing out. melonDS 1.1's stub never notices a
  client hang-up on Windows, so an agent that exits means restarting melonDS.
- 2026-11-13, later: smoother puppets: instead of writing the position each tick, the agent starts the game's own
  step command on the puppet (`movementCmd` +0xA4 = step, `movementStep` +0xA8 = 0, flags |= 0x10, &= ~0x20, as
  `MapObject_SetHeldMovement` `0x0206214C` does), copying the other player's last step command (walk 12–15, run
  16–19, …) and running to catch up when 2–3 tiles behind; further means a teleport. Relay mode (`--relay`) and the
  bridge preset `hgss` (`udp:47600`). Local test through the relay: both agents saw each other walk, and the player confirmed the movement is smooth.
- 2026-11-14: **in-game code box.** Holding L+R for 1 s in the overworld (`KEYINPUT` 0x04000130) opens the game's own
  keyboard, type 7 ("Your friend's name?", saves nothing), filled in with the player's session code. The agent
  creates a parent task (`FieldSystem_CreateTask` with `Battle_Init` as its function), then `CallTask_NamingScreen`,
  both from one stop at `FieldSystem_Control`; it reads the text at `NamingScreen_DeleteArgs`, then fades the screen
  in (`BeginNormalPaletteFade(0, 1, 1, 0, 6, 1, HEAP_ID_FIELD1)`, as scripts do after `NicknameInput`). A typed code
  makes the agent leave its own session and run `uscl-bridge join <code>`. Tested on one PC: the box shows the
  session code, closes, and the game and the other player carry on. Not yet tested: joining a friend by code (needs
  two PCs). Bugs found on the way: setting a breakpoint while the CPU runs raced with the CPU reaching it (the stub
  stopped and waited for an ack the agent never sent: frozen games), fixed by halting first and queueing stop
  reports; a parent task returning non-`TRUE` never ended (no control back); no fade-in left the top screen black.
- 2026-11-14: **talk menu (first attempt; superseded by the next entry).** Talking to a puppet should open BATTLE / TRADE / CANCEL (Union
  Room text, bank 738: 192 / 193 / 198), with TRADE leading to POKéMON / ITEM / CANCEL (bank 775: 61 / 73 / 89); the
  choice goes to the other agent as `{"req": ...}`. How: puppets get script id 2000 (`_std_misc`, loads the large
  `scr_seq_0003`); a breakpoint at `CreateScriptContext` `0x020400AC` (armed only while the player faces a puppet,
  and then every tick runs with the CPU stopped: melonDS reads the bytes after a stop report as its ack and eats any
  command in flight, which dropped the connection on the first try) runs to `sub_0203FD68` `0x0203FD68` (context
  loaded, nothing run yet), writes the menu bytecode over `ctx->mapScripts` and points `ctx->script_ptr` at it, then
  swaps `ctx->msgdata`; the choice is read at `DestroyScriptContext` `0x02040044` from `ScriptEnvironment + 0xA4`
  (`VAR_SPECIAL_RESULT`). Other addresses: `StartMapSceneScript` `0x0203FE74`, `NewMsgDataFromNarc` `0x0200BAF8`,
  `DestroyMsgData` `0x0200BB44`, `HEAP_ID_FIELD2` = 11, `NARC_msgdata_msg` = 27.
  **Where it stops:** the breakpoint fires and the script is patched, then the injected
  `NewMsgDataFromNarc(1, 27, 738, 11)` data-aborts (`pc ffff0104 lr 020d7754`, abort mode). Next: (1) find what is
  at `020d7754`; (2) or skip the text-bank swap: `MenuInitStdGmm` (749) uses the standard bank `msg_0191`, which has
  BATTLE (52), TRADE (23), CANCEL (24), so the main menu needs no new message data.
- 2026-11-15: **talk menu works.** Facing another player and pressing A shows "Talking to ..." with BATTLE / TRADE /
  CANCEL; TRADE opens POKéMON / ITEM / CANCEL; the choice reaches the other agent (`A asks you: trade item`), the
  window closes and the player can move. What it took: (1) no injected `NewMsgDataFromNarc` (it data-aborted):
  instead stop at `LoadScriptsAndMessagesParameterized` `0x02040168` and set its `msgBank` argument (r3, GDB `P`
  write) so the game loads 738 / 775 itself; (2) no `LockAll` (it waits for every object to stop; a puppet paused
  mid-step never does); (3) the game's own menu pattern: `NPCMsg` prompt, `TouchscreenMenuHide`, `MenuInit` …
  `MenuExec`, `TouchscreenMenuShow`, `CloseMsg`, `ReleaseAll`, `End` (without the message window and the hidden
  touchscreen menu the menu never draws and takes no input). Not done: battles / trades themselves (D3, D4).
- 2026-11-15: **request popups work.** The other player gets the Union Room question with YES/NO (738 #21 battle,
  #26 trade) showing the asker's trainer name; their answer shows #22/#27 or #23/#28 to them and #22/#27 or #201
  ("Whoops! Sorry, but I have to do something else.") to the asker. Tested: A → TRADE → ITEM, B accepted, both saw
  the messages. Names: each agent reads its trainer name once (`Save_PlayerData_GetProfile` `0x02028E9C`, u16[8]) and
  sends it with its position; the agent writes it into text buffer 0 (`env+0x44` MessageFormat → `fields[0].msg`)
  at the first `RunScriptCommand` (`0x0203FD6C`), when the buffers exist. Popups start like the TRADE submenu
  (`StartMapSceneScript` with PUPPET_SCRIPT, swapped bytecode: `NPCMsg`, `YesNo VAR_SPECIAL_RESULT` or `WaitButton`,
  `CloseMsg`, `ReleaseAll`, `End`).
- 2026-11-15: **link battle works between two melonDS copies** (one PC, agents stopped). melonDS LAN mode: copy A
  Multiplayer → Host LAN game (UDP 7064, discovery 7063), copy B Join LAN game; both entered the Pokémon Center 2F
  Colosseum and played a single battle through. Needed: a distinct wireless MAC for B (two processes are both
  "instance 0", so the generated firmware gave both the same MAC): `[Instance0.Firmware] OverrideSettings = true`,
  `MAC = "00:09:BF:0E:49:17"`. The Colosseum only opens later in the story (not tied to `FLAG_GOT_POKEDEX`). The
  joiner connects out from a random port, so the bridge only forwards 7064 (`hgss` preset now `udp:47600,udp:7064`);
  over Steam the joiner uses Direct connect to 127.0.0.1.
- 2026-11-15: **battle from the overworld works, round trip.** BATTLE accepted → each agent remembers its spot,
  warps its player (FadeScreen/Warp/FadeScreen script) to the Cherrygrove Pokémon Center desk (map 69, 11,5,
  facing up), then starts the attendant's own `std_wireless_colosseum` (9001) with her map object as the one talked
  to, so her menu opens by itself. Gotcha: `StartMapSceneScript` creates the one field task
  (`FieldSystem_CreateTask` asserts `taskman == NULL`); started while the warp script's task was still ending, the
  new task was dropped. The agent now waits for `FieldSystem.taskman` (+0x10) to be empty before starting a script.
  After the battle, back at the desk, each agent warps its player home to where they stood: confirmed.
- 2026-11-15: **link without the LAN menu.** Patched melonDS (`uscl-melonds/patches`): the agent sends GDB
  `monitor uscl-link host` when uscl-bridge says `hosting`, `join` when it says `joined`, `off` before joining a
  friend (the hosted link holds 127.0.0.1:7064, which the joining bridge listens on). One PC, A `--bridge` (host),
  B `--link join`: the LAN host bound UDP 127.0.0.1:7064 only, and a battle from the overworld went through without
  touching melonDS's Multiplayer menu. GDB stubs now listen on 127.0.0.1 only. Two PCs over Steam: untested.
- 2026-11-15: **trades from the overworld work.** An accepted TRADE (POKéMON or ITEM) warps both players to
  `UNION_DESK` (map 69, tile (6, 5) facing up, in front of `std_union_room` (9003) at (6, 4)) and starts the
  attendant's script on her object, as for battles. In the Union Room (MAP_UNION, 2) the players talked to each other
  and traded with the game's own screens; leaving the room back to map 69 warped each home. Gen 4 has no item trades,
  so ITEM goes to the Union Room too. Two copies on one PC, A `--link host`, B `--link join`.
- 2026-11-15: **stuck behind the counter after Continue.** Entering a link room sets flag `0x966` and saves; the
  game's own return script (map init script, `std_union_room_return` / `std_wireless_colosseum_return`) walks the
  player out and clears it. The agent warped home before that script ran, so `0x966` stayed set, and
  `FieldTask_ContinueGame_Normal` then loaded both saves at the link-room exit behind the counter. Fix: back on the
  Pokémon Center map, warp home only after no field task has run for 1.5 s. `--warp MAP,X,Z,DIR` got both out.
- 2026-11-15: **following Pokémon shown.** `FieldSystem.followMon` (`+0xE4`: map object pointer, `active` at
  `+0xE4+0x16`); the follower's map object has its follower sprite (428–993) and `param[0..2]` (species, form/shiny,
  `+0x38`). The agent sends it with the player's state; the other side spawns it as a puppet with those params,
  moved like a player puppet.
- 2026-11-15: **smooth puppets: halt once per tick.** A running melonDS answers one GDB packet per frame, so a
  ~15-packet tick ran at ~4/s (choppy, jumpy with followers). Stopping the CPU first makes every packet immediate:
  29.4 ticks/s measured on both agents, smooth in game.
- 2026-11-15: **NPCs in sync.** Each game moves its NPCs with its own random numbers (movement types: wander, look
  around), so players saw them differently. Now the player whose name sorts first on a map leads: its agent reads
  the map's objects (ids below 0xF0: not the player 255, a follower 253 or our puppets) every 3rd tick and sends
  `[id, tile, facing, step]`; the others set `MAPOBJECTFLAG_MOVEMENT_PAUSED` (0x40) on the same ids and walk them
  with held movements like puppets. Skipped while a field task runs. Full scan of the 64 slots every 2 s, cached
  slots in between. Cherrygrove (map 67), two copies on one PC: NPCs match in both games.
