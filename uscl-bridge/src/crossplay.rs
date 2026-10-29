//! Black Ops II PS3 <-> PS4 crossplay: rewrites game-to-game packets between the two builds (games/CUSA57548.md).
//! Both games run with USCL's "plain peer data" patch, so bdDTLS DATA carries the payload unencrypted. The DTLS
//! handshake is the same on both builds and passes through; DATA payloads get their frame translated.
//!
//! DATA packet: u8 type 6, u8 version 2, u16 vtag, u16 seq, 8-byte MAC (not checked by patched games), u16 payload
//! length (LE on both), payload padded to 8 with 0x01. Payload frame: u16 0x0457 (native order: BE on PS3, LE on
//! PS4), u8 flags, message; PS3 adds an RFC 1071 checksum (BE) after the message, PS4 has none.

use std::borrow::Cow;

const DATA: u8 = 6;
const QOS_REPLY: u8 = 0x29; // bdQoS reply: u32 id, u64 echo, u8 0, u8 1, u32 payload length, payload (u16 protocol first)
const QOS_PROTOCOL: usize = 18;
const HEADER: usize = 16; // header 6 + MAC 8 + length 2
const MAGIC: u16 = 0x0457;

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Build {
    Ps3,
    Ps4,
}

impl Build {
    /// The game's network protocol version (lobby, QoS payload).
    fn protocol(self) -> u16 {
        match self {
            Build::Ps3 => 2093,
            Build::Ps4 => 2097,
        }
    }

    fn other(self) -> Build {
        if self == Build::Ps3 { Build::Ps4 } else { Build::Ps3 }
    }
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
    let to = from.other();
    if pkt.first() == Some(&QOS_REPLY) && pkt.len() >= QOS_PROTOCOL + 2 {
        // the searcher drops a host whose QoS payload names another protocol
        let mut out = pkt.to_vec();
        out[QOS_PROTOCOL..QOS_PROTOCOL + 2].copy_from_slice(&to.protocol().to_le_bytes());
        return Some(out);
    }
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

/// Our game's build: the serial in USCL_GAME ("<serial> <hash>") if set, else the forward preset (a mixed session
/// runs the bo2ps4 forwards with a PS3 game too).
pub fn own_build(spec: &str, game: &str) -> Option<Build> {
    match game.split(' ').next() {
        Some("BLUS31011") => Some(Build::Ps3),
        Some("CUSA57548") => Some(Build::Ps4),
        _ => build_for(spec),
    }
}

/// The build a packet came from: DATA by its payload frame's byte order, a QoS reply by its protocol.
pub fn sender(pkt: &[u8]) -> Option<Build> {
    match (pkt.first(), pkt.get(HEADER..HEADER + 2)) {
        (Some(&DATA), Some([0x04, 0x57])) => Some(Build::Ps3),
        (Some(&DATA), Some([0x57, 0x04])) => Some(Build::Ps4),
        (Some(&QOS_REPLY), _) => match pkt.get(QOS_PROTOCOL..QOS_PROTOCOL + 2) {
            Some(&[a, b]) if u16::from_le_bytes([a, b]) == 2093 => Some(Build::Ps3),
            Some(&[a, b]) if u16::from_le_bytes([a, b]) == 2097 => Some(Build::Ps4),
            _ => None,
        },
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

/// Our game's packet as a peer on build `to` expects it (None: drop, not a well-formed packet of ours).
pub fn for_peer(own: Build, to: Build, pkt: &[u8]) -> Option<Cow<'_, [u8]>> {
    if own == to { Some(Cow::Borrowed(pkt)) } else { translate(own, pkt).map(Cow::Owned) }
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

    #[test]
    fn qos_replies_name_the_receivers_protocol() {
        // captured 2026-10-29: a PS4 host's reply to a PS3 probe
        let ps4 = hex("29190000007c575f730000000001100000003108450a020000000000000000000000");
        assert_eq!(sender(&ps4), Some(Build::Ps4));
        let out = for_game(Build::Ps3, &ps4).unwrap();
        assert_eq!(&out[18..20], &2093u16.to_le_bytes());
        assert_eq!((&out[..18], &out[20..]), (&ps4[..18], &ps4[20..]));
        assert!(matches!(for_game(Build::Ps4, &ps4), Some(Cow::Borrowed(_))));
        assert_eq!(&for_peer(Build::Ps4, Build::Ps3, &ps4).unwrap()[18..20], &2093u16.to_le_bytes());
    }

    #[test]
    fn own_build_and_outgoing() {
        assert_eq!(own_build("bo2ps4", "BLUS31011 abc"), Some(Build::Ps3)); // PS3 game in a mixed session
        assert_eq!(own_build("bo2ps4", ""), Some(Build::Ps4));
        assert_eq!(own_build("skate3", "BLUS30464 x"), None);
        let ps3 = hex(PS3);
        assert!(matches!(for_peer(Build::Ps3, Build::Ps3, &ps3), Some(Cow::Borrowed(_))));
        assert_eq!(&for_peer(Build::Ps3, Build::Ps4, &ps3).unwrap()[16..18], &[0x57, 0x04]);
        assert_eq!(sender(&ps3), Some(Build::Ps3));
    }
}
