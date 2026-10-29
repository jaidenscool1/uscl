//! Black Ops II PS3 <-> PS4 crossplay: rewrites game-to-game packets between the two builds (games/CUSA57548.md).
//! Both games run with USCL's patches: "plain peer data" (bdDTLS DATA unencrypted, the Steam tunnel encrypts) and,
//! on PS3, "little-endian messages" (msg_t numbers little-endian like PS4), so most messages are the same bytes on
//! both builds. The DTLS handshake passes through; what differs and is rewritten here:
//! - DATA payload: u16 0x0457 (LE), u8 flags, message; PS3 adds an RFC 1071 checksum (BE) after the message.
//! - QoS reply: the host's protocol (2093 / 2097) in its payload.
//! - joinParty: the joiner's protocol and netcode checksum, which the host compares with its own.
//!
//! DATA packet: u8 type 6, u8 version 2, u16 vtag, u16 seq, 8-byte MAC (not checked by patched games), u16 payload
//! length (LE), payload padded to 8 with 0x01.

use std::borrow::Cow;

const DATA: u8 = 6;
const QOS_REPLY: u8 = 0x29; // bdQoS reply: u32 id, u64 echo, u8 0, u8 1, u32 payload length, payload (u16 protocol first)
const QOS_PROTOCOL: usize = 18;
const HEADER: usize = 16; // header 6 + MAC 8 + length 2
const MAGIC: [u8; 2] = 0x0457u16.to_le_bytes();

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

    /// The netcode checksum the build's joinParty carries and its host compares (PS4 `FUN_0038aca0`, a global set
    /// at start). Captured from both games 2026-10-29.
    fn netcode(self) -> u32 {
        match self {
            Build::Ps3 => 0xe377b8be,
            Build::Ps4 => 0x5fcda453,
        }
    }

    fn other(self) -> Build {
        if self == Build::Ps3 { Build::Ps4 } else { Build::Ps3 }
    }

    /// Lobby member data value, so other bridges know which build this player runs.
    pub fn name(self) -> &'static str {
        if self == Build::Ps3 { "ps3" } else { "ps4" }
    }

    pub fn from_name(s: &str) -> Option<Build> {
        match s {
            "ps3" => Some(Build::Ps3),
            "ps4" => Some(Build::Ps4),
            _ => None,
        }
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
    if p.len() < 3 || p[..2] != MAGIC {
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
    let mut p = MAGIC.to_vec();
    p.push(flags);
    p.extend_from_slice(m);
    if build == Build::Ps3 {
        p.extend_from_slice(&checksum(m).to_be_bytes());
    }
    p
}

/// joinParty body (PS4 reader `FUN_00154c00`): u32 protocol, u32 netcode, then the rest (playlist version, ...),
/// the same on both builds. The host refuses another build's protocol or netcode ("partyJoinFailed netcode").
fn join_party(to: Build, body: &[u8]) -> Option<Vec<u8>> {
    let rest = body.get(8..)?;
    Some([&(to.protocol() as u32).to_le_bytes()[..], &to.netcode().to_le_bytes(), rest].concat())
}

/// An out-of-band message (`ff ff ff ff`, an optional digit, the command, its body) in the other build's form.
/// Other commands pass as they are.
fn oob(to: Build, m: &[u8]) -> Option<Cow<'_, [u8]>> {
    let rest = m.strip_prefix(&[0xff; 4])?;
    let start = usize::from(rest.first().is_some_and(u8::is_ascii_digit));
    let name_len = rest[start..].iter().position(|&b| b == 0 || b == b' ').unwrap_or(rest.len() - start);
    let (head, body) = rest.split_at(start + name_len);
    let body = match &head[start..] {
        b"joinParty" if body.first() == Some(&0) => [&[0][..], &join_party(to, &body[1..])?].concat(),
        _ => return Some(Cow::Borrowed(m)),
    };
    Some(Cow::Owned([&[0xff; 4][..], head, &body].concat()))
}

/// A packet sent by a `from` game, as the other build expects it. Handshake packets are the same on both builds;
/// a DATA packet whose payload isn't a well-formed `from` payload is dropped (None), as its receiver would.
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
    let m = if m.starts_with(&[0xff; 4]) { oob(to, m)? } else { Cow::Borrowed(m) };
    let p = payload(to, flags, &m);
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

/// The build a packet came from, for peers whose bridge doesn't announce its build: a QoS reply by its protocol, a
/// DATA payload by PS3's checksum (shortcut: a PS4 payload whose last 2 bytes happen to match it reads as PS3).
pub fn sender(pkt: &[u8]) -> Option<Build> {
    match pkt.first() {
        Some(&QOS_REPLY) => match u16::from_le_bytes([*pkt.get(QOS_PROTOCOL)?, *pkt.get(QOS_PROTOCOL + 1)?]) {
            2093 => Some(Build::Ps3),
            2097 => Some(Build::Ps4),
            _ => None,
        },
        Some(&DATA) => {
            let n = u16::from_le_bytes([*pkt.get(14)?, *pkt.get(15)?]) as usize;
            let p = pkt.get(HEADER..HEADER + n)?;
            if message(Build::Ps3, p).is_some() {
                Some(Build::Ps3)
            } else {
                message(Build::Ps4, p).map(|_| Build::Ps4)
            }
        }
        _ => None,
    }
}

/// A packet between games on builds `from` and `to`: translated when they differ. None: drop it.
pub fn between(from: Build, to: Build, pkt: &[u8]) -> Option<Cow<'_, [u8]>> {
    if from == to { Some(Cow::Borrowed(pkt)) } else { translate(from, pkt).map(Cow::Owned) }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(s: &str) -> Vec<u8> {
        (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect()
    }

    // a joiner's "getchallenge" to the host, captured from the patched games 2026-10-29 (PS3: frame in the
    // little-endian messages patch's order; the checksum covers only the message)
    const PS3: &str = "06021725850097122875872718b61500570440ffffffff6765746368616c6c656e67658295010101";
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
        assert_eq!(&out[14..], &hex("1300570440ffffffff6765746368616c6c656e67650101010101")[..]); // flags kept
    }

    #[test]
    fn ps4_to_ps3_adds_the_checksum() {
        let out = translate(Build::Ps4, &hex(PS4)).unwrap();
        assert_eq!(&out[14..19], &[0x15, 0x00, 0x57, 0x04, 0x00]);
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
        assert_eq!(translate(Build::Ps3, &hex(PS4)), None); // no checksum: not a PS3 payload
    }

    #[test]
    fn same_build_is_untouched() {
        let (ps3, ps4) = (hex(PS3), hex(PS4));
        assert!(matches!(between(Build::Ps3, Build::Ps3, &ps3), Some(Cow::Borrowed(_))));
        assert_eq!(&between(Build::Ps3, Build::Ps4, &ps3).unwrap()[..], &translate(Build::Ps3, &ps3).unwrap()[..]);
        assert_eq!(&between(Build::Ps4, Build::Ps3, &ps4).unwrap()[14..16], &[0x15, 0x00]);
        assert_eq!(build_for("bo2"), Some(Build::Ps3));
        assert_eq!(build_for("skate3"), None);
        assert_eq!(own_build("bo2ps4", "BLUS31011 abc"), Some(Build::Ps3)); // PS3 game in a mixed session
        assert_eq!(own_build("bo2ps4", ""), Some(Build::Ps4));
        assert_eq!(Build::from_name(Build::Ps3.name()), Some(Build::Ps3));
    }

    #[test]
    fn join_party_names_the_hosts_protocol_and_netcode() {
        // captured 2026-10-29 from the PS4 (body after "2joinParty\0")
        let ps4 = hex("3108000053a4cd5f010000000000000000000000020f000000000001012f49e36a00000000010400000000000000");
        let to3 = join_party(Build::Ps3, &ps4).unwrap();
        assert_eq!(&to3[..8], &hex("2d080000beb877e3")[..]); // 2093, PS3 netcode (little-endian)
        assert_eq!(&to3[8..], &ps4[8..]);
        assert_eq!(join_party(Build::Ps4, &to3).unwrap(), ps4);
        assert_eq!(join_party(Build::Ps3, &ps4[..6]), None);

        let m = [&[0xff; 4][..], b"2joinParty\0", &ps4].concat();
        let out = oob(Build::Ps3, &m).unwrap();
        assert_eq!((&out[..15], &out[15..]), (&m[..15], &to3[..]));
        let text = [&[0xff; 4][..], b"0partyJoinFailed netcode"].concat();
        assert!(matches!(oob(Build::Ps3, &text), Some(Cow::Borrowed(_))));
    }

    #[test]
    fn qos_replies_name_the_receivers_protocol() {
        // captured 2026-10-29: a PS4 host's reply to a PS3 probe
        let ps4 = hex("29190000007c575f730000000001100000003108450a020000000000000000000000");
        assert_eq!(sender(&ps4), Some(Build::Ps4));
        let out = between(Build::Ps4, Build::Ps3, &ps4).unwrap();
        assert_eq!(&out[18..20], &2093u16.to_le_bytes());
        assert_eq!((&out[..18], &out[20..]), (&ps4[..18], &ps4[20..]));
        assert_eq!(sender(&hex(PS4)), Some(Build::Ps4));
        assert_eq!(sender(&hex(PS3)), Some(Build::Ps3));
        assert_eq!(sender(&hex("010200000000a26f0554175aed837702")), None); // handshake
    }
}
