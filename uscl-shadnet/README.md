# uscl-shadnet: PSN for PS4 games (shadNet)

| | |
|---|---|
| **Console** | PlayStation 4 |
| **Upstream** | [shadps4-emu/shadNet](https://github.com/shadps4-emu/shadNet) (GPL-2.0) |
| **Pinned base** | `6f534a1` |
| **Our changes** | `patches/0001-uscl.patch`, one diff applied with `git apply` |
| **Runs on** | the host's PC; members reach it at 127.0.0.1:31513 (web API 31515) through `uscl-bridge host bo2ps4` |

The stand-in for PS4 sign-in, friends and presence, for [uscl-shadps4](../uscl-shadps4/).

## Build
```
git clone --recursive https://github.com/shadps4-emu/shadNet.git && cd shadNet
git checkout 6f534a1            # the commit the patch was made against
git apply ../patches/0001-uscl.patch
```
Needs Qt 6 with QtHttpServer.

## Patch
- `USCL_LOCAL=1`: an unknown name signing in gets an account made on the spot (like uscl-rpcn). A name that
  exists still needs its password, so a name can't be taken over. It also keeps the working directory it was
  started in (the player's USCL folder) instead of moving to its own folder, which may not be writable.
- Links with `--exclude-libs,ALL`: its LibreSSL clashed with the system Qt's OpenSSL.

The USCL app writes `shadnet.cfg` the first time it hosts a PS4 game (ports 31513/31515, admin API off).

## USCL release integration

The v0.5.3-alpha release line integrates the reviewed installer/library work and
console themes. Use the [release page](https://github.com/jaidenscool1/uscl/releases/tag/v0.5.3-alpha)
for published assets and the [fork matrix](../README.md#fork-maintenance) for build pins.
This does not update the upstream base or establish new gameplay/online support
for this fork. Patch changes require rebuilding the corresponding core; updating
the shell alone does not update a separately installed emulator.

Switch 2 CPU/GPU translation, game evidence and pending work are tracked in
[SWITCH2-PROGRESS.md](../docs/SWITCH2-PROGRESS.md).
