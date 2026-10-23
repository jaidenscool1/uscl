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
//!
//! Forwards (`host skate3` or `host tcp:42100,udp:17001-17064`): for games that play through a community server.
//! The host runs that server; members' bridges listen on the same 127.0.0.1 ports (lobby data "fwd") and carry the
//! traffic to it, so members point the game at 127.0.0.1. The host only ever connects to ports on its own list.

use std::collections::{HashMap, HashSet};
use std::io::{Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream, UdpSocket};
use std::sync::mpsc::{channel, Sender};
use std::time::{Duration, Instant};
use steamworks::networking_messages::SessionRequest;
use steamworks::networking_types::{NetworkingIdentity, SendFlags};
use steamworks::{Client, FriendFlags, GameLobbyJoinRequested, LobbyChatUpdate, LobbyDataUpdate, LobbyId, LobbyType, SteamId};

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
const UDP_FWD: u8 = 2; // [port u16 BE][data]
const TCP_OPEN: u8 = 0; // payload: empty = RPCN, or a forwarded port u16 BE
const TCP_DATA: u8 = 1;
const TCP_CLOSE: u8 = 2;

/// Port lists for games played through a community server (see games/<serial>.md).
fn preset(name: &str) -> Option<&'static str> {
    match name {
        // Skate3BlazeServer: Blaze, HTTP, one UDP relay per lobby at 17000 + game id
        "skate3" => Some("tcp:42100,tcp:80,udp:17001-17064"),
        _ => None,
    }
}

/// "tcp:42100,udp:17001-17064" -> [(udp?, port)]
pub fn parse_forwards(spec: &str) -> Result<Vec<(bool, u16)>, String> {
    let spec = preset(spec).unwrap_or(spec);
    let mut out = Vec::new();
    for item in spec.split(',').filter(|i| !i.is_empty()) {
        let bad = || format!("bad forward `{item}` (want tcp:PORT, udp:PORT or udp:FIRST-LAST, or a preset: skate3)");
        let (proto, ports) = item.split_once(':').ok_or_else(bad)?;
        let udp = match proto { "tcp" => false, "udp" => true, _ => return Err(bad()) };
        let (a, b) = ports.split_once('-').unwrap_or((ports, ports));
        let (a, b): (u16, u16) = (a.parse().map_err(|_| bad())?, b.parse().map_err(|_| bad())?);
        if a == 0 || b < a {
            return Err(bad());
        }
        out.extend((a..=b).map(|p| (udp, p)));
    }
    if out.len() > 512 {
        return Err("more than 512 forwarded ports".into());
    }
    Ok(out)
}

fn fwd_spec(fwd: &[(bool, u16)]) -> String {
    fwd.iter().map(|(u, p)| format!("{}:{p}", if *u { "udp" } else { "tcp" })).collect::<Vec<_>>().join(",")
}

fn fwd_frame(port: u16, data: &[u8]) -> Vec<u8> {
    let mut f = vec![UDP_FWD];
    f.extend_from_slice(&port.to_be_bytes());
    f.extend_from_slice(data);
    f
}

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
    Accepted(TcpStream, u16), // local port it came in on
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

/// The host's RPCN. Windows takes ~2 s to refuse a closed loopback port, which would stall the whole loop.
fn connect_rpcn() -> std::io::Result<TcpStream> {
    TcpStream::connect_timeout(&local(HOST_RPCN_TCP), Duration::from_millis(300))
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

/// A new session lobby. `slots` carry over when Steam dropped the old one, so members keep their ports.
fn create_session(client: &Client, slots: &HashMap<u64, u16>, fwd: &[(bool, u16)]) -> Result<LobbyId, String> {
    let mm = client.matchmaking();
    let (t, r) = channel();
    mm.create_lobby(LobbyType::FriendsOnly, 250, move |res| {
        let _ = t.send(res);
    });
    let lobby = wait(client, &r, "lobby creation")?.map_err(|e| format!("create_lobby: {e:?}"))?;
    mm.set_lobby_data(lobby, "uscl", "session");
    mm.set_lobby_data(lobby, "fwd", &fwd_spec(fwd));
    for (id, slot) in slots {
        mm.set_lobby_data(lobby, &format!("slot:{id}"), &slot.to_string());
    }
    Ok(lobby)
}

fn join_session(client: &Client, lobby: LobbyId) -> Result<LobbyId, String> {
    let (t, r) = channel();
    client.matchmaking().join_lobby(lobby, move |res| {
        let _ = t.send(res);
    });
    let lobby = wait(client, &r, "lobby join")?.map_err(|_| "join_lobby failed (wrong id, full, or not a friend of the host)".to_string())?;
    // other projects use app 480 too, so a friend's lobby may be someone else's
    if client.matchmaking().lobby_data(lobby, "uscl").as_deref() != Some("session") {
        client.matchmaking().leave_lobby(lobby);
        return Err(NOT_USCL.to_string());
    }
    Ok(lobby)
}

const NOT_USCL: &str = "that lobby isn't a USCL session";

/// Lobbies of this app that friends are in, with who's in them: the sessions we could join.
fn friend_sessions(client: &Client) -> Vec<(LobbyId, String)> {
    let app = client.utils().app_id();
    let mut found: Vec<(LobbyId, String)> = Vec::new();
    for f in client.friends().get_friends(FriendFlags::IMMEDIATE) {
        if let Some(g) = f.game_played().filter(|g| g.game.app_id() == app && g.lobby.raw() != 0) {
            match found.iter_mut().find(|(l, _)| *l == g.lobby) {
                Some((_, names)) => *names += &format!(", {}", f.name()),
                None => found.push((g.lobby, f.name())),
            }
        }
    }
    found
}

/// `join`: None joins the one session a friend is in, or a Steam invite, whichever comes first.
pub fn run(client: Client, host: bool, join: Option<LobbyId>, fwd: Vec<(bool, u16)>) -> Result<(), String> {
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

    let mut slot_of: HashMap<u64, u16> = HashMap::new();
    let mut lobby = if host {
        slot_of.insert(me.raw(), 1);
        create_session(&client, &slot_of, &fwd)?
    } else {
        match join {
            Some(id) => join_session(&client, id)?,
            None => {
                eprintln!("uscl-bridge: looking for a friend's USCL session (or accept a Steam invite)...");
                let mut last = Instant::now() - Duration::from_secs(10);
                // lobbies that turned out not to be USCL sessions or refused us
                let mut skip: HashSet<LobbyId> = HashSet::new();
                loop {
                    client.run_callbacks();
                    if let Ok(Ev::Invite(id)) = rx.try_recv() {
                        match join_session(&client, id) {
                            Ok(l) => break l,
                            Err(e) => eprintln!("uscl-bridge: couldn't join the invite: {e}"),
                        }
                    }
                    if last.elapsed() > Duration::from_secs(2) {
                        last = Instant::now();
                        // join each candidate to read its data; stay only if exactly one is a USCL session
                        let mut ok: Vec<(LobbyId, String)> = Vec::new();
                        let fresh: Vec<_> = friend_sessions(&client).into_iter().filter(|(l, _)| !skip.contains(l)).collect();
                        for (l, who) in fresh {
                            match join_session(&client, l) {
                                Ok(l) => ok.push((l, who)),
                                Err(e) => {
                                    if e != NOT_USCL {
                                        eprintln!("uscl-bridge: couldn't join {}'s lobby: {e}", who);
                                    }
                                    skip.insert(l);
                                }
                            }
                        }
                        match ok.len() {
                            0 => {}
                            1 => break ok[0].0,
                            _ => {
                                ok.iter().for_each(|(l, _)| mm.leave_lobby(*l));
                                let list: Vec<String> = ok.iter().map(|(l, who)| format!("  {}  ({who})", l.raw())).collect();
                                return Err(format!("friends are in more than one session; run `join <number>` with one of:\n{}", list.join("\n")));
                            }
                        }
                    }
                    std::thread::sleep(Duration::from_millis(50));
                }
            }
        }
    };
    let host_id = mm.lobby_owner(lobby);
    println!("uscl-bridge: session {} ({}); give friends this number or invite them from Steam", lobby.raw(), if host { "hosting" } else { "joined" });
    client.friends().set_rich_presence("status", Some("USCL session"));

    let accept = |listener: TcpListener, port: u16| {
        let t = tx.clone();
        std::thread::spawn(move || {
            for s in listener.incoming().flatten() {
                if t.send(Ev::Accepted(s, port)).is_err() {
                    return;
                }
            }
        });
    };
    accept(listener, RPCN_TCP);

    // Forwards: the host connects to its own list only; a member listens on the host's list.
    let fwd = if host { fwd } else { parse_forwards(&mm.lobby_data(lobby, "fwd").unwrap_or_default())? };
    let mut fwd_udp: HashMap<u16, (UdpSocket, Option<SocketAddr>)> = HashMap::new(); // member: port -> socket, game's address
    let mut fwd_host: HashMap<(u64, u16), UdpSocket> = HashMap::new(); // host: one socket per member and port
    if !host && !fwd.is_empty() {
        let mut failed = Vec::new();
        for &(udp, port) in &fwd {
            let res = if udp {
                bind_udp(port).map(|s| { fwd_udp.insert(port, (s, None)); })
            } else {
                TcpListener::bind(local(port)).map(|l| accept(l, port)).map_err(|e| e.to_string())
            };
            if res.is_err() {
                failed.push(format!("{}:{port}", if udp { "udp" } else { "tcp" }));
            }
        }
        println!("uscl-bridge: forwarding {} ports to the host's server", fwd.len() - failed.len());
        if !failed.is_empty() {
            // e.g. port 80 on Linux needs privileges
            eprintln!("uscl-bridge: couldn't listen on {} (in use, or a port below 1024 on Linux)", failed.join(", "));
            if cfg!(target_os = "linux") && fwd.iter().any(|&(_, p)| p < 1024) {
                let exe = std::env::current_exe().map(|p| p.display().to_string()).unwrap_or("uscl-bridge".into());
                eprintln!("uscl-bridge: to allow ports below 1024 once: sudo setcap cap_net_bind_service=+ep '{exe}'");
            }
        }
    }

    let send = |peer: u64, ch: u32, data: &[u8], reliable: bool| {
        let flags = if reliable { SendFlags::RELIABLE_NO_NAGLE } else { SendFlags::UNRELIABLE_NO_NAGLE };
        let _ = net.send_message_to_user(
            NetworkingIdentity::new_steam_id(SteamId::from_raw(peer)),
            flags | SendFlags::AUTO_RESTART_BROKEN_SESSION,
            data,
            ch,
        );
    };

    let mut member_at: HashMap<u16, u64> = HashMap::new();
    let mut socks: HashMap<u16, UdpSocket> = HashMap::new();
    let mut tcp: HashMap<(u64, u32), TcpStream> = HashMap::new();
    let mut next_conn: u32 = 0;
    let mut next_slot: u16 = 1;
    let mut changed = true;
    let mut last_check = Instant::now();
    let mut host_gone = false;
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
                Ev::Accepted(s, port) => {
                    next_conn += 1;
                    if host {
                        // our own RPCS3: straight to the local RPCN
                        match connect_rpcn() {
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
                            let to: &[u8] = if port == RPCN_TCP { &[] } else { &port.to_be_bytes() };
                            send(host, CH_TCP, &tcp_frame(next_conn, TCP_OPEN, to), true);
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
        // Steam deletes a lobby when its owner's client loses its connection, even for a few seconds. The
        // host makes a new one with the same slots; members follow the host there. RPCN and game traffic
        // go peer to peer, not through the lobby, so they carry on.
        if last_check.elapsed() > Duration::from_secs(2) {
            last_check = Instant::now();
            let list = mm.lobby_members(lobby);
            if host && !list.contains(&me) {
                match create_session(&client, &slot_of, &fwd) {
                    Ok(l) => {
                        lobby = l;
                        changed = true;
                        println!("uscl-bridge: Steam dropped the session; new session {} (members rejoin by themselves)", lobby.raw());
                    }
                    Err(e) => eprintln!("uscl-bridge: session lost, retrying: {e}"),
                }
            } else if !host && !list.contains(&host_id) {
                if !host_gone {
                    host_gone = true;
                    println!("uscl-bridge: the host left the session; waiting for them to start it again (Ctrl+C to stop)");
                }
                let now = client.friends().get_friend(host_id).game_played().map(|g| g.lobby).filter(|l| l.raw() != 0 && *l != lobby);
                if let Some(l) = now.and_then(|l| join_session(&client, l).ok()) {
                    mm.leave_lobby(lobby);
                    lobby = l;
                    changed = true;
                    host_gone = false;
                    println!("uscl-bridge: followed the host to session {}", lobby.raw());
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

        // Forwarded UDP: member side (game <-> host) and host side (member <-> server)
        for (&port, (sock, game)) in fwd_udp.iter_mut() {
            while let Ok((n, src)) = sock.recv_from(&mut buf) {
                *game = Some(src);
                send(host_id.raw(), CH_UDP, &fwd_frame(port, &buf[..n]), false);
            }
        }
        for (&(peer, port), sock) in &fwd_host {
            while let Ok((n, _)) = sock.recv_from(&mut buf) {
                send(peer, CH_UDP, &fwd_frame(port, &buf[..n]), false);
            }
        }

        for m in net.receive_messages_on_channel(CH_UDP, 256) {
            let Some(peer) = m.identity_peer().steam_id().map(|s| s.raw()) else { continue };
            if let [UDP_FWD, a, b, d @ ..] = m.data() {
                let port = u16::from_be_bytes([*a, *b]);
                if host && members.contains(&peer) && fwd.contains(&(true, port)) {
                    if !fwd_host.contains_key(&(peer, port)) {
                        // its own source port, so the server tells members apart by address
                        match bind_udp(0) {
                            Ok(s) => { fwd_host.insert((peer, port), s); }
                            Err(e) => { eprintln!("uscl-bridge: {e}"); continue; }
                        }
                    }
                    let _ = fwd_host[&(peer, port)].send_to(d, local(port));
                } else if peer == host_id.raw() {
                    if let Some((sock, Some(game))) = fwd_udp.get(&port) {
                        let _ = sock.send_to(d, *game);
                    }
                }
                continue;
            }
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
                TCP_OPEN if host => match match d {
                    [] => connect_rpcn(),
                    &[a, b] if fwd.contains(&(false, u16::from_be_bytes([a, b]))) => {
                        TcpStream::connect_timeout(&local(u16::from_be_bytes([a, b])), Duration::from_millis(300))
                    }
                    _ => Err(std::io::Error::other("port isn't forwarded")),
                } {
                    Ok(s) => {
                        if let Ok(r) = s.try_clone() {
                            tcp.insert((peer, id), s);
                            spawn_reader(r, peer, id, tx.clone());
                        }
                    }
                    Err(e) => {
                        let what = if d.is_empty() { format!("RPCN on {HOST_RPCN_TCP}") } else { format!("forwarded port {d:?}") };
                        eprintln!("uscl-bridge: {what} not reachable: {e}");
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

    #[test]
    fn forwards() {
        let f = parse_forwards("skate3").unwrap();
        assert_eq!(f.len(), 2 + 64);
        assert_eq!(&f[..3], &[(false, 42100), (false, 80), (true, 17001)]);
        assert_eq!(parse_forwards(&fwd_spec(&f)).unwrap(), f);
        assert_eq!(parse_forwards("").unwrap(), vec![]);
        for bad in ["tcp", "sctp:1", "udp:0", "udp:5-4", "tcp:70000", "udp:1-1000"] {
            assert!(parse_forwards(bad).is_err(), "{bad}");
        }
    }
}
