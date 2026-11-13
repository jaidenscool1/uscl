# Packaging USCL

One program: the home screen (`uscl-shell`, what USCL opens to), the emulators, the online servers and
`uscl-bridge` next to it, and the server data they start from.

| | Windows installer | Linux AppImage |
|---|---|---|
| Home screen, `uscl-bridge` | ✅ | ✅ |
| PS3: our RPCS3 (`uscl-rpcs3`, with the **USCL > Online** panel), `uscl-rpcn`, `dw-server` (uscl-dw) | ✅ | ✅ |
| PS4: our shadPS4 + shadNet (`uscl-shadps4`, `uscl-shadnet`) | not yet | when built with them (`USCL_SHADPS4_BUILD` / `USCL_SHADNET_BUILD`) |
| Switch: our Eden core (`uscl-eden`) | not yet | not yet (build it with `tools/dev-scripts/build-switch-core.sh`) |
| Wii / GameCube, 3DS, Wii U: Dolphin, Azahar, Cemu | not yet | no: USCL starts the player's Flathub apps |

No games, console keys or firmware are ever packaged.

## Linux: AppImage
```
USCL_RPCS3_BUILD=<rpcs3 build dir> USCL_RPCN_SRC=<uscl-rpcn checkout> USCL_DW_SRC=<uscl-dw checkout> packaging/build-linux.sh
```
Output `build/USCL-x86_64.AppImage`. The RPCS3 build dir is a configured `cmake -G Ninja` Release build of RPCS3
with our patches (see `uscl-rpcs3/README.md`). Needs Qt 6's `qmake6`, cargo and python3; linuxdeploy and
appimagetool are downloaded to `~/.cache/uscl-build`.

The AppImage starts the home screen; `USCL-x86_64.AppImage --classic` opens the RPCS3 window, and a game file
as the argument boots it in RPCS3. Players install it with `packaging/linux/install.sh USCL-x86_64.AppImage` (copies it to `~/Applications`, adds
USCL to the app menu; `--remove` undoes that), or just run the AppImage.

## Windows: installer
`.github/workflows/windows-installer.yml` (run it from the Actions tab, or push a `v*` tag) builds RPCS3 with our
patches using RPCS3's own Windows CI steps, the servers with cargo, and `USCL-Setup.exe` with Inno Setup
(`windows/uscl.iss`). The installer needs no admin rights (installs to `%LOCALAPPDATA%\Programs\USCL`), adds Start
menu and desktop shortcuts and installs the Visual C++ runtime if it's missing.

## Where things go
- Settings, games, saves: `~/.config/uscl` (Linux), the install folder (Windows). Separate from a normal RPCS3.
- Server data (RPCN accounts, Demonware storage): `uscl/` inside the settings folder, created on first Host.
- RPCN and Demonware only listen on 127.0.0.1; friends reach them through the Steam connection.

## Releases and updates
USCL updates itself from this repo's GitHub releases (Settings → System Update). Every update must be signed:
the release's `<file>.sha256` needs a `<file>.sha256.sig` made with the USCL update key (Ed25519), and USCL
checks it against the public key built in (`USCL_UPDATE_KEY` in `uscl-shell/CMakeLists.txt`) before it
downloads anything, then checks the download against the checksum. Unsigned or altered releases are refused.

1. Push a `v*` tag: the Windows workflow builds `USCL-Setup.exe` and uploads it as a **draft** release.
2. On the maintainer's PC: `packaging/sign-release.sh v0.4.0 build/USCL-x86_64.AppImage` adds the AppImage,
   verifies each file against its checksum, signs the checksums and publishes the release.

The private key is `~/.config/uscl-signing/update-key.pem` on the maintainer's PC and nowhere else (not in
GitHub, not in CI), so a compromised repo or CI run can't ship an update USCL would install. **Back it up**
somewhere offline: without it no further update can be published to existing installs (a new key means
everyone reinstalls once).
