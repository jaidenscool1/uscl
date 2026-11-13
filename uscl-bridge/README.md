# uscl-bridge: the Steam tunnel

| | |
|---|---|
| **What** | Carries every console's online traffic between the host's PC and their friends' over Steam |
| **Language** | Rust (Steamworks through `libsteam_api`) |
| **Ships in** | Windows installer and Linux AppImage; the home screen starts it |
| **Tests** | `cargo test` (CI: `.github/workflows/build.yml` on Linux, Windows, macOS) |

The host's PC runs the game's servers (or the emulator's netplay host) on 127.0.0.1. Everyone else's bridge makes
those ports appear on their own 127.0.0.1, through a Steam lobby and Steam's relay network. Nobody opens ports, and
nothing leaves Steam's network except the game itself.

It also handles sessions and invites, kicks and bans by Steam ID (`tunnel.rs`), a player's progress travelling with
them (`progress.rs`), and the PS3 ↔ PS4 crossplay frame translator (`crossplay.rs`).

## Use
Steam must be running. Start the bridge before the game (the home screen does all of this for you).
```
uscl-bridge host [preset | ports]   # host: create a session, print its number
uscl-bridge join [session id]       # join a session; with no id, accept the host's Steam invite
uscl-bridge friends                 # online Steam friends (the home screen's Friends list)
uscl-bridge selftest                # a Steam lobby round trip (Test Connection)
```

## Presets
`host` with no preset forwards RPCN (PSN for PS3). A preset adds a game's or an emulator's ports:

| Preset | Ports | For |
|---|---|---|
| `skate3` | tcp 42100, 80; udp 17001–17064 | EA Blaze ([uscl-blaze](../uscl-blaze/)) |
| `bo2` | tcp 3074, 3076; slot-udp 3074, 3077–3078 | Black Ops II PS3, our Demonware ([uscl-dw](../uscl-dw/)) |
| `bo2ps4` | `bo2` + tcp 443, 31513, 31515 | Black Ops II PS4: Demonware sign-in and shadNet ([uscl-shadnet](../uscl-shadnet/)) |
| `switch` | udp 24872 | Eden local-wireless room ([uscl-eden](../uscl-eden/)) |
| `dolphin` | udp 2626 | Dolphin NetPlay, Wii / GameCube ([uscl-dolphin](../uscl-dolphin/)) |
| `3ds` | udp 24872 | Azahar room ([uscl-3ds](../uscl-3ds/)) |

Or list ports yourself: `tcp:PORT`, `udp:FIRST-LAST`, `sudp:PORT` (a server that has to see each player at their own
"slot" address), comma-separated. Presets live in `preset()` in `src/tunnel.rs`.

## Build
```
cargo build --release      # target/release/uscl-bridge, with libsteam_api next to it
cargo test
```
