# USCL native PS4 preview

An independently built native PS4 launcher target for USCL. Target console:
**firmware 9.00 with GoldHEN**. No console execution has been verified yet.
This is **not the complete PC application port**. Successful compilation,
package validation and desktop UI tests do not establish install, launch or
gameplay success on PS4.

The launcher runs locally using PS4 display, controller, user and application
services. It contains no PC streaming client, Qt runtime or PC emulator binary.
It reuses USCL's existing font and category artwork.

## Implemented

- Native 1920x1080 controller UI with All, PS4, PS3, Switch and other emulator categories.
- Installed application discovery from internal and external app metadata, checked
  against the installed-app service. Excludes USCL itself and system applications.
- PS4 installed-title launch requests through LncUtil with the foreground user.
- PS3 disc-folder/HDD-game/ISO discovery, and Switch NSP/XCI/NRO discovery.
- Game selection handoffs to **separately installed** RPCS3 PORT FOR PS4 v0.40
  (`RPCS30010`, `/data/rpcs3/boot.txt`) and NX on Orbis v0.1.0 (`EDPS00001`,
  `/data/edenps4/game.txt`). These are existing upstream interfaces, checked in
  their tagged source. End-to-end handoff remains untested on hardware.
- Checked atomic handoff writes; failed launch requests restore the prior selection.
- Rescan, bounded metadata/image reads, controller user switching and startup log.
- Separate desktop UI preview, sanitizer tests and package/import validation.

Selecting a game leaves its path in the corresponding emulator's launch file
after an accepted launch request. Launching that emulator again can therefore
start the last selected game. Remove its `boot.txt`/`game.txt` to restore its
normal game picker. Existing files are restored if the launch API returns an error.
An accepted request is not evidence that the emulator or game actually booted.

## Not implemented or established

- PC UI/animation/settings parity; PC updates, Discord, Steam friends/lobbies,
  USCL Steam tunnel, publisher servers and multiplayer integration.
- Bundled emulators, automatic firmware/key installation, other emulator adapters,
  automatic return to USCL from an emulator, Switch title metadata decryption,
  or speed/compatibility equivalent to the PC.
- Installation or boot on 9.00; native display/input behavior; application switching;
  emulator handoff; game compatibility, audio, speed, saves and multiplayer on PS4.

RPCS3's upstream PS4 port is an experimental pre-release: its 0.39 tests reached
menus for several games rather than stable gameplay, and 0.40 still needs console
tests. NX on Orbis was tested upstream only on a PS4 Pro with 12.02/GoldHEN;
MK8 Deluxe races ran at 13–16 fps with incorrect colors. It is not established
to run on 9.00 or at playable speed. These are upstream reports, not USCL tests:

- https://github.com/RoberixStudios/rpcs3-port-for-ps4/tree/v0.40
- https://github.com/alechurri/nx-on-orbis/tree/v0.1.0

## Build on Linux

Requires Clang/LLD, make, Python 3, git, curl, a .NET 8 SDK and host C++ compiler.
Desktop preview additionally needs SDL2 and FreeType development packages.
Bootstrap downloads the pinned open toolchain and dependencies outside the repo:

```sh
bash scripts/bootstrap.sh
make -j4 PKGTOOL="$HOME/ps4dev/pkgtool8/PkgTool.Core" package
make PKGTOOL="$HOME/ps4dev/pkgtool8/PkgTool.Core" validate test
```

If `dotnet` is not on PATH, set `DOTNET=/absolute/path/to/dotnet` for bootstrap.
Override `OO_PS4_TOOLCHAIN` and `JBC_SOURCE` when using other dependency locations.
The .NET 8 publishing tool avoids the obsolete SSL/ICU dependencies of the SDK's
bundled publishing binary. The generated package and all binaries stay in ignored
`build/`; they must not be committed to the source repository.

Output: `build/IV0000-USCL00001_00-USCLNATIVEPS4001.pkg`, plus its ELF, OELF and
`build/build-manifest.json`. `console_tested` and `pc_feature_parity` are false.
`THIRD_PARTY.md` records dependencies, including an unresolved libjbc binary
redistribution license question; the current binary is a local test artifact.

```sh
make assets build/uscl-ps4-preview
./build/uscl-ps4-preview
# Offscreen render only; does not emulate a PS4:
SDL_VIDEODRIVER=dummy ./build/uscl-ps4-preview --capture build/preview.ppm
```

Keyboard: arrows, Enter, Escape, R, F1, Page Up/Down. Preview games are explicitly
labelled fixtures and cannot launch anything on the PC.

## Console testing

See [CONSOLE-TEST.md](CONSOLE-TEST.md). Optional separate emulator downloads:

```sh
python3 scripts/fetch_emulators.py /path/outside/repository/PS4-test-packages
```

That command verifies the authors' published SHA-256 values. It does not install
packages, supply games/keys/firmware, or establish 9.00 compatibility. PS4 system
firmware and other existing game PKGs are not a replacement for a development
toolchain or a real console runtime test.
