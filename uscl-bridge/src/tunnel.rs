//! `uscl-bridge host` / `uscl-bridge join [lobby]`: carry RPCN and game traffic between players over Steam.
//!
//! One player (the host) runs uscl-rpcn with USCL_LOCAL=1 (TCP 31413, UDP 3757). Everyone, the host included,
//! points RPCS3 at 127.0.0.1 and runs the bridge, which owns the ports RPCS3 always uses:
//!   TCP 31313 -> host's RPCN TCP       UDP 3657 -> host's RPCN UDP (signaling address discovery)
//! The session is a Steam lobby. Its owner is the host and gives each member a slot (lobby data "slot:<steamid>").
//! Member `s` is 127.0.0.1:SLOT_BASE+s to every RPCS3: the host forwards that member's RPCN UDP from that port,
//! so RPCN hands it out as the member's P2P address, and every bridge listens there and relays to that member.
//! Only 127.0.0.1 and ports, so it behaves the same on Windows, Linux, macOS and the Steam Deck.
//! Game traffic goes straight between members (Steam relay), not through the host. Lobbies hold up to 250.

use std::collections::{HashMap, HashSet};
use std::io::{Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream, UdpSocket};
use std::sync::mpsc::{channel, Sender};
use std::time::{Duration, Instant};
use steamworks::networking_messages::SessionRequest;
use steamworks::networking_types::{NetworkingIdentity, SendFlags};
use steamworks::{Client, GameLobbyJoinRequested, LobbyChatUpdate, LobbyDataUpdate, LobbyId, LobbyType, SteamId};

const RPCN_TCP: u16 = 31313;
const RPCN_UDP: u16 = 3657;
const RPCS3_P2P: u16 = 3658;
const HOST_RPCN_TCP: u16 = 31413;
const HOST_RPCN_UDP: u16 = 3757;
// shortcut: fixed port range 39001+, make it configurable if something on a player's PC already uses it
const SLOT_BASE: u16 = 39000;

const CH_TCP: u32 = 1;
const CH_UDP: u32 = 2;
const UDP_RPCN: u8 = 0;
const UDP_P2P: u8 = 1;
const TCP_OPEN: u8 = 0;
const TCP_DATA: u8 = 1;
const TCP_CLOSE: u8 = 2;

fn local(port: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], port))
}

/// Steam channel 1 frame: connection id, op, payload.
fn tcp_frame(id: u32, op: u8, data: &[u8]) -> Vec<u8> {
    let mut f = Vec::with_capacity(5 + data.len());
    f.extend_from_slice(&id.to_be_bytes());
    f.push(op);
    f.extend_from_slice(data);
    f
}

fn parse_tcp_frame(f: &[u8]) -> Option<(u32, u8, &[u8])> {
    (f.len() >= 5).then(|| (u32::from_be_bytes([f[0], f[1], f[2], f[3]]), f[4], &f[5..]))
}

enum Ev {
    Accepted(TcpStream),
    Tcp { peer: u64, id: u32, data: Option<Vec<u8>> }, // None: that side closed
    Changed,
    Invite(LobbyId),
    Session(SessionRequest),
}

fn spawn_reader(mut s: TcpStream, peer: u64, id: u32, tx: Sender<Ev>) {
    std::thread::spawn(move || {
        let mut buf = vec![0u8; 64 * 1024];
        loop {
            match s.read(&mut buf) {
                Ok(0) | Err(_) => break,
                Ok(n) => {
                    if tx.send(Ev::Tcp { peer, id, data: Some(buf[..n].to_vec()) }).is_err() {
                        return;
                    }
                }
            }
        }
        let _ = tx.send(Ev::Tcp { peer, id, data: None });
    });
}

fn bind_udp(port: u16) -> Result<UdpSocket, String> {
    let s = UdpSocket::bind(local(port)).map_err(|e| format!("bind UDP 127.0.0.1:{port}: {e}"))?;
    s.set_nonblocking(true).map_err(|e| e.to_string())?;
    Ok(s)
}

/// Wait for a one-shot Steam call result.
fn wait<T>(client: &Client, rx: &std::sync::mpsc::Receiver<T>, what: &str) -> Result<T, String> {
    for _ in 0..400 {
        client.run_callbacks();
        if let Ok(v) = rx.try_recv() {
            return Ok(v);
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    Err(format!("timed out waiting for {what}"))
}

/// `join`: None waits for a Steam invite (accept it from the friends list while the bridge runs).
pub fn run(client: Client, host: bool, join: Option<LobbyId>) -> Result<(), String> {
    client.networking_utils().init_relay_network_access();
    let mm = client.matchmaking();
    let net = client.networking_messages();
    let me = client.user().steam_id();
    let (tx, rx) = channel::<Ev>();

    // Bind before touching Steam so a port clash (RPCN or another bridge already running) fails fast.
    let listener = TcpListener::bind(local(RPCN_TCP)).map_err(|e| format!("bind TCP 127.0.0.1:{RPCN_TCP}: {e} (stop RPCN/other bridge on this port)"))?;
    let rpcn_sock = bind_udp(RPCN_UDP)?;

    let t = tx.clone();
    let _h1 = client.register_callback(move |_: LobbyChatUpdate| {
        let _ = t.send(Ev::Changed);
    });
    let t = tx.clone();
    let _h2 = client.register_callback(move |_: LobbyDataUpdate| {
        let _ = t.send(Ev::Changed);
    });
    let t = tx.clone();
    let _h3 = client.register_callback(move |j: GameLobbyJoinRequested| {
        let _ = t.send(Ev::Invite(j.lobby_steam_id));
    });
    // Only lobby members may open a Steam session with us; the main loop decides (see `pending`).
    let t = tx.clone();
    net.session_request_callback(move |req| {
        let _ = t.send(Ev::Session(req));
    });

    let lobby = if host {
        let (t, r) = channel();
        mm.create_lobby(LobbyType::FriendsOnly, 250, move |res| {
            let _ = t.send(res);
        });
        let lobby = wait(&client, &r, "lobby creation")?.map_err(|e| format!("create_lobby: {e:?}"))?;
        mm.set_lobby_data(lobby, "uscl", "session");
        mm.set_lobby_data(lobby, &format!("slot:{}", me.raw()), "1");
        lobby
    } else {
        let id = match join {
            Some(id) => id,
            None => {
                eprintln!("uscl-bridge: waiting for a Steam invite to a USCL session...");
                loop {
                    client.run_callbacks();
                    if let Ok(Ev::Invite(id)) = rx.try_recv() {
                        break id;
                    }
                    std::thread::sleep(Duration::from_millis(50));
                }
            }
        };
        let (t, r) = channel();
        mm.join_lobby(id, move |res| {
            let _ = t.send(res);
        });
        wait(&client, &r, "lobby join")?.map_err(|_| "join_lobby failed (wrong id, full, or not a friend of the host)".to_string())?
    };
    let host_id = mm.lobby_owner(lobby);
    println!("uscl-bridge: session {} ({}); give friends this number or invite them from Steam", lobby.raw(), if host { "hosting" } else { "joined" });
    client.friends().set_rich_presence("status", Some("USCL session"));

    let t = tx.clone();
    std::thread::spawn(move || {
        for s in listener.incoming().flatten() {
            if t.send(Ev::Accepted(s)).is_err() {
                return;
            }
        }
    });

    let send = |peer: u64, ch: u32, data: &[u8], reliable: bool| {
        let flags = if reliable { SendFlags::RELIABLE_NO_NAGLE } else { SendFlags::UNRELIABLE_NO_NAGLE };
        let _ = net.send_message_to_user(
            NetworkingIdentity::new_steam_id(SteamId::from_raw(peer)),
            flags | SendFlags::AUTO_RESTART_BROKEN_SESSION,
            data,
            ch,
        );
    };

    let mut slot_of: HashMap<u64, u16> = HashMap::new();
    let mut member_at: HashMap<u16, u64> = HashMap::new();
    let mut socks: HashMap<u16, UdpSocket> = HashMap::new();
    let mut tcp: HashMap<(u64, u32), TcpStream> = HashMap::new();
    let mut next_conn: u32 = 0;
    let mut next_slot: u16 = 1;
    let mut changed = true;
    let mut members: HashSet<u64> = HashSet::new();
    // a request can beat the lobby update that makes its sender a member, so it waits here a little
    let mut pending: Vec<(SessionRequest, Instant)> = Vec::new();
    let mut buf = vec![0u8; 65536];

    loop {
        client.run_callbacks();

        while let Ok(ev) = rx.try_recv() {
            match ev {
                Ev::Changed => changed = true,
                Ev::Invite(_) => {} // already in a session
                Ev::Session(req) => pending.push((req, Instant::now())),
                Ev::Accepted(s) => {
                    next_conn += 1;
                    if host {
                        // our own RPCS3: straight to the local RPCN
                        match TcpStream::connect(local(HOST_RPCN_TCP)) {
                            Ok(up) => {
                                let (mut a, mut b) = (s.try_clone().map_err(|e| e.to_string())?, up.try_clone().map_err(|e| e.to_string())?);
                                std::thread::spawn(move || { let _ = std::io::copy(&mut a, &mut b); let _ = b.shutdown(Shutdown::Both); });
                                let (mut a, mut b) = (up, s);
                                std::thread::spawn(move || { let _ = std::io::copy(&mut a, &mut b); let _ = b.shutdown(Shutdown::Both); });
                            }
                            Err(e) => eprintln!("uscl-bridge: RPCN not reachable on {HOST_RPCN_TCP}: {e} (start uscl-rpcn with USCL_LOCAL=1)"),
                        }
                    } else {
                        let host = host_id.raw();
                        if let Ok(r) = s.try_clone() {
                            tcp.insert((host, next_conn), s);
                            spawn_reader(r, host, next_conn, tx.clone());
                            send(host, CH_TCP, &tcp_frame(next_conn, TCP_OPEN, &[]), true);
                        }
                    }
                }
                Ev::Tcp { peer, id, data: Some(d) } => send(peer, CH_TCP, &tcp_frame(id, TCP_DATA, &d), true),
                Ev::Tcp { peer, id, data: None } => {
                    if tcp.remove(&(peer, id)).is_some() {
                        send(peer, CH_TCP, &tcp_frame(id, TCP_CLOSE, &[]), true);
                    }
                }
            }
        }

        if changed {
            changed = false;
            let list = mm.lobby_members(lobby);
            if !host && !list.is_empty() && mm.lobby_owner(lobby) != host_id {
                return Err("the host left; the session is over".into());
            }
            members = list.iter().map(|m| m.raw()).collect();
            for m in &list {
                let key = format!("slot:{}", m.raw());
                let mut slot = mm.lobby_data(lobby, &key).and_then(|v| v.parse::<u16>().ok());
                if host && slot.is_none() {
                    next_slot += 1;
                    mm.set_lobby_data(lobby, &key, &next_slot.to_string());
                    slot = Some(next_slot);
                }
                if let Some(s) = slot {
                    next_slot = next_slot.max(s);
                    slot_of.insert(m.raw(), s);
                    member_at.insert(s, m.raw());
                    if !socks.contains_key(&s) {
                        socks.insert(s, bind_udp(SLOT_BASE + s)?);
                    }
                }
            }
        }
        let mut waiting = Vec::new();
        for (req, since) in pending.drain(..) {
            if req.remote().steam_id().is_some_and(|id| members.contains(&id.raw())) {
                req.accept();
            } else if since.elapsed() < Duration::from_secs(10) {
                waiting.push((req, since));
            } // else dropped, which rejects it
        }
        pending = waiting;
        let my_slot = slot_of.get(&me.raw()).copied();

        // Our RPCS3 -> RPCN signaling port
        while let Ok((n, _)) = rpcn_sock.recv_from(&mut buf) {
            if host {
                if let Some(s) = my_slot.and_then(|s| socks.get(&s)) {
                    let _ = s.send_to(&buf[..n], local(HOST_RPCN_UDP));
                }
            } else {
                let mut f = vec![UDP_RPCN];
                f.extend_from_slice(&buf[..n]);
                send(host_id.raw(), CH_UDP, &f, false);
            }
        }

        // Slot ports: RPCN replies (host only) and our RPCS3's game packets to that member
        for (&slot, sock) in &socks {
            while let Ok((n, src)) = sock.recv_from(&mut buf) {
                let from_rpcn = host && src.port() == HOST_RPCN_UDP;
                if from_rpcn && Some(slot) == my_slot {
                    let _ = rpcn_sock.send_to(&buf[..n], local(RPCS3_P2P));
                } else if Some(slot) != my_slot {
                    if let Some(&peer) = member_at.get(&slot) {
                        let mut f = vec![if from_rpcn { UDP_RPCN } else { UDP_P2P }];
                        f.extend_from_slice(&buf[..n]);
                        send(peer, CH_UDP, &f, false);
                    }
                }
            }
        }

        for m in net.receive_messages_on_channel(CH_UDP, 256) {
            let Some(peer) = m.identity_peer().steam_id().map(|s| s.raw()) else { continue };
            let Some(sock) = slot_of.get(&peer).and_then(|s| socks.get(s)) else { continue };
            match m.data().split_first() {
                Some((&UDP_RPCN, d)) if host => { let _ = sock.send_to(d, local(HOST_RPCN_UDP)); }
                Some((&UDP_RPCN, d)) if peer == host_id.raw() => { let _ = rpcn_sock.send_to(d, local(RPCS3_P2P)); }
                Some((&UDP_P2P, d)) => { let _ = sock.send_to(d, local(RPCS3_P2P)); }
                _ => {}
            }
        }

        for m in net.receive_messages_on_channel(CH_TCP, 256) {
            let Some(peer) = m.identity_peer().steam_id().map(|s| s.raw()) else { continue };
            let Some((id, op, d)) = parse_tcp_frame(m.data()) else { continue };
            match op {
                TCP_OPEN if host => match TcpStream::connect(local(HOST_RPCN_TCP)) {
                    Ok(s) => {
                        if let Ok(r) = s.try_clone() {
                            tcp.insert((peer, id), s);
                            spawn_reader(r, peer, id, tx.clone());
                        }
                    }
                    Err(e) => {
                        eprintln!("uscl-bridge: RPCN not reachable on {HOST_RPCN_TCP}: {e}");
                        send(peer, CH_TCP, &tcp_frame(id, TCP_CLOSE, &[]), true);
                    }
                },
                TCP_DATA => {
                    let failed = tcp.get_mut(&(peer, id)).is_some_and(|s| s.write_all(d).is_err());
                    if failed {
                        tcp.remove(&(peer, id));
                        send(peer, CH_TCP, &tcp_frame(id, TCP_CLOSE, &[]), true);
                    }
                }
                TCP_CLOSE => {
                    if let Some(s) = tcp.remove(&(peer, id)) {
                        let _ = s.shutdown(Shutdown::Both);
                    }
                }
                _ => {}
            }
        }

        // shortcut: 1 ms polling adds up to 1 ms latency per hop; switch to blocking reader threads if that shows in-game
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tcp_frame_round_trip() {
        let f = tcp_frame(0xDEADBEEF, TCP_DATA, b"hi");
        assert_eq!(parse_tcp_frame(&f), Some((0xDEADBEEF, TCP_DATA, &b"hi"[..])));
        assert_eq!(parse_tcp_frame(&f[..4]), None);
    }
}
