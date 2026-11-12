# MK8 Deluxe native-menu diagnostic — 2026-11-10

Native Online Play remains **unsupported**. This work implements an opt-in
emulator diagnostic route, a request observer, and an original game-facing
adapter in [`native/`](native/README.md); none of them is yet a validated
game-facing service. Private rooms in the JSON backend and playable Wireless
Play are separate results.

## Observed locally

Mario Kart 8 Deluxe 4.0.1, title `0100152000022000`, firmware 22.5.0,
no Booster Course DLC; existing isolated preview profile, patched graphical Eden.

1. The previous log shows account-token stubs, followed by NSD returning the
   unresolved template `g2b309e01-%.s.n.srv.nintendo.net`. The resolver then
   rejects that template. There was no observed native server exchange.
2. Patch 0005 routes the **exact observed templates**, only for this title and
   only with `USCL_MK8_NATIVE_DIAGNOSTIC=1`, to IPv4 loopback: the game server
   and `nncs1` to 127.0.0.1, `nncs2` to 127.0.0.2. It applies through both NSD
   and the socket DNS resolver. Other names, other titles, and disabled launches
   retain their previous routing. Patch 0006 additionally parses a private
   `USCL_NATIVE_ID_V1` identity from `USCL_MK8_NATIVE_CREDENTIAL_FILE` and
   reports a loopback IP address under the same opt-in.
3. Selecting Online Play in the real game selected this route. A local observer
   on TCP 443 received a 1,551-byte first chunk beginning with a TLS record
   header declaring 1,546 bytes. This initial observer did not decrypt TLS.
4. The observer closed the connection without replying. The game displayed
   **2306-0502** (server communication error), replacing the previous
   **2306-0520** lookup-path failure. This is expected diagnostic failure,
   not a successful login.
5. With an ephemeral, local diagnostic TLS certificate, the observer received a
   156-byte HTTP GET to `/`. A subsequent run confirmed `Upgrade: websocket`,
   `Connection: Upgrade` and WebSocket version 13. The guest requested verification
   option 0 in Eden's existing SSL API; no verification-policy patch was made.
   The strict diagnostic WebSocket adapter rejected the request's key because
   the game sent a three-character nonce rather than the standard 24-character
   key; the value was not logged. The observer now accepts that nonce only with
   an explicit `--mk8-short-nonce` option. The game again displayed 2306-0502.
   No real-game WebSocket binary frame was observed and no authentication
   request was identified.

REA 6.8.0 `capture-process` Evidence:
`ev_3aea0a136fb5f694883e081e8dd3fa71376da48eb856845c56407efc6fd59812`.
REA captured the owned game's process lifecycle, not network packets or UI actions.
The externally performed keyboard actions, isolated Eden log, metadata-only
observer output, and screenshot supply the separate runtime observations above.
The process required SIGKILL after SIGTERM left it open; REA recorded signal 9
and quiesced settlement. This was not a clean emulator shutdown.

Further REA process captures (same limitations): TLS request run
`ev_81bb522f41aa9ac9fda3cbb34cbb0176d79160362945e7c3b1a98095873b9fdd`;
WebSocket rejection run
`ev_212b50694f0f13be98517ffc4b82b6be0b744bf2e02deda83d9f3b8f0ce2aef4`.
The latter reached its bounded timeout, then REA sent SIGTERM and SIGKILL.

The full Evidence, logs and screenshots remain private under `/tmp` or ignored
build paths. No full capture is committed. REA reports sampled process trees,
inherited environment uncertainty and unknown filesystem effects; its capture
does not prove protocol compatibility. The installed plugin had no exposed MCP
tools and its bare launcher lacked dependencies. The dependency-complete,
version-pinned CLI supplied the actual Evidence; client registration was not changed.

## Reproduce in an isolated profile

Build/apply the five Eden patches as described in `../uscl-eden/README.md`.
Before launching the game, start the observer on the observed destination port:

```sh
python tools/dev-scripts/observe-switch-native.py --port 443 --duration 120
```

The host must permit binding that port. The observer binds only IPv4 loopback,
listens on TCP and UDP, expires after the selected duration, bounds concurrent
connections and reads, and emits only byte counts/framing categories. It never
prints peer addresses, payloads, raw paths/headers, tokens or TLS material.
HTTP metadata includes only a fixed-word path shape, method and WebSocket
header checks; arbitrary path components and query values are omitted.
Without TLS/WebSocket options it does not answer requests. First-chunk
framing is not complete-message parsing. Zero-byte observations do not establish
a successful handshake or identify which local process connected.
Do not forward this port or the separate control-plane port through Steam.

Optional `--cert <local.pem> --key <local.key>` terminates diagnostic TLS;
`--websocket` additionally validates a root-path RFC6455 upgrade, sends the 101
response and observes one masked, unfragmented binary frame of at most 4 KiB.
Certificates and private keys must stay outside Git. These options do not
change emulator certificate trust or authentication. No PRUDP response is sent.
This deliberately limited observer is not a production WebSocket server.

Use the isolated profile environment from the existing preview and add
`USCL_MK8_NATIVE_DIAGNOSTIC=1` **only to the diagnostic Eden process**. Launch
the game normally and select Online Play. Do not enable this in normal USCL
launches. Unset the variable when done; there is no persistent setting.

```sh
python tools/dev-scripts/test-switch-native-diagnostic.py --eden-source /path/to/patched/eden
```

Checks cover actual TCP/UDP observation, oversized input, finite idle lifetime,
metadata-only output, and the compiled title/endpoint routing allowlist. Existing
seven backend tests and the real Eden control-plane probe also pass. Linux only;
the graphical Eden build passed with existing fmt deprecation warnings.
The regenerated patch series reproduces the source HEAD tree from the pinned base.
Additional tests cover a split WebSocket upgrade, the RFC accept digest,
masked/unmasked frame handling and a synthetic PRUDP Lite header. Passing that
synthetic frame is not evidence of receiving PRUDP from the real game. Optional
`--tls-cert <localhost.pem> --tls-key <localhost.key>` on the test script exercises
real TLS with a verified local certificate, including request-value redaction.

## Current adapter and remaining boundary

The [`native/`](native/README.md) adapter now terminates TLS/WebSocket, accepts
the observed short nonce, and implements PRUDP Lite framing, RMC dispatch, a
private ticket authority, NEX matchmaking objects backed by the JSON room
service, and loopback-only NAT checks. Its fourteen unit tests pass.

## Native login — validated 2026-11-10

With Eden patches 0005/0006, a private identity file, the local adapter on TCP
443, the JSON control plane on 24873, and the
[`tools/dev-scripts/dsu-pad.py`](../../tools/dev-scripts/dsu-pad.py) DSU pad
server on UDP 26760 (plus `USCL_UDP_POLL_MS` set fine enough to navigate the
menus), the real MK8 Deluxe 4.0.1 Online Play menu was reached and the game
completed the native authentication flow against the local adapter. Observed
in order: WebSocket upgrade with the three-character nonce, PRUDP Lite
SYN/CONNECT/DATA, RMC `Authentication::Login`, a private ticket issued, the
secure connection, station registration, utility integer settings, and
matchmaking/data-store queries. The game then displayed the Online Play
submenu (Worldwide / Regional / Friends / Tournaments) and reached the
Friends → Create Room screen.

This validates native account/authentication compatibility. It does not yet
validate private-room creation/join or racing: those methods were reached by
the game's UI but have not completed against the matchmaking adapter.

## Native room creation — validated 2026-11-10

Confirming the create-room prompt now drives the full matchmaking room flow.
Observed in order: `CreateMatchmakeSessionWithParam` (protocol 109, method 38)
creates the private room (`native_private_room_created`), the game asks for a
relay signature key (protocol 3, method 6), the adapter answers with a "no
relay offered" payload, the game runs its NAT checks against the loopback
responder (`--nat`, UDP 10025/10125), reports NAT properties (protocol 3,
method 5), updates room progress (protocol 109, method 34), and requests the
room code (protocol 109, method 68). The game then shows the room lobby.
Without the NAT responder the game aborted the same flow with 2618-0201.

This validates private-room creation. A second client joining via the room
code and a completed race remain unverified. Eden patch 0008 implements
`ISslConnection::Peek`, which the game calls during this flow and which
previously threw an unimplemented-function assertion.

The [public PRUDP reference](https://github.com/kinnay/NintendoClients/wiki/PRUDP-Protocol#lite-format)
describes Switch WebSocket/Lite framing; the adapter uses that documented
layout but was written against the observed headers, not an imported server
implementation. The exact NEX method versions for private-room
discovery/join and racing are still unverified against the real game, and the
matchmaking methods the adapter implements were not imported from another
title. Do not treat the JSON room test as native compatibility. No native
client has created/joined a private room or raced through this service.
Windows/macOS and independent review remain required before merge.

## Native room join — server side validated on Windows (2026-11-11)

Two patched Eden instances on one Windows 10 PC (MSVC build, MK8 Deluxe 4.0.1,
firmware 22.5.0), one adapter, two private identities
(`tools/dev-scripts/switch-native-test.ps1`). Observed in order:

1. Host: login, `CreateMatchmakeSessionWithParam` (109/38), relay key (3/6),
   NAT checks, NAT report (3/5), progress (109/34), room ID (109/68); the
   lobby shows the 5-character ID.
2. Joiner: types the ID; 109/69 resolves it to the gathering,
   `FindMatchmakeSessionBySingleGatheringId` (109/49) returns the room,
   `JoinMatchmakeSessionWithParam` (109/39) joins it, `GetSessionURLs` (21/41)
   returns the host's station, then 3/6 and 3/5.
3. The joiner then shows **2618-0502** ("other consoles are not responding").

Fixes found on the way:
- Eden patch 0009: Windows reports an ICMP port-unreachable as WSAECONNRESET
  on the next UDP receive, and the host dropped its server connection right
  after the NAT checks. `SIO_UDP_CONNRESET` is now off for guest UDP sockets.
- The lobby shows the 109/68 value and Search by Room ID takes 5 characters
  (`ABCDEFGHIJKLMNPQRSTUVWXYZ23456789`); the MatchmakeSession codeword stays
  12 digits. Keeping both, separately, works.
- The game registers
  `prudp:/address=127.0.0.1;port=1;Pl=3;Tpt=2;natf=0;natm=0;pmp=0;sid=30;upnp=0`.
  Port 1 is a placeholder. The game's Pia socket sends the NAT checks, so the
  adapter records that source port and the reported natm/natf and hands them
  out in 21/41. `Pl` and `Tpt` are kept.
- Other players' 3001 notifications are pushed immediately instead of waiting
  for the target's next packet.

### Remaining boundary: ports 33334/33335 on the NAT-check host

Neither game ever sends to the other's station. Both send, from their Pia
socket, to the NAT-check host (127.0.0.1 under the diagnostic route):
- port 33334: five 16-byte all-zero packets, before the 10025 checks;
- port 33335: Pia packets (magic `32ab9864`). Type 1, 76 bytes, contains
  `NATTestId_Dummy` and repeats without a reply. After a join the host sends
  84-byte type-2 (encrypted) packets there.

These ports are not in Eden's source, and the public NAT-check and Pia pages
do not document them. An experiment that echoed 33334 and forwarded 33335
packets between the two games delivered packets both ways, but the joiner
still failed with 2618-0502. 33335 is probably a Nintendo relay or
connection-test service whose replies the game checks. Its protocol, and
whether the Pia session key comes from the MatchmakeSession key the adapter
generates, must be worked out before native races can work. The experiment
is not committed.

Windows notes: two Eden instances loading pipeline caches at once crashed one
with 0xc0000005, so the launcher starts them 8 s apart. Each instance needs its
own portable `user` folder, and its saved absolute paths are rewritten per
player.

### Progress 2026-11-11 (later): NAT traversal succeeds, connection request fails

Driven end to end by script (`switch-native-test.ps1 -Pad`, DSU pads per player). Fixes that got here:
the Pia relay host `g2b309e01-%.p.srv.nintendo.net` routed to loopback (0005); each console gets its own
loopback address, `USCL_MK8_NATIVE_LOCAL_ADDRESS=127.0.0.1x` (0006 NIFM, 0010 socket binding), and the
adapter advertises the client's real source address, so Pia no longer treats both players as behind one NAT;
reserved Windows port ranges now reach the game as EADDRINUSE (0011) instead of a silent success; the
adapter answers `ReplaceURL` (11/7), relays `RequestProbeInitiationExt` (3/3) as `InitiateProbe` (3/2) to
the host, and accepts traversal reports (3/4, 3/7).

Result: the joiner reports `ReportNATTraversalResult` ok, resolves and tries the host address, then fails
immediately in `ConnectStationJob::SendConnectionRequest` (main `0x9a27a4`): either the virtual check at
vtable+0x40 or the send helper `0x9a2908` (station lookup `0x9b2dfc`, packet build `0x9b170c`/`0x9b1954`)
returns failure. That is the next thing to trace.
