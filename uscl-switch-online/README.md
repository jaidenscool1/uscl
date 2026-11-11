# USCL Switch online service — development foundation

This is USCL's independent service, analogous in architecture to the project's private RPCN setup. It is **not yet compatible with Mario Kart's native Online Play menu**. The current playable path remains Eden Wireless Play over the existing Steam tunnel.

Implemented: protocol negotiation, temporary player identities with random bearer credentials, public and invite-only rooms, region filtering, exact title/update/DLC/adapter compatibility, member lists, capacity enforcement, leave/logout and expired-host cleanup. No Nintendo code, credentials, firmware or game assets are included. This original component is MIT licensed; the Eden client patch retains Eden's GPL license.

Not implemented: persistent accounts, a friend graph, tournaments, ranking, game-facing NEX/PRUDP services, guest account integration, race packet transport through this service, Steam peer identity binding or distribution packaging. The `hello` reply explicitly reports `native_online_play: false`, `friends: false`, and `tournaments: false`.

## Build and run

From the repository root:

```sh
cargo test --locked --manifest-path uscl-switch-online/Cargo.toml --target-dir build/switch-online-target
cargo run --locked --manifest-path uscl-switch-online/Cargo.toml --target-dir build/switch-online-target -- --port 24873
```

The listener is fixed to IPv4 loopback. `--port 0` allocates an available test port; stdout prints `USCL_SWITCH_ONLINE_READY <port>`. Request bodies and credentials are never logged. The process owns all state in memory; restart ends every identity and room. A credential identifies a temporary player, not a verified Steam identity or Nintendo account. Duplicate display names are allowed and convey no identity guarantee.

Do not publish or forward this prototype port. Before Steam integration, bind players to bridge-authenticated peers and define protected reconnect/persistence. The current Switch forward preset intentionally remains UDP 24872 only. This service does not change DNS, system certificates, Nintendo endpoint routing, guest entitlements or the existing working Wireless Play path.

## Protocol v1

One compact JSON object followed by a newline per TCP connection; one JSON reply and then close. Requests are limited to 16 KiB including the newline. Reads/writes have a five-second inactivity timeout. At most 32 connections, 256 temporary players and 64 rooms are retained. Identities expire after 120 seconds without an authenticated request; cleanup runs on subsequent requests. Host departure/expiry closes the room; host migration is not implemented.

Replies are `{"ok":true,"result":{...}}` or `{"ok":false,"error":"code"}`. Extra request fields and unknown operations are rejected. Bearer credentials must be kept private. Room discovery/member lists never contain credentials or invite secrets.

| Operation | Fields beyond `op` | Result |
| --- | --- | --- |
| `hello` | `protocol: 1` | Service, version, capabilities, explicit unsupported features |
| `register` | `name` (4–20 ASCII letters, digits, spaces, `_`, `-`, `.`; no leading/trailing spaces) | Private random `token`, lease duration |
| `heartbeat` | `token` | Renewed lease |
| `logout` | `token` | Identity removed; room membership cleared |
| `create_room` | `token`, `game`, `region`, `capacity` (2–12), `visibility` (`public` or `invite`) | Random `room` ID and private `invite` secret |
| `list_rooms` | `token`, `game`, optional `region` | Compatible public rooms; omitted region means all regions |
| `join_room` | `token`, `room`, `game`, optional `invite` | Room joined if compatible, accessible and not full |
| `room` | `token` | Current room and member display names/host flags |
| `leave_room` | `token` | Membership removed; host departure closes room |

`game` is `{"title_id":"0100152000022000","update":"4.0.1","dlc":[],"adapter":1}`. Title/DLC IDs must be 16 uppercase hexadecimal characters. DLC IDs must be sorted and unique. Update identifiers are 1–32 ASCII letters, digits, dots or hyphens. An adapter version other than 1 is rejected. Every compatibility field must match for discovery and joining. Regions are explicit 1–16 uppercase ASCII labels such as `NA` and `EU`, optionally containing hyphens; no geolocation is inferred.

Room IDs identify rooms only; credentials authorize membership operations. Invite rooms are omitted from discovery and require the invitation secret to join. Creating/joining a second room requires leaving the current one first. This service stores no game keys, firmware, external addresses, access tokens for other services or console IDs.

## Eden connection

Patch 0004 in [uscl-eden](../uscl-eden/README.md) adds a bounded, loopback-only Qt client and an explicit developer probe:

```sh
build/switch-core/bin/eden --uscl-online-probe 127.0.0.1:24873
```

It prints the negotiated capability response and exits without loading a player profile, requiring a display or booting a game. Non-loopback endpoints, invalid frames, unavailable servers and incompatible protocol replies fail. Success confirms service negotiation only, not game compatibility.

Run `python tools/dev-scripts/test-switch-online.py --eden build/switch-core/bin/eden` after building the debug backend above to exercise the real emulator/service handshake, malformed and oversized responses, incompatible versions and invalid/non-loopback endpoints. Seven Rust tests additionally cover the room model and a real two-client socket round trip. No automatic launch route or guest service is changed by the probe.

## Native game adapter work remaining

The [native diagnostic implementation and evidence](NATIVE-DIAGNOSTIC.md) establish an opt-in NSD route from the real MK8 Online Play menu to a local TCP 443 observer. Diagnostic TLS termination exposed an HTTP GET with WebSocket upgrade headers and a nonstandard three-character nonce; strict validation rejected it and the game reported 2306-0502. The observer supports a bounded, tested WebSocket upgrade/frame parser.

An original game-facing adapter now lives in [`native/`](native/README.md). It implements TLS/WebSocket termination, PRUDP Lite framing, RMC dispatch, a private ticket authority, NEX matchmaking objects backed by the JSON room service, and loopback-only NAT checks. Its thirteen tests pass. On 2026-11-10 the real game completed the native authentication flow through it (WebSocket upgrade with the three-character nonce, PRUDP Lite, login ticket, secure connection, station registration) and showed the Online Play submenu, reaching Friends → Create Room. The JSON backend remains a USCL control plane; the game speaks NEX/PRUDP, which this adapter translates.

Remaining before any native compatibility claim:

1. Complete private-room creation and join against the real game. Native login is validated; the game's Friends → Create Room flow is reached but the exact matchmaking create/join method versions and payloads have not yet completed against the adapter. Validate with runtime evidence rather than another title's guesses.
2. Fix any remaining protocol boundary the real game exposes (method versions, payload layouts, NAT addresses) using that same runtime evidence.
3. Bind identity and signaling to Steam-authenticated peers, then add a transport preset and shell-managed lifecycle. Do not trust claimed player IDs from request JSON.
4. Prove native room discovery/join and race completion with two isolated clients, followed by two PCs. Add friends, worldwide/regional policies, tournaments and durable state only with meaningful compatibility tests.

Until those stages pass, the native Online Play menu remains unsupported. Existing local Wireless Play results are recorded separately in [the game notes](../games/0100152000022000.md).
