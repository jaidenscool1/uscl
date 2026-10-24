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

## Run (per player, local only)
```
mkdir -p ~/.local/share/uscl/rpcn && cd ~/.local/share/uscl/rpcn
cp <rpcn>/{rpcn.cfg,servers.cfg,scoreboards.cfg,server_redirs.cfg} .
sed -i 's/^Host=0.0.0.0/Host=127.0.0.1/; s/^HostIPv6=.*/HostIPv6=none/' rpcn.cfg   # localhost, IPv4 UDP
<rpcn>/target/release/rpcn --cert-gen      # once: self-signed cert.pem/key.pem
USCL_LOCAL=1 <rpcn>/target/release/rpcn
```
`HostIPv6=none` makes the UDP signaling port fall back to IPv4 127.0.0.1. Bound to `::1` it can't receive
RPCS3's IPv4 packets.

## RPCS3 side
In RPCN settings (`rpcn.yml`): `Host: 127.0.0.1`, any NPID/password. The first login creates the account
(`USCL_LOCAL=1`). Tested 2026-10-22: official RPCS3 0.0.43-20372 + Minecraft PS3 (BLUS31426) logs in
("You are now logged in RPCN"), using a separate RPCS3 config dir (`XDG_CONFIG_HOME=...`) so the player's
real config is untouched.

## Patches
- 0001: with `USCL_LOCAL` set, the first login creates the account.
- 0002: auto-created accounts get RPCS3's default avatar URL. RPCS3 parses the avatar with
  `get_string(false)` and treats an empty one as a malformed login reply.
