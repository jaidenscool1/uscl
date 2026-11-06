#!/bin/bash
# Sign and publish a USCL release. USCL only installs updates whose checksum file carries a signature from the
# update key, which stays on this PC (never in GitHub). CI uploads the release as a draft; this checks each
# file against its checksum, signs the checksums, and publishes.
# usage: packaging/sign-release.sh <tag> [file ...]   (extra files, e.g. build/USCL-x86_64.AppImage, are added)
#   USCL_SIGNING_KEY  the Ed25519 key (default ~/.config/uscl-signing/update-key.pem)
set -euo pipefail
tag=${1:?usage: sign-release.sh <tag> [file ...]}; shift
key=${USCL_SIGNING_KEY:-$HOME/.config/uscl-signing/update-key.pem}
[ -f "$key" ] || { echo "no signing key at $key"; exit 1; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# gh finds the repo from the current folder; pin it, since the signing below runs in $tmp
export GH_REPO=${GH_REPO:-$(gh repo view --json nameWithOwner -q .nameWithOwner)}

gh release view "$tag" >/dev/null 2>&1 || {
  pre=""; case "$tag" in *-*) pre="--prerelease";; esac
  gh release create "$tag" --draft $pre --title "USCL $tag" --notes "USCL $tag"
}
for f in "$@"; do                       # extra files: checksum and upload them
  (cd "$(dirname "$f")" && sha256sum "$(basename "$f")") > "$tmp/$(basename "$f").sha256"
  gh release upload "$tag" "$f" "$tmp/$(basename "$f").sha256" --clobber
done

gh release download "$tag" --dir "$tmp/rel" --clobber
cd "$tmp/rel"
for sum in *.sha256; do
  sha256sum -c "$sum"                   # the file in the release is the one the checksum names
  openssl pkeyutl -sign -rawin -inkey "$key" -in "$sum" -out "$sum.sig"
  echo "signed $sum"
done
gh release upload "$tag" ./*.sig --clobber
gh release edit "$tag" --draft=false
echo "published $tag"
