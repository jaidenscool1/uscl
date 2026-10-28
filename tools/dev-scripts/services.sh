#!/bin/bash
# start RPCN, our Demonware and the bridge for local tests; logs in $L
L=${L:-$CLAUDE_JOB_DIR/tmp/svc}; mkdir -p $L
SL=$(dirname $(find ~/.cargo/registry -name libsteam_api.so -path '*linux64*' | head -1))
(cd ~/.local/share/uscl/rpcn && USCL_LOCAL=1 nohup $CLAUDE_JOB_DIR/tmp/appimg/squashfs-root/usr/bin/uscl-rpcn > $L/rpcn.out 2>&1 &)
(cd ~/.local/share/uscl/dw && DW_ADDR=127.0.0.1:3074 DW_STUN_ADDR=127.0.0.1:3074 DW_STUN_ALT_ADDR=127.0.0.1:3077 DW_TRUSTED_USER=uscltest DW_AC_INTERVAL=60 RUST_LOG=${DWLOG:-debug} nohup /home/main/src/obde-uscl/.claude/worktrees/anticheat/target/release/dw-server > $L/dw.out 2>&1 &)
(cd /home/main/src/ps3-steam-np/.claude/worktrees/dev/uscl-bridge && LD_LIBRARY_PATH=$SL nohup target/release/uscl-bridge host ${PRESET:-bo2} > $L/bridge.out 2>&1 &)
sleep 5; tail -1 $L/rpcn.out; grep -m1 'Auth and lobby' $L/dw.out; grep 'session' $L/bridge.out | head -1
