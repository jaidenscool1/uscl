# Dev scripts

Helpers written while testing on the maintainer's Linux PC (Hyprland, outputs `USCLTEST` / `JSXTV`, RPCS3 test
profiles in `~/.local/share/uscl/rpcs3-bo2a|b`). Paths inside assume that setup and `$CLAUDE_JOB_DIR/tmp` as the
scratch folder: adjust before use. See `tools/autotest/README.md` for the virtual pad they drive.

| script | what |
|---|---|
| `release.sh <run id> <tag>` | waits for the Windows installer build, signs it with the local update key (`packaging/sign-release.sh`), publishes, verifies the signature |
| `services.sh` | starts RPCN, our Demonware (dw-server) and the bridge for local tests |
| `mw2launch.sh <a\|b>`, `mw2nav.sh` | start Modern Warfare 2 in test profile a/b and place the window; leave lobby / Find Game -> TDM |
| `p.sh <a\|b> <cmd>` | virtual pad command plus a short pause |
| `vs.sh`, `shotb.sh` | screenshots of the test output (whole / one half) |
| `iso.py` | list / extract files from an ISO 9660 disc image (PS3 `.iso`) |
| `ffdec.py` | inflate an IW4 (MW2) PS3 fastfile |
| `bitdec.py` | decode Demonware bit-packed (MW2) task payloads |
| `gs.py` | GameSpy probe: answers the "available" check (UDP 27900), logs TCP 29900/443/80/... (Gotham City Impostors) |
| `relay.py` | logging UDP relay (watching peer traffic between two local copies) |

`tools/ghidra/toc_str.py` / `abs_str.py`: find code that uses a string in a PS3 ELF (TOC slot or lis/addi loads),
for strings Ghidra's references miss.
