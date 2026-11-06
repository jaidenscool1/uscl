# Security

## How USCL updates are protected
- USCL installs an update only if the release's checksum file is signed with the USCL update key (Ed25519).
  The public key is built into USCL; the private key never leaves the maintainer's PC (it isn't in GitHub or
  CI), so changing a release, the repo or a CI run can't produce an update USCL will install.
- The download must match the signed checksum before anything runs.
- `main` only changes through reviewed pull requests; branches can't be rewritten or deleted; only the owner
  can create release tags.

## Reporting a problem
Please don't open a public issue for a security problem. Use GitHub's private vulnerability reporting
(Security tab → Report a vulnerability).
