# uscl-shadnet

USCL's shadNet server (PS4 sign-in, friends, presence for shadPS4): [shadps4-emu/shadNet](https://github.com/shadps4-emu/shadNet)
(GPL-2.0) plus `patches/`. The host runs it; members reach it at 127.0.0.1:31513 (and web API 31515) through
`uscl-bridge host bo2ps4`.

## Build
```
git clone --recursive https://github.com/shadps4-emu/shadNet.git && cd shadNet
git checkout 6f534a1            # the commit the patch was made against
git apply ../patches/0001-uscl.patch
```
Needs Qt 6 with QtHttpServer.

## What the patch does
- `USCL_LOCAL=1`: an unknown name signing in gets an account made on the spot (like uscl-rpcn). A name that
  exists still needs its password, so a name can't be taken over. It also keeps the working directory it was
  started in (the player's USCL folder) instead of moving to its own folder, which may not be writable.
- Links with `--exclude-libs,ALL`: its LibreSSL clashed with the system Qt's OpenSSL.

The USCL app writes `shadnet.cfg` the first time it hosts a PS4 game (ports 31513/31515, admin API off).
