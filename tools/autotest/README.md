# Automated two-copy tests (Black Ops II)

Runs two RPCS3 copies of a game on one PC and drives them without a person, used for BO2 matchmaking
(`games/BLUS31011.md`). Linux + Hyprland (window placement and screenshots), `grim`, Python 3 with Pillow.

- `vpad.py <socket> <x360|xone>`: a virtual game pad on `/dev/uinput` (your user needs write access; RPCS3's own
  pad thread already uses it). Commands: `press A|B|X|Y|START|... [ms]`, `dpad UP|DOWN|... [ms]`, `stick L|R x y`.
  Keyboard injection (`hyprctl sendshortcut`) does **not** work: the press is shorter than RPCS3's pad poll.
- `pad.sh <a|b> <command>`: send a command to copy a's / b's pad (sockets in `$USCL_TEST`, default
  `~/.local/share/uscl/test`). Start the pads first: `vpad.py $USCL_TEST/a.sock x360`, `vpad.py $USCL_TEST/b.sock xone`.
- Configs `~/.local/share/uscl/rpcs3-bo2a` / `rpcs3-bo2b`: copies of `rpcs3-bo2` with SDL pads "Xbox 360 Controller 1"
  / "Xbox One S Controller 1", bind address 127.0.0.1 / 127.0.0.2, RPCN accounts uscltest / uscltest2, and the IP
  swap list including the stun names (`uscl-dw/README.md`).
- `bo2ctl.sh launch|key|shot|win`: start a copy (via `systemd-run --user`, so it runs at normal priority), find its
  window, screenshot output `JSXTV` (change for your setup). `place.sh` pins both windows side by side.
- `bo2run.sh <a|b> <npid>`: start a copy and drive it to Multiplayer → Play Online → Core → Team Deathmatch. Waits
  on our Demonware's log (`$USCL_TEST/dw.out`), and picks MULTIPLAYER by reading the screen (`menusel.py`: the
  selected entry's width; menu input is ignored for a variable time after START).
- `online.sh <a|b>`: from the multiplayer menu to Team Deathmatch.

Limits: two BO2 copies use all of a 6-core CPU; a match runs for about a minute, then the host starves. Use two
PCs for full matches. Pick a class in the match (press A) or the game kicks you for not spawning.
