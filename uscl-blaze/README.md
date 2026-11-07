# uscl-blaze

USCL's stand-in for **EA's Blaze** online service — the EA equivalent of what `uscl-dw` is for Activision's
Demonware. It is built on the community **Skate3BlazeServer** (skate6743/Skate3BlazeServer, no license, so USCL
doesn't vendor it, same as the Demonware base), plus the patches in `patches/` that add other EA games.

First game on it: **NBA Jam** (BLUS30696, `games/BLUS30696.md`). Skate 3 (BLUS30464) uses the same server
unmodified.

## What the patches add (on top of Skate3BlazeServer + the Skate 3 teams/invite patches)
- **0001** — accept tickets from the host's own RPCN. The upstream server only trusts official RPCN's public key;
  a self-hosted RPCN signs with its own key (`ticket_private.pem`). `settings.json` gets a `HostTicketKey` (the
  host RPCN's public key, uncompressed hex) that is trusted the same way. Also: a `fetchClientConfig` handler
  (NBA Jam asks for OSDK_* config sections Skate 3 never requests), tolerant TDF decoding (older BlazeSDK 2.x sends
  some fields with a different type), and an empty success (not an error) for commands the server doesn't implement
  — NBA Jam aborts its login on `ERR_COMMAND_NOT_FOUND`, Skate 3 doesn't send those commands.
- **0002/0003** — matchmaking for BlazeSDK 2.x, which sends no `ATTR` map (Skate 3 matchmaking is challenge-key
  based). The server makes the game itself: the first player to search hosts it, the next with the same size joins.
  Game traffic goes through the server's **UDP relay** (dirtycast failover topology), the same path Skate 3 uses, so
  it works between players behind NAT and between two instances on one PC — direct peer-to-peer can't connect there.

## Build & run
Same as the Skate 3 server (it IS that server plus these patches):
```
git clone https://github.com/skate6743/Skate3BlazeServer && cd Skate3BlazeServer
git checkout f248282
git am <skate3 teams/invite patches>        # the 14 patches USCL already carries for Skate 3
git am ../uscl-blaze/patches/*.patch         # these
dotnet publish "Skate Custom Server/Skate Custom Server.csproj" -c Release -r linux-x64 --self-contained -o out
```
`out/settings.json`: `LocalHost: true`, `LocalIPAddress` the host's IP (127.0.0.1 for local tests), and
`HostTicketKey` the host RPCN's public key. Run `out/"Skate Custom Server"` (Blaze on TCP 42100, HTTP on 80,
one UDP relay per lobby on 17000+gameId).

The game reaches it through RPCS3's IP swap list (every `gosredirector.*.ea.com` → the server), with
`PSN status: RPCN` so the game gets a ticket. See `games/BLUS30696.md`.

## Status (2026-11-07)
NBA Jam: two players sign in, matchmake into one lobby, and reach the head-to-head team-select seeing each other;
the relay carries game traffic both ways. Verified with two instances on one PC (RPCN accounts uscltest /
uscltest2). Driving both automated pads through team-select to the tip-off is a two-controller test-harness issue
(a leftover virtual pad on this PC scrambles SDL enumeration), not a game/server limitation.

## TLS for Blaze 3.x games (Madden NFL 25 — in progress)
Madden 25 is **Blaze 13.3** and runs the whole Blaze session over EA's **ProtoSSL** (legacy TLS), not plaintext.
Two pieces in `tls/`:
- `redirector.crt.pem` / `redirector.key.pem` — the forged `gosredirector.ea.com` certificate (CN matches, MD5-RSA
  signed) that old ProtoSSL accepts. From jacobtread/blaze-ssl-async (MIT). Modern TLS libs won't present it without
  a legacy profile (SSLv3, `@SECLEVEL=0`).
- `tlsproxy.py` — terminates ProtoSSL on the redirector port (42127) and forwards the plaintext Blaze stream to the
  C# server on 42100: `python3 tlsproxy.py 42127 tls 127.0.0.1 42100`.

Patch **0004** makes `getServerInstance` return the secure redirector port with `SECU=1` for Blaze 3.x+ clients
(it reads the `BSDK` version by a raw byte scan, since Madden's request uses the Blaze 3.x TDF encoding the 2.x
decoder can't parse). `settings.json` gets `SecureRedirectPort` (the TLS proxy port).

**Verified (2026-11-07):** Madden's ProtoSSL accepts the forged cert over SSLv3/AES256-SHA and sends its Blaze
redirector request (BlazeSDK 13.3.0.3.1, service `MADDEN2014_PS3`) through the proxy to our server.
**Remaining:** Madden's main Blaze session uses the **Blaze 3.x TDF wire encoding** (new type codes, e.g. type 15,
`type+length` field headers) which this server's 2.x codec does not implement — `preAuth`/login can't be decoded
yet. Full Madden online needs a Blaze 3.x codec + the 13.3 components. The TLS hop and redirector are done.
