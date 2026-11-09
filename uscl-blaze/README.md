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
- **0004–0014 — Blaze 3.x "heat2" (Madden 25, NHL 15).** The EA Sports titles use a newer BlazeSDK with a different
  TDF wire encoding and login handshake. These patches add: the secure (TLS) redirector + plaintext-session toggle
  (0004/0005), a full heat2 **encoder and decoder** (`Heat2Writer`/`Heat2Reader`, 0006–0008) with end-to-end tests
  (0009), and the complete **heat2 login** (0010–0012) — the real `ConsoleLoginResponse`, and the
  `UserAuthenticated` notification that actually completes the SDK's login — plus the shared **Stats** component
  (0013) and the full write-up in `docs/EA-Sports-Blaze3-heat2.md` (0014). See the "Blaze 3.x" section below.

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

## Blaze 3.x "heat2" — Madden 25 & NHL 15

Full details, the complete handshake, the live test harness, and the disassembly toolchain are in
**`docs/EA-Sports-Blaze3-heat2.md`** (added by patch 0014; it lands in the built server tree). Summary:

### TLS
These games run the Blaze session over EA's **ProtoSSL**, which only speaks **SSLv3 + RC4** — and OpenSSL 3.x
**removed both**, which silently broke the original `tlsproxy.py`. `tls/` has:
- `redirector.crt.pem` / `redirector.key.pem` — forged `gosredirector.ea.com` cert (CN matches, MD5-RSA signed),
  from jacobtread/blaze-ssl-async (MIT).
- **`tlsproxy_legacy.py`** — terminates TLS with **tlslite-ng** (pure-Python, keeps SSLv3/RC4) and monkeypatches
  tlslite's OID table to accept the forged cert's `rsaEncryption` sig-alg. Run on 42127 with
  `PYTHONPATH=<pylib>` (`tls/tlsproxy.py` only works where the system OpenSSL still has SSLv3).

Patch **0004** makes `getServerInstance` return the secure redirector with `SECU=1` for Blaze 3.x clients
(detected by a raw `BSDK` version byte-scan). Patch **0005** adds the heat2 getServerInstance encoder and a
plaintext-session toggle (`env BLAZE3_SESSION`): a bare-IP secure address makes ProtoSSL refuse TLS, so the
session is returned `SECU=0` and the game connects **plaintext** to the Blaze port; only the redirector hop is TLS.

### heat2 codec — DONE
`Heat2Writer.cs` (encode) + `Heat2Reader.cs` (decode), patches 0006–0008, tested end-to-end (0009).
`[3-byte tag][1-byte type][value]`, little-endian varints; types 0=int,1=string,2=blob,3=struct,4=list,5=map,
6=union. Connections are flagged heat2 in `PreAuthHandler`. ⚠️ `TdfUtils.EncodeTdfTag` mis-encodes **digit-leading
tags** (writes `'Q'` for `'1'`) — avoid digit tags or fix it first.

### Login — SOLVED (NHL 15 connects to the EA servers)
Patches 0010–0012. NHL 15 now logs in and reaches **"Retrieving data from server"** with the online ticker
playing — up from an instant "EA servers not available". Four things were needed:
1. **TLS** (SSLv3/RC4 via `tlsproxy_legacy.py`, above).
2. **Ticket**: `HostTicketKey` in `settings.json` = the host RPCN's uncompressed public point, so tickets our own
   RPCN self-signs validate; expiry hard-reject relaxed.
3. **`ps3Login` response = the real `ConsoleLoginResponse`** (from ZamboniDevelopment/BlazeSDK), persona under
   **`DSNM`** (the earlier DS2-derived `PNAM/DPTS` tags were unreadable — the SDK logged straight back out).
4. **`UserAuthenticated` (UserSessions notification 8)** — the local-user login-completion signal the SDK blocks
   on. Without it the client pings then `logout`s. Sent after login as **8 → 2 (UserAdded) → 5 (UserUpdated)**.

Shapes 3/4 were reverse-engineered from **`ceinydO/fifa17-ps3-online`** (FIFA 17 on RPCS3 — the best Blaze-3.x-PS3
reference). The shared **Stats** component (`getKeyScopesMap`, patch 0013) was also added.

### Remaining — one diagnosed, server-fixable NHL crash
After the full online bootstrap NHL's PPU null-derefs at a settings-manager lookup (`"stgm" → "O_SG_TCKR"`
returns null; PC `0x008b1aec`, deterministic). **Proven server-fixable, not an emulator bug** — the PPU
**interpreter** crashes identically. The `O_SG_TCKR` ticker settings group is never created; it's **not** from
config (ruled out). Next step: make the game skip the ticker (return an error for the `OsdkTicker2` component,
2249) or RE its `RegisterResponse` so the group is created. Disassembly toolchain + the full trace are in
`docs/EA-Sports-Blaze3-heat2.md`. **Madden 25** hits the same crash class earlier (FGGameSim ~52s) and now shares
this login path.
