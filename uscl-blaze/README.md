# uscl-blaze: EA Blaze (Skate 3, NBA Jam, NHL, Madden)

| | |
|---|---|
| **Console** | PlayStation 3 |
| **Upstream** | [skate6743/Skate3BlazeServer](https://github.com/skate6743/Skate3BlazeServer) (C#, no license, so USCL doesn't vendor it) |
| **Pinned base** | `f248282` + the 14 Skate 3 teams/invite patches USCL carries |
| **Our changes** | `patches/0001`–`0015`, applied with `git am`; `tls/` (TLS terminator for Blaze 3.x games) |
| **Runs on** | the host's PC; players reach it through [uscl-bridge](../uscl-bridge/) (`skate3` preset) |

The stand-in for **EA's Blaze** online service: what [uscl-dw](../uscl-dw/) is for Activision's Demonware. It is
the community Skate 3 server plus patches that add other EA games, including the newer Blaze 3.x ("heat2") titles.

## Build and run
```
git clone https://github.com/skate6743/Skate3BlazeServer && cd Skate3BlazeServer
git checkout f248282
git am <skate3 teams/invite patches>        # the 14 patches USCL already carries for Skate 3
git am ../uscl-blaze/patches/*.patch         # these
dotnet publish "Skate Custom Server/Skate Custom Server.csproj" -c Release -r linux-x64 --self-contained -o out
```
`out/settings.json`: `LocalHost: true`, `LocalIPAddress` the host's IP (127.0.0.1 for local tests), and
`HostTicketKey` the host RPCN's public key. Run `out/"Skate Custom Server"` (Blaze on TCP 42100, HTTP on 80, one
UDP relay per lobby on 17000+gameId).

The game reaches it through RPCS3's IP swap list (every `gosredirector.*.ea.com` → the server), with
`PSN status: RPCN` so the game gets a ticket. Blaze 3.x games also need the TLS terminator (below).

## Patches
- **0001**: accept tickets from the host's own RPCN. Upstream only trusts official RPCN's key; `settings.json`
  gets a `HostTicketKey` (the host RPCN's public key, uncompressed hex) trusted the same way. Also a
  `fetchClientConfig` handler (NBA Jam asks for OSDK_* sections), tolerant TDF decoding (BlazeSDK 2.x sends some
  fields with another type), and an empty success instead of `ERR_COMMAND_NOT_FOUND` for unimplemented commands
  (NBA Jam aborts its login on that error).
- **0002–0003**: matchmaking for BlazeSDK 2.x, which sends no `ATTR` map. The first player to search hosts, the next
  with the same size joins. Game traffic goes through the server's **UDP relay** (dirtycast failover), like Skate 3,
  so it works behind NAT and between two copies on one PC.
- **0004–0005**: Blaze 3.x redirector: `getServerInstance` returns the secure redirector (`SECU=1`) for Blaze 3.x
  clients (detected by a `BSDK` version byte-scan), with a heat2 encoder and a plaintext-session toggle
  (`BLAZE3_SESSION`): the session is returned `SECU=0` and only the redirector hop is TLS.
- **0006–0009**: the heat2 codec, `Heat2Writer` (encode) and `Heat2Reader` (decode), with end-to-end tests.
- **0010–0012**: the heat2 login: the real `ConsoleLoginResponse` and the `UserAuthenticated` notification.
- **0013**: the shared **Stats** component (`getKeyScopesMap`).
- **0014**: the full write-up, `docs/EA-Sports-Blaze3-heat2.md` (it lands in the built server tree).
- **0015**: **OSDKSettings** (component 2249): the settings groups, ticker settings and user views NHL 15 looks up.

## Status
| Game | Blaze | Where it is |
|---|---|---|
| Skate 3 (`BLUS30464`) | 2.x | the upstream server, unmodified ([notes](../games/BLUS30464.md)) |
| NBA Jam (`BLUS30696`) | 2.11 (heat1) | two players sign in, matchmake into one lobby and reach team select; the relay carries game traffic both ways (two copies on one PC, 2026-11-07) ([notes](../games/BLUS30696.md)) |
| NHL 15 (`BLUS31436`) | 3.19 (heat2) | **logs in and connects**; past the ticker crash (patch 0015), next stop is the OSDK user settings ([notes](../games/BLUS31436.md)) |
| Madden NFL 25 (`BLUS31178`) | 13.3 (heat2) | shares NHL's login; hits an RPCS3 FGGameSim crash (~52 s) first ([notes](../games/BLUS31178.md)) |

## Blaze 3.x "heat2" (NHL 15, Madden 25)
Full details, the handshake, the live test harness and the disassembly toolchain are in
`docs/EA-Sports-Blaze3-heat2.md` (patch 0014).

### TLS
These games run Blaze over EA's **ProtoSSL**, which only speaks **SSLv3 + RC4**. OpenSSL 3.x removed both. `tls/`:
- `redirector.crt.pem` / `redirector.key.pem`: the community `gosredirector.ea.com` certificate (CN matches,
  MD5-RSA signed), from jacobtread/blaze-ssl-async (MIT).
- **`tlsproxy_legacy.py`**: terminates TLS with **tlslite-ng** (pure Python, keeps SSLv3/RC4) and accepts the
  certificate's `rsaEncryption` signature algorithm. Run on 42127 with `PYTHONPATH=<pylib>`. (`tls/tlsproxy.py` only
  works where the system OpenSSL still has SSLv3.)

### heat2 codec
`[3-byte tag][1-byte type][value]`, little-endian varints; types 0=int, 1=string, 2=blob, 3=struct, 4=list, 5=map,
6=union. Connections are flagged heat2 in `PreAuthHandler`. ⚠️ `TdfUtils.EncodeTdfTag` mis-encodes
**digit-leading tags** (writes `'Q'` for `'1'`): avoid digit tags or fix it first.

### Login
Four things were needed for NHL 15 to log in (it used to fail at once with "EA servers not available"):
1. **TLS**: SSLv3/RC4 through `tlsproxy_legacy.py`.
2. **Ticket**: `HostTicketKey` = the host RPCN's uncompressed public point, so tickets our RPCN signs validate;
   the expiry hard-reject is relaxed.
3. **`ps3Login` response = the real `ConsoleLoginResponse`** (from ZamboniDevelopment/BlazeSDK), persona under
   **`DSNM`**. The earlier `PNAM/DPTS` tags were unreadable and the SDK logged straight back out.
4. **`UserAuthenticated`** (UserSessions notification 8), the login-completion signal the SDK waits for, sent after
   login as **8 → 2 (UserAdded) → 5 (UserUpdated)**. Without it the client pings, then logs out.

Shapes 3 and 4 were worked out from [ceinydO/fifa17-ps3-online](https://github.com/ceinydO/fifa17-ps3-online), the
best Blaze 3.x PS3 reference.

### Remaining: one NHL crash (server-fixable)
After the online bootstrap NHL's PPU dereferences null at a settings lookup (`"stgm" → "O_SG_TCKR"`, PC
`0x008b1aec`, every time). Not an emulator bug: the PPU interpreter crashes the same way. The ticker settings group
`O_SG_TCKR` is never created, and it doesn't come from config. Next: answer the `OsdkTicker2` component (2249) with
an error so the game skips the ticker, or work out its `RegisterResponse` so the group gets created.
