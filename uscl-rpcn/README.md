# uscl-rpcn

USCL's local RPCN server: [RipleyTom/rpcn](https://github.com/RipleyTom/rpcn) (AGPL-3.0) plus the patches in
`patches/`. The patched server is AGPL-3.0 as well.

## Build
```
git clone https://github.com/RipleyTom/rpcn.git && cd rpcn
git checkout 7a3c3c4            # the commit these patches were made against
git am ../patches/*.patch
cargo build --release
```

### Windows (tested 2026-10-22, Windows 11, patches 0001-0004)
Rust's GNU toolchain plus a few build tools (all via winget): `rustup` with host `x86_64-pc-windows-gnu`,
`BrechtSanders.WinLibs.POSIX.MSVCRT` (gcc, dlltool), `Google.Protobuf` (protoc) and
`StrawberryPerl.StrawberryPerl`. The vendored OpenSSL won't build: its `mingw64` target needs a Unix-style Perl,
and Git's lacks modules. So build against Strawberry's OpenSSL instead, from Git Bash with WinLibs first in PATH:
```
OPENSSL_NO_VENDOR=1 OPENSSL_DIR='C:\Strawberry\c' PROTOC=<path to protoc.exe> cargo build --release
cp /c/Strawberry/c/bin/{libcrypto-3-x64__,libssl-3-x64__,zlib1__}.dll target/release/
```
`rpcn.exe` loads those three DLLs at start (without them it exits with 0xC0000135). The run steps below work
the same from any folder (edit `rpcn.cfg` by hand if there's no `sed`). With `USCL_LOCAL=1` it listens on
TCP 127.0.0.1:31413 and UDP 127.0.0.1:3757, and `uscl-bridge host` in front of it passes connections through.

## Run (host only)
The Steam lobby owner runs this; everyone else only runs `uscl-bridge join`.
```
mkdir -p ~/.local/share/uscl/rpcn && cd ~/.local/share/uscl/rpcn
cp <rpcn>/{rpcn.cfg,servers.cfg,scoreboards.cfg,server_redirs.cfg} .
sed -i 's/^Host=0.0.0.0/Host=127.0.0.1/; s/^Port=31313/Port=31413/; s/^HostIPv6=.*/HostIPv6=none/' rpcn.cfg
<rpcn>/target/release/rpcn --cert-gen      # once: self-signed cert.pem/key.pem
USCL_LOCAL=1 <rpcn>/target/release/rpcn    # TCP 31413, UDP 3757; uscl-bridge owns 31313/3657
```
`HostIPv6=none` makes the UDP port fall back to IPv4 127.0.0.1. Bound to `::1` it can't receive IPv4 packets.

## Bridge (every player, the host included)
```
uscl-bridge host            # host: creates the session and prints its number
uscl-bridge host skate3     # same, and forwards a community server's ports from members to this PC (games/BLUS30464.md)
uscl-bridge join <number>   # others; or plain `uscl-bridge join` and accept the host's Steam invite
```
Steam must be running. Start the bridge before the game.

## RPCS3 side
In RPCN settings (`rpcn.yml`): `Host: 127.0.0.1`, any NPID/password. The first login creates the account
(`USCL_LOCAL=1`). Tested 2026-10-22: official RPCS3 0.0.43-20372 + Minecraft PS3 (BLUS31426) logs in through
`uscl-bridge host`, and the UDP signaling reply arrives. Use a separate RPCS3 config dir
(`XDG_CONFIG_HOME=...`) so the player's real config is untouched.

## Patches
- 0001: with `USCL_LOCAL` set, the first login creates the account.
- 0002: auto-created accounts get RPCS3's default avatar URL. RPCS3 parses the avatar with
  `get_string(false)` and treats an empty one as a malformed login reply.
- 0003: with `USCL_LOCAL`, no LAN-address swap (every player is 127.0.0.1 with their own port) and UDP on
  3757, because the bridge owns 3657.
- 0004: with `USCL_LOCAL`, a new account is friends with every other account on the server (everyone there is
  in the same Steam session; Minecraft only lists friends' worlds). Online players are notified.
