//! Black Ops II PS3 <-> PS4 crossplay: rewrites game-to-game packets between the two builds (games/CUSA57548.md).
//! Both games run with USCL's "plain peer data" patch, so bdDTLS DATA carries the payload unencrypted. The DTLS
//! handshake is the same on both builds and passes through; DATA payloads get their frame translated.
//!
//! DATA packet: u8 type 6, u8 version 2, u16 vtag, u16 seq, 8-byte MAC (not checked by patched games), u16 payload
//! length (LE on both), payload padded to 8 with 0x01. Payload frame: u16 0x0457 (native order: BE on PS3, LE on
//! PS4), u8 flags, message; PS3 adds an RFC 1071 checksum (BE) after the message, PS4 has none.

use std::borrow::Cow;

const DATA: u8 = 6;
const HEADER: usize = 16; // header 6 + MAC 8 + length 2
const MAGIC: u16 = 0x0457;

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Build {
    Ps3,
    Ps4,
}

/// The PS3 build's checksum (t6mp `0x334244`): BE u16 words summed, carries folded, complemented. An odd last byte
/// is added as is, not shifted into the high byte.
fn checksum(d: &[u8]) -> u16 {
    let mut s: u32 = d.chunks_exact(2).map(|w| u16::from_be_bytes([w[0], w[1]]) as u32).sum();
    if d.len() % 2 == 1 {
        s += d[d.len() - 1] as u32;
    }
    while s >> 16 != 0 {
        s = (s & 0xffff) + (s >> 16);
    }
    !(s as u16)
}

/// The message inside a DATA payload (after the 3-byte frame, without PS3's checksum), with the frame's flags.
/// None if it isn't a well-formed payload of that build (PS3: checksum must match, as the game checks it).
fn message(build: Build, p: &[u8]) -> Option<(u8, &[u8])> {
    if p.len() < 3 {
        return None;
    }
    let magic = match build {
        Build::Ps3 => u16::from_be_bytes([p[0], p[1]]),
        Build::Ps4 => u16::from_le_bytes([p[0], p[1]]),
    };
    if magic != MAGIC {
        return None;
    }
    match build {
        Build::Ps4 => Some((p[2], &p[3..])),
        Build::Ps3 => {
            let (m, ck) = p[3..].split_at_checked(p.len().checked_sub(5)?)?;
            (checksum(m) == u16::from_be_bytes([ck[0], ck[1]])).then_some((p[2], m))
        }
    }
}

fn payload(build: Build, flags: u8, m: &[u8]) -> Vec<u8> {
    let mut p = match build {
        Build::Ps3 => MAGIC.to_be_bytes(),
        Build::Ps4 => MAGIC.to_le_bytes(),
    }
    .to_vec();
    p.push(flags);
    p.extend_from_slice(m);
    if build == Build::Ps3 {
        p.extend_from_slice(&checksum(m).to_be_bytes());
    }
    p
}

/// A packet sent by a `from` game, as the other build expects it. Non-DATA packets (handshake) are the same on both
/// builds; a DATA packet whose payload isn't a well-formed `from` payload is dropped (None), as its receiver would.
pub fn translate(from: Build, pkt: &[u8]) -> Option<Vec<u8>> {
    let to = if from == Build::Ps3 { Build::Ps4 } else { Build::Ps3 };
    if pkt.first() != Some(&DATA) {
        return Some(pkt.to_vec());
    }
    let n = u16::from_le_bytes([*pkt.get(14)?, *pkt.get(15)?]) as usize;
    let (flags, m) = message(from, pkt.get(HEADER..HEADER + n)?)?;
    // shortcut: the message itself is passed as is; text commands match, binary ones (ph, pseg, b, in-game
    // snapshots) still need per-message translation
    let p = payload(to, flags, m);
    let mut out = pkt[..14].to_vec();
    out.extend_from_slice(&(p.len() as u16).to_le_bytes());
    out.extend_from_slice(&p);
    out.resize(HEADER + p.len().next_multiple_of(8), 1);
    Some(out)
}

/// Our own game's build, from the forward preset the bridge runs with.
pub fn build_for(spec: &str) -> Option<Build> {
    match spec {
        "bo2" => Some(Build::Ps3),
        "bo2ps4" => Some(Build::Ps4),
        _ => None,
    }
}

/// The build a DATA packet came from, by its payload frame's byte order.
fn sender(pkt: &[u8]) -> Option<Build> {
    match (pkt.first(), pkt.get(HEADER..HEADER + 2)) {
        (Some(&DATA), Some([0x04, 0x57])) => Some(Build::Ps3),
        (Some(&DATA), Some([0x57, 0x04])) => Some(Build::Ps4),
        _ => None,
    }
}

/// Another player's packet as our game (`own`) expects it: the other build's DATA is translated, everything else
/// passes as is. None: drop it (a malformed packet from the other build).
pub fn for_game(own: Build, pkt: &[u8]) -> Option<Cow<'_, [u8]>> {
    match sender(pkt) {
        Some(from) if from != own => translate(from, pkt).map(Cow::Owned),
        _ => Some(Cow::Borrowed(pkt)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(s: &str) -> Vec<u8> {
        (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect()
    }

    // captured from the patched games (2026-10-29): a joiner's "getchallenge" to the host
    const PS3: &str = "06021725850097122875872718b61500045740ffffffff6765746368616c6c656e67658295010101";
    const PS4: &str = "06024cd7e20039d813a886a2dd681300570400ffffffff6765746368616c6c656e67650101010101";

    #[test]
    fn ps3_checksum_matches_the_game() {
        let p = hex(PS3);
        assert_eq!(checksum(&p[19..35]), 0x8295);
        assert_eq!(checksum(b"\x01\x02\x03"), !0x0105); // odd byte unshifted
    }

    #[test]
    fn ps3_to_ps4_drops_the_checksum() {
        let out = translate(Build::Ps3, &hex(PS3)).unwrap();
        assert_eq!(&out[..14], &hex(PS3)[..14]);
        // PS4 framing, flags kept
        assert_eq!(&out[14..], &hex("1300570440ffffffff6765746368616c6c656e67650101010101")[..]);
    }

    #[test]
    fn ps4_to_ps3_adds_the_checksum() {
        let out = translate(Build::Ps4, &hex(PS4)).unwrap();
        assert_eq!(&out[14..16], &[0x15, 0x00]);
        assert_eq!(&out[16..19], &[0x04, 0x57, 0x00]);
        let (flags, m) = message(Build::Ps3, &out[16..16 + 0x15]).unwrap();
        assert_eq!((flags, m), (0, &b"\xff\xff\xff\xffgetchallenge"[..]));
        assert_eq!(out.len() % 8, 0);
    }

    #[test]
    fn round_trip_and_rejects() {
        let p = hex(PS3);
        assert_eq!(translate(Build::Ps4, &translate(Build::Ps3, &p).unwrap()).unwrap(), p);
        let handshake = hex("010200000000a26f0554175aed837702");
        assert_eq!(translate(Build::Ps3, &handshake).unwrap(), handshake);
        let mut bad = p.clone();
        bad[30] ^= 1; // checksum no longer matches: the PS3 game would drop it too
        assert_eq!(translate(Build::Ps3, &bad), None);
        assert_eq!(translate(Build::Ps3, &p[..20]), None); // truncated
        assert_eq!(translate(Build::Ps4, &hex(PS3)), None); // wrong build
    }

    #[test]
    fn games_get_their_own_build() {
        let (ps3, ps4) = (hex(PS3), hex(PS4));
        assert_eq!(build_for("bo2"), Some(Build::Ps3));
        assert_eq!(build_for("skate3"), None);
        assert!(matches!(for_game(Build::Ps3, &ps3), Some(Cow::Borrowed(_)))); // same build: untouched
        assert_eq!(&for_game(Build::Ps4, &ps3).unwrap()[16..18], &[0x57, 0x04]);
        assert_eq!(&for_game(Build::Ps3, &ps4).unwrap()[16..18], &[0x04, 0x57]);
        let handshake = hex("010200000000a26f0554175aed837702");
        assert!(matches!(for_game(Build::Ps4, &handshake), Some(Cow::Borrowed(_))));
    }
}
