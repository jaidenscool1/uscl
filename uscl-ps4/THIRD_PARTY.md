# Native target dependencies

- USCL's existing XMB icons and Noto Sans font are reused from `uscl-shell`.
  The staged assets include the existing icon and font notices.
- stb_image, commit `f0569113c93ad095470c54bf34a17b36646bbbb5`:
  https://github.com/nothings/stb . MIT/public domain; see `third_party/STB-LICENSE`.
- OpenOrbis SDK 0.5.4: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain .
  Toolchain code is GPL-3.0; individual runtime components have their own notices.
  This project uses its headers, runtime, import stubs and publishing tools.
- libjbc, commit `835fe016ff0ae5dd89b9249f39cc0fe093fd07dd`:
  https://github.com/bucanero/ps4-libjbc (derived from sleirsgoevy/ps4-libjbc).
  Fetched separately; source is not copied into this repository. The pinned tree
  supplies no LICENSE file; redistribution rights must be resolved before a public binary release.

External emulator applications are separate projects with separate licenses,
identities and limitations. USCL does not redistribute their code or binaries in
its own PKG, does not claim their work as USCL's port, and does not contain games,
keys, firmware, PS4 system modules or Steamworks SDK files.
