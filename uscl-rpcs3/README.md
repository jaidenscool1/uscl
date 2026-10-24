# uscl-rpcs3

USCL's RPCS3 build: [RPCS3/rpcs3](https://github.com/RPCS3/rpcs3) (GPL-2.0) plus the patches in `patches/`.
Only changes the server side can't make go here: game-specific sceNp fixes, an NP call trace for working out
each game, and turning Steam invites into in-game invites. Rooms, friends, presence and P2P stay in uscl-rpcn.

## Build
```
git clone --recursive https://github.com/RPCS3/rpcs3.git && cd rpcs3
git checkout e53ac18            # the commit these patches were made against
git am ../patches/*.patch
```
Then build as in RPCS3's `BUILDING.md`.

## Patches
- 0001: send `CELL_SYSUTIL_NP_INVITATION_SELECTED` after `sceNpBasicRecvMessageCustom` picks an invite. Skate 3 waits for it
  before fading in after accepting an invite. Untested in game.
