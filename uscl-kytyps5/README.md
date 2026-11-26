# uscl-kytyps5: PlayStation 5 (KytyPS5, Wolverine fork)

| | |
|---|---|
| **Console** | PlayStation 5 |
| **Upstream** | [Senaxx/KytyPS5](https://github.com/Senaxx/KytyPS5) `main`, a Marvel's Wolverine 1.05 fork of [KytyPS5/KytyPS5](https://github.com/KytyPS5/KytyPS5) (GPL-2.0) |
| **Pinned base** | `ec6c3f18` |
| **Our changes** | `patches/0001…0003`, a `git am` series |
| **Ships in** | not yet: built locally, the shell runs it from `~/.local/share/uscl/KytyPS5/` (or `USCL_KYTYPS5`) |
| **Status** | Marvel's Wolverine (`PPSA03671` 1.05) boots, plays the logos, the initial setup, the title, saves and the opening fight on Linux (RTX 4060 8 GB, i5-10600K): ~15 fps in base PS5 mode, 2–3 fps in Pro mode (8 GB is too little for it) |

## Build (Linux)
```
git clone --recursive https://github.com/Senaxx/KytyPS5.git && cd KytyPS5
git checkout ec6c3f18
git am ../patches/*.patch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DKYTY_BUILD_LAUNCHER=OFF
cmake --build build --target kyty_emulator
```
Put `build/kyty_emulator` in `~/.local/share/uscl/KytyPS5/`. Precompile the shipped shader hints once, from the shell's
working folder (`~/.config/uscl/uscl/kytyps5`) and with the arguments the shell plays with, plus
`--import-hints <repo>/assets/hints/PPSA03671.kyhints` (about 3 minutes warm, 35 cold). Without it the first visit of
each place compiles shaders while playing.

## Patches
- **0001** FidelityFX's Vulkan backend placed its effect contexts at an address short of their `alignas(32)`: Pro mode
  (FSR 3.1) crashed on Linux at the first upscaled frame (`vmovdqa` in `ffx_vk.cpp`).
- **0002** A card without Resizable BAR has a 256 MiB device-local, host-visible heap whose budget is below the fork's
  512 MiB reserve, so every image went to system memory: the logos ran at 7 fps with the GPU stalled on PCIe. Only the
  main video-memory heap is tested now (logos 60 fps, title 1.7 → 14 fps).
- **0003** `CompileProgram` freed the instructions the SRT reads point to and then validated through them (a
  use-after-free that stopped the precompile with `index >= self.NumArgs()` at the title).

## What the shell does for PS5 games
- Arguments: `--game <dir> --fullscreen --shader-cache --hide-cursor --amd-cpu --readback-linear-images --tessellation
  --trinity false` (`--redzone` too on Windows), as the fork's own launcher but in base PS5 mode; `--amd-cpu` emulates the PS5's AMD-only instructions on any
  host and gives the game hooks their code area.
- Dump repair before each start: some dumps reach the PC through a copy that appends a guessed extension
  (`d/0/userinterface.txt`, `d/*/wem*.wav`, `sce_sys/*.dds.dds`, `snd0.at9.wav`, `pkg_settings.set.txt`). The game then
  finds no UI archive (black screen after the logos, the "stuck after the Marvel Games logo" reports), no audio streams
  (cinematics stall) and no `pkg_settings.set` (crash at boot). The shell hard-links each file to its real name; nothing
  is renamed or removed.

## Known open issues
- Pro mode (the fork's default) needs more than 8 GB of video memory, so the shell plays base mode; a Pro setting for
  larger cards is still to do.
- "shader resource specialization failed: SRT refresh" lines appear in play (not yet looked into).
- KytyPS5 contribution rules: each upstream PR is written and posted by a human, with AI use disclosed.
