# Switch 2 emulation and CPU/GPU translation tracker

Last reviewed: 2026-11-18. Scope: USCL's Eglantine integration and its pinned
`30181ba05b484a4beefa838457b02d6ab5aa7baa` base. Translation means guest CPU
instructions and GPU commands/shaders translated for the host. No completion
percentage is assigned: a boot or benchmark does not establish broad compatibility.

## Release baseline versus pending development

The release baseline uses patches 0001–0008 plus the theme-loading patch.
[PR #18](https://github.com/jaidenscool1/uscl/pull/18) contains additional
performance/correctness work; its results below are development evidence, not
features shipped in this baseline. The PR's body predates some of its commits;
consult its current files before relying on the earlier summary.

| Area | Baseline evidence | Remaining work / next acceptance check |
|---|---|---|
| Launch and input | USCL routes `04` program IDs to Eglantine; automatic controller mapping is patch 0001 | Test physical controllers, motion input and Windows gameplay |
| CPU translation (AArch64 JIT) | Patch 0005 removes unused perf-map overhead (17.8 to 16.8 µs/block in the documented test). Patch 0006 shares per-process JIT cache: about 510k compilations rather than 1.55M for 511k distinct blocks in the measured MKW boot-to-race run | Exercise cross-core invalidation and multiple games; compare `USCL_SHARED_JIT=0` under the same workload |
| GPU texture translation/upload | Patches 0002–0004 avoid overwritten uploads, use host-cached staging on NVIDIA, and parallelize large unswizzles. Round-trip output was byte-identical in the documented test | Repeat on AMD/Intel and Windows; inspect streamed terrain and long-session memory use |
| GPU sparse resource ordering | Patch 0007 replaces a CPU fence wait with a GPU semaphore attached to the next submission | Stress streaming with capture/probes both enabled and disabled; compare visual output and validation errors |
| GPU command and shader translation | MKW races render via Eglantine's Vulkan backend; the GPU back thread remains the measured bottleneck (~18.5 ms/frame in the baseline optimized run) | Reduce per-draw binding/descriptor setup; measure CPU JIT and GPU back-thread time independently against 16.7 ms |
| Frame presentation / command snapshots | Baseline has a reproducible Switch Sports freeze. Pending #18 adds present-grace and command-list snapshot fixes | Replay the complete combined patch series; build on both platforms; verify intro-to-gameplay and long races |
| Build optimization | Patch 0008 enables optional video_core-only ThinLTO. Whole-program ThinLTO was rejected after a fiber/thread-local-related crash | Keep release builds portable; identify compiler, PGO profile and flags in every comparison |
| Online translation / transport | Offline only; no Eglantine `--uscl-room` integration | Port Eden room support, then demonstrate a two-PC session; no online compatibility claim yet |

## Game evidence

Measurements below come from [the fork notes](../uscl-eglantine/README.md),
[GAMES.md](../GAMES.md) and [PR #18's current branch](https://github.com/jaidenscool1/uscl/tree/agent/jaiden-pc/switch2-grace/uscl-eglantine).
The reported PC is an i5-10600K with RTX 4060 8 GB on Linux, NVIDIA 615.78.
These are prior recorded runs, not new tests performed for this tracker.

| Game / program ID | Baseline | Pending #18 evidence | Next test |
|---|---|---|---|
| Mario Kart World / `0400C3F00006E000` | Offline Grand Prix races; pre-race 60 fps, optimized racing ~41–42 fps | Current branch reports ~48–50 fps after further patches; the original present-grace comparison was 36.8/40.0 versus 35.6/34.6 fps in plain builds, plus one optimized 47.2 fps run | Controlled A/B runs with identical scene, flags, CPU load and caches; long-session rendering/stutter checks |
| Nintendo Switch Sports Resort v1.1.0 / `04008E001F098000` | Intro at 60 fps, then GPU/present freeze | Current branch records a 10-minute run through intro, island tour and customization into Tennis at 60 fps with snapshot fix 0010; serve waits for motion input | Physical motion serve, completed match and repeated scene transitions; validate on Windows |
| Other titles | No USCL-specific evidence recorded here | Upstream compatibility claims are outside this pinned test matrix | Add a title-specific evidence file and GAMES.md row after an observed run |

## Work queue

- [ ] Review and build PR #18 at its current head; reconcile its patch numbers with the loading-theme patch.
- [ ] Reproduce the snapshot fix and check CPU/GPU synchronization without forcing unsafe frame-ahead settings.
- [ ] Complete a physical-motion Tennis match and a long Mario Kart World session.
- [ ] Run equivalent Windows tests; record build revision, settings, frame times and visual regressions.
- [ ] Profile draw-state setup and shader compilation separately; improve the measured bottleneck.
- [ ] Add Eglantine room transport and verify it on two PCs.

Update this document, the fork README and GAMES.md together when evidence changes.
Record date, full source revision/patch list, host OS/driver, build flags, game
version, test duration, median/tail frame times, visual correctness and next blocker.
Keep logs trimmed and omit keys, game files and personal data.

## Sources

- [Pinned fork notes and patch explanations](../uscl-eglantine/README.md)
- [USCL Switch setup and limitations](SWITCH.md)
- [Pending performance and correctness PR #18](https://github.com/jaidenscool1/uscl/pull/18)
- [Eglantine upstream](https://github.com/Eglantine-Community/Eglantine), checked 2026-11-18. Upstream calls the emulator experimental; its current README is not validation of USCL's older pinned build.
