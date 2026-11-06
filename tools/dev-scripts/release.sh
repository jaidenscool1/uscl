#!/bin/bash
# Wait for the tag's Windows build, then sign + publish it and verify the published signature. Every step is bounded.
R=$1; TAG=$2; REPO=jaidenscool1/uscl; DEV=/home/main/src/ps3-steam-np/.claude/worktrees/dev
for i in $(seq 210); do s=$(timeout 60 gh run view $R --repo $REPO --json status -q .status 2>/dev/null); [ "$s" = completed ] && break; sleep 120; done
c=$(timeout 60 gh run view $R --repo $REPO --json conclusion -q .conclusion 2>/dev/null)
echo "build: ${s:-unknown} ${c:-unknown}"
if [ "$c" != success ]; then
  timeout 60 gh run view $R --repo $REPO --json jobs -q '[.jobs[].steps[] | select(.conclusion=="failure") | .name] | join(", ")'; exit 1
fi
timeout 300 gh release view $TAG --repo $REPO --json isDraft,assets -q '"draft=\(.isDraft) assets=\([.assets[].name]|join(","))"'
cd $DEV && timeout 1200 packaging/sign-release.sh $TAG 2>&1 | tail -4 || { echo "signing failed"; exit 1; }
V=$(mktemp -d); cd $V && timeout 600 gh release download $TAG --repo $REPO -p '*.sha256*' && \
  openssl pkey -in ~/.config/uscl-signing/update-key.pem -pubout -out pub.pem && \
  openssl pkeyutl -verify -pubin -inkey pub.pem -rawin -in USCL-Setup.exe.sha256 -sigfile USCL-Setup.exe.sha256.sig
timeout 60 gh release view $TAG --repo $REPO --json isDraft,isPrerelease,url,assets -q '.url+" draft="+(.isDraft|tostring)+" pre="+(.isPrerelease|tostring)+" | "+([.assets[].name]|join(", "))'
rm -rf $V
