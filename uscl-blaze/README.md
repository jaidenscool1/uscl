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

**Captured the real heat2 wire (2026-11-07).** `tlsproxy.py` takes an optional 5th arg — a capture file — that dumps
the decrypted frames both directions. `heat2.py` decodes them. A sample getServerInstance exchange is checked in at
`tls/madden-getserverinstance.hex`. From it:
- **heat2 field format** (what the codec must implement): `3-byte tag + 1-byte type`; string = `varint length +
  bytes (incl NUL)`; int = Blaze varint; types seen: 0=int, 1=string, 6=union. Madden's request decodes cleanly.
- **The blocker is now the *response*, not the request.** Our getServerInstance reply is still 2.x-encoded (first
  field type byte `0xd0`, invalid for heat2), so Madden can't read the `ServerAddress`/`SECU` — it falls back to
  `127.0.0.1:443`, finds nothing, and never reaches `preAuth`.

**Redirector + session reached (2026-11-07, patch 0005).** Two changes got Madden into the Blaze handshake:
- **heat2 getServerInstance encoder** (`Heat2Writer.cs`): the 2.x model encoder produced heat1 Madden couldn't parse.
  The response is now hand-built in heat2 (`3-byte tag + full type byte`; little-endian varint, unlike heat1's
  big-endian `EncodeVlq`): `ADDR` union → `VALU{IP,PORT}`, `SECU`, `XDNS`.
- **plaintext session (SECU=0)**: a secure address given as a bare IP makes ProtoSSL refuse to start TLS (no CN to
  validate), so the session is returned as **SECU=0** and Madden connects **plaintext** to the Blaze port (42100).
  Only the redirector hop stays TLS. (`env BLAZE3_SESSION=tls:<port>|plain:<port>` keeps the toggle.)

Madden now connects 42127 (redirector) → 42100 (session, plaintext), sends `preAuth` + `fetchClientConfig OSDK_*`,
and shows **"connecting to the EA Servers"**. Real session capture: `tls/madden-session-plaintext.hex`.
**Remaining:** a heat2 **codec for the session** (the server still answers in heat1, so Madden stalls). The session
commands are the same shape as NBA Jam's, so the handlers just need heat2 decode/encode. NHL 15 rides the same codec.
