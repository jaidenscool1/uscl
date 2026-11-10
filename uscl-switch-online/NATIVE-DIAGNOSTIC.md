# MK8 Deluxe native-menu diagnostic — 2026-11-10

Native Online Play remains **unsupported**. This change implements an opt-in
emulator diagnostic route and a request observer, not a game-facing service.
Private rooms in the JSON backend and playable Wireless Play are separate results.

## Observed locally

Mario Kart 8 Deluxe 4.0.1, title `0100152000022000`, firmware 22.5.0,
no Booster Course DLC; existing isolated preview profile, patched graphical Eden.

1. The previous log shows account-token stubs, followed by NSD returning the
   unresolved template `g2b309e01-%.s.n.srv.nintendo.net`. The resolver then
   rejects that template. There was no observed native server exchange.
2. Patch 0005 routes that **exact template**, only for this title and only with
   `USCL_MK8_NATIVE_DIAGNOSTIC=1`, to IPv4 loopback. Other names, other titles,
   and disabled launches retain their previous routing. DNS rejection rules
   are unchanged. This covers the observed explicit NSD call; the separate
   resolver TODO for requests that rely solely on `use_nsd_resolve` remains.
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
   The strict diagnostic WebSocket adapter rejected the request's key during
   the standard base64/16-byte nonce check. Its value was not logged. Whether
   this is an emulator defect or a game-specific handshake variant is unresolved.
   The game again displayed 2306-0502. No real-game WebSocket binary frame was
   observed and no authentication request was identified.

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

## Remaining protocol boundary

The next observed boundary is WebSocket nonce validation after TLS on TCP 443.
The [public PRUDP reference](https://github.com/kinnay/NintendoClients/wiki/PRUDP-Protocol#lite-format)
describes Switch WebSocket/Lite framing; the observer's original framing parser
uses that documented layout. No candidate server implementation was imported.
There is no runtime evidence yet identifying authentication schemas, NEX method
versions, PRUDP exchanges, friend methods, or native private-room operations for this build.
Do not guess these from another game's implementation or treat a JSON room test
as native compatibility. No native client has logged in, discovered/joined a
private room, or raced through this service. Windows/macOS and independent review
remain required before merge.
