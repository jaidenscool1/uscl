# MK8D native protocol adapter — development

Original, loopback-only adapter that accepts Mario Kart 8 Deluxe's real Online
Play traffic (TLS + WebSocket + PRUDP Lite + RMC) and maps the private
account/authentication/matchmaking subset onto the existing
[`uscl-switch-online`](../README.md) room backend. This is USCL's own
implementation; no Nintendo code, keys, firmware, or credentials are included.

**Status: not yet game-compatible.** The unit tests below exercise the wire
codec and private service in isolation. The real game's end-to-end login,
private-room discovery/join, and racing have not yet passed through this
adapter. The service still reports `native_online_play: false`.

## What it implements

- `adapter.py` — TLS WebSocket server: RFC 6455 upgrade (including the
  three-character nonce observed from MK8D), PRUDP Lite SYN/CONNECT/DATA
  handling, RMC request dispatch, and bounded, never-logged payloads.
- `auth.py` — local ticket authority and bounded NEX serialization
  (Authentication, Kerberos-style ticket issuance, SecureConnection,
  Utility, and friends/presence/competition queries that return empty sets).
- `rooms.py` — NEX matchmaking `MatchSession` read/write plus private room
  create/join/unregister/progress/codeword/leave and the host station URL
  for joiners (`GetSessionURLs`), backed by the Rust room service.
- `nat.py` — loopback-only Nintendo NAT-check responder on UDP
  127.0.0.1/127.0.0.2 ports 10025/10125.
- `identity.py` — creates/loads the `USCL_NATIVE_ID_V1` private local identity
  file shared by this adapter and the Eden account patch.

## Run

1. Start the Rust control plane (see `../README.md`):

   ```sh
   cargo run --locked --manifest-path uscl-switch-online/Cargo.toml \
     --target-dir build/switch-online-target -- --port 24873
   ```

2. Create one identity per local player and a loopback TLS certificate/key.
   Neither the identity, the key, nor the certificate may be committed.

   ```sh
   python uscl-switch-online/native/identity.py /path/to/player1.identity
   python uscl-switch-online/native/identity.py /path/to/player2.identity
   ```

3. Run the adapter for those identities:

   ```sh
   python uscl-switch-online/native/adapter.py \
     --cert localhost.pem --key localhost.key \
     --account /path/to/player1.identity --account /path/to/player2.identity \
     --control-port 24873 --nat
   ```

   `--nat` is required for the room flow: after the game creates a room it
   runs Nintendo NAT checks on UDP 10025/10125, and without the responder it
   aborts with error 2618-0201.

4. Launch patched Eden with `USCL_MK8_NATIVE_DIAGNOSTIC=1` and
   `USCL_MK8_NATIVE_CREDENTIAL_FILE=/path/to/player1.identity`, then select
   Online Play. See the Eden patches and reproduction in
   [`../NATIVE-DIAGNOSTIC.md`](../NATIVE-DIAGNOSTIC.md).

## Tests

```sh
cd uscl-switch-online/native
python3 -m unittest discover -v
```

Fourteen tests cover the connection/RPC codec, ticket authority and integrity,
NAT-check round trips, and native room creation backed by a real Rust control
plane. They do not establish game compatibility.

## Limitations and next steps

- The adapter listens only on `127.0.0.1:443`; the diagnostic route resolves
  `nncs2` to `127.0.0.2`. If the game dials the secondary auth host the
  adapter must also bind and serve `127.0.0.2`. Not yet proven necessary.
- No friend graph, tournaments, ranking, durable accounts, Steam identity
  binding, or real-game validation. Any protocol not explicitly implemented
  returns `Core::NotImplemented`; it never fabricates a successful operation.
- Routing, the credential file, and the identity are opt-in and title-pinned
  to `0100152000022000`. Do not forward port 443 or the control-plane/NAT
  ports.
