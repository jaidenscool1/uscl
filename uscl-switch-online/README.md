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

1. Extend the recorded native-menu trace in isolated profiles and map requests to a documented private account/friend contract. The first local attempt produced 2306-0520: Eden rejected the game-server hostname with `EAI_AGAIN` after placeholder account-token calls. This is not a PC DNS configuration diagnosis. Eden currently returns a placeholder ID-token buffer in `IManagerForApplication::LoadIdTokenCache`; several authorization/license/friend handlers are absent. This source observation is not a complete runtime trace.
2. Implement a private game-facing protocol adapter. Mario Kart uses game services beyond the emulator's IPC layer; the JSON protocol above is USCL's own control plane and is not accepted by the unmodified game. Public research describes the NEX family in [NintendoClients](https://github.com/kinnay/NintendoClients/wiki). Validate the exact game's method versions and payloads rather than guessing from another title.
3. Add opt-in guest integration and emulator-owned backend credentials. Preserve normal emulator behavior when disabled. An unsupported operation must produce a clear error; reporting every service successful does not implement online play.
4. Bind identity and signaling to Steam-authenticated peers, then add a transport preset and shell-managed lifecycle. Do not trust claimed player IDs from request JSON.
5. Prove native room discovery/join and race completion with two isolated clients, followed by two PCs. Add friends, worldwide/regional policies, tournaments and durable state only with meaningful compatibility tests.

Until those stages pass, the native Online Play menu remains unsupported. Existing local Wireless Play results are recorded separately in [the game notes](../games/0100152000022000.md).
