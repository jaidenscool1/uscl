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
//!
//! Removing players: the host reads Steam IDs from USCL_BANS (for good) and USCL_KICKS (this session), one per
//! line, and lists the ones in the lobby in lobby data "removed". Every bridge drops their traffic both ways and
//! closes their connections; a removed player's own bridge leaves. Steam IDs come from Steam's authenticated
//! connections, so a removed player can't come back under a new name. USCL_GAME ("<serial> <hash>") goes in
//! lobby data "game" so members can check they run the same game files as the host.

use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::io::{Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream, UdpSocket};
use std::sync::mpsc::{channel, Sender};
use std::sync::Arc;

use crate::progress;
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
// sudp: the host's server listens this much higher than the port the game sends to (bridges own the game's ports)
const SLOT_SERVER_OFFSET: u16 = 100;

const CH_TCP: u32 = 1;
const CH_UDP: u32 = 2;
const CH_PROGRESS: u32 = 3; // a player's saved progress (progress.rs): member -> host on join, host -> member after
// shortcut: copies go back to members every minute (and on change only); a stats upload just before someone leaves can miss the last one
const PROGRESS_EVERY: Duration = Duration::from_secs(60);
const UDP_RPCN: u8 = 0;
const UDP_P2P: u8 = 1;
const UDP_FWD: u8 = 2; // [port u16 BE][data]
const UDP_P2P_PORT: u8 = 3; // [sender's local port u16 BE][data]: delivered to the same port on the other side
const UDP_SLOT: u8 = 4; // [server port u16 BE][data]: member <-> host's server, sent from the member's slot port
const TCP_OPEN: u8 = 0; // payload: empty = RPCN, or a forwarded port u16 BE
const TCP_DATA: u8 = 1;
const TCP_CLOSE: u8 = 2;

/// Port lists for games played through a community server (see games/<serial>.md).
fn preset(name: &str) -> Option<&'static str> {
    match name {
        // Skate3BlazeServer: Blaze, HTTP, one UDP relay per lobby at 17000 + game id
        "skate3" => Some("tcp:42100,tcp:80,udp:17001-17064"),
        // uscl-dw on DW_ADDR 127.0.0.1:3074 (login + lobby over TCP), its content server on TCP 3076 (file share:
        // emblems, served as http://127.0.0.1:3076/...) and DW_STUN_ADDR 127.0.0.1:3174 (+ alt 3177, 3178): NAT
        // discovery through each player's slot port, so it reports the address other players reach them at
        "bo2" => Some("tcp:3074,tcp:3076,sudp:3074,sudp:3077-3078"),
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
        let udp = match proto { "tcp" => false, "udp" => true, "sudp" => continue, _ => return Err(bad()) };
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

/// "sudp:3074,sudp:3077-3078" items of a forward spec: UDP to the host's server that leaves the host from the
/// player's slot port (so the server sees the player's tunnel address). Every bridge, the host's too, binds them for
/// its own game; the host's server listens SLOT_SERVER_OFFSET higher. The game's socket that sends there is the one
/// its tunnel address stands for, so peers' game traffic is delivered to it (`slot_game`).
pub fn parse_slot_udp(spec: &str) -> Vec<u16> {
    let spec = preset(spec).unwrap_or(spec);
    let mut out = Vec::new();
    for item in spec.split(',') {
        let Some(ports) = item.strip_prefix("sudp:") else { continue };
        let (a, b) = ports.split_once('-').unwrap_or((ports, ports));
        if let (Ok(a), Ok(b)) = (a.parse::<u16>(), b.parse::<u16>()) {
            if a != 0 && a <= b && b - a < 16 {
                out.extend(a..=b);
            }
        }
    }
    out
}

fn slot_frame(port: u16, data: &[u8]) -> Vec<u8> {
    let mut f = vec![UDP_SLOT];
    f.extend_from_slice(&port.to_be_bytes());
    f.extend_from_slice(data);
    f
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

/// Game traffic is delivered to the port it was sent from: RPCS3's P2P port for RPCN games, the game's own UDP
/// socket for others (Demonware titles: 3074-3079). Nothing else, so a peer can't reach other local services
/// through us. With sudp the receiver's own game socket is known and wins (`delivery_port`): games fall back to
/// different free ports on each PC (BO2: 3075 on one, 3076 on the other), so "the same port" isn't theirs.
fn p2p_port_ok(port: u16) -> bool {
    port == RPCS3_P2P || (3074..=3079).contains(&port)
}

/// Where a peer's game packet sent from `port` goes on this PC, if anywhere.
// shortcut: all of a peer's game sockets reach our one slot-address socket (a NAT would give each its own port);
// enough for BO2/MW2, which play from the socket they ran NAT discovery on
fn delivery_port(port: u16, slot_game: Option<SocketAddr>) -> Option<u16> {
    match slot_game {
        _ if !p2p_port_ok(port) => None,
        Some(g) if port != RPCS3_P2P => Some(g.port()),
        _ => Some(port),
    }
}

fn p2p_frame(port: u16, data: &[u8]) -> Vec<u8> {
    let mut f = vec![UDP_P2P_PORT];
    f.extend_from_slice(&port.to_be_bytes());
    f.extend_from_slice(data);
    f
}

fn local(port: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], port))
}

/// Our game's socket at `port`. Windows lets a game bind 0.0.0.0 on a port we hold on 127.0.0.1 (BO2 takes 3074
/// there), and then 127.0.0.1:port is us; 127.0.0.2 reaches only the game. Linux refuses that bind, so it never
/// happens there.
fn game_addr(port: u16, ours: &HashMap<u16, UdpSocket>) -> SocketAddr {
    if ours.contains_key(&port) { SocketAddr::from(([127, 0, 0, 2], port)) } else { local(port) }
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
    Progress { to: u64, bundle: String }, // a bundle to send (member: ours to the host; host: a member's copy back)
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

/// Steam IDs, one per line ("<id> <name>", # comments): ban and kick lists, and lobby data "removed".
fn parse_ids(text: &str) -> HashSet<u64> {
    text.split([',', '\n'])
        .filter_map(|l| l.split_whitespace().next())
        .filter(|w| !w.starts_with('#'))
        .filter_map(|w| w.parse().ok())
        .collect()
}

fn read_ids(var: &str) -> HashSet<u64> {
    std::env::var(var).ok().and_then(|p| std::fs::read_to_string(p).ok()).map(|t| parse_ids(&t)).unwrap_or_default()
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
fn create_session(client: &Client, slots: &HashMap<u64, u16>, fwd: &[(bool, u16)], sudp: &[u16], game: &str) -> Result<LobbyId, String> {
    let mm = client.matchmaking();
    let (t, r) = channel();
    mm.create_lobby(LobbyType::FriendsOnly, 250, move |res| {
        let _ = t.send(res);
    });
    let lobby = wait(client, &r, "lobby creation")?.map_err(|e| format!("create_lobby: {e:?}"))?;
    mm.set_lobby_data(lobby, "uscl", "session");
    mm.set_lobby_data(lobby, "fwd", &fwd_spec(fwd));
    let sudp: Vec<String> = sudp.iter().map(|p| format!("sudp:{p}")).collect();
    mm.set_lobby_data(lobby, "sudp", &sudp.join(","));
    mm.set_lobby_data(lobby, "game", game);
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
pub fn run(client: Client, host: bool, join: Option<LobbyId>, spec: &str) -> Result<(), String> {
    let fwd = parse_forwards(spec)?;
    let sudp = parse_slot_udp(spec);
    client.networking_utils().init_relay_network_access();
    let mm = client.matchmaking();
    let net = client.networking_messages();
    let me = client.user().steam_id();
    let game = std::env::var("USCL_GAME").unwrap_or_default();
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
        create_session(&client, &slot_of, &fwd, &sudp, &game)?
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

    // progress travels with the player: a member hands the host its own copy (exported off the loop, it runs uscl-dw)
    let carry = progress::Carry::from_env().map(Arc::new);
    if let (Some(c), false) = (carry.clone(), host) {
        let (t, to) = (tx.clone(), host_id.raw());
        std::thread::spawn(move || match c.export() {
            Ok(bundle) => {
                let _ = t.send(Ev::Progress { to, bundle });
            }
            Err(e) => eprintln!("uscl-bridge: progress: couldn't read {}'s progress: {e}", c.player),
        });
    }
    let mut carried: HashMap<u64, String> = HashMap::new(); // host: member -> their online id
    let mut sent_back: HashMap<u64, String> = HashMap::new(); // host: the last copy each member got
    let mut last_progress = Instant::now();

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
    let sudp = if host { sudp } else { parse_slot_udp(&mm.lobby_data(lobby, "sudp").unwrap_or_default()) };
    // our sockets on the server's ports, and the game socket that last used them
    let mut slot_udp: HashMap<u16, UdpSocket> = HashMap::new();
    let mut slot_game: Option<SocketAddr> = None;
    for &port in &sudp {
        match bind_udp(port) {
            Ok(sock) => { slot_udp.insert(port, sock); }
            Err(e) => eprintln!("uscl-bridge: {e}"),
        }
    }
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

    // players the host removed (see the top of this file)
    let removed: RefCell<HashSet<u64>> = RefCell::new(HashSet::new());
    let send = |peer: u64, ch: u32, data: &[u8], reliable: bool| {
        if removed.borrow().contains(&peer) {
            return;
        }
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
    let mut players = String::new();
    let mut host_game = String::new();
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
                Ev::Progress { to, bundle } => {
                    if bundle.len() > progress::MAX_BUNDLE {
                        eprintln!("uscl-bridge: progress: {} KB is too big to carry", bundle.len() / 1024);
                    } else if !host || sent_back.get(&to) != Some(&bundle) {
                        send(to, CH_PROGRESS, bundle.as_bytes(), true);
                        if host {
                            sent_back.insert(to, bundle);
                        }
                    }
                }
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
            let now: Vec<_> = list.iter().map(|m| serde_json::json!({"id": m.raw().to_string(), "name": client.friends().get_friend(*m).name(), "me": *m == me})).collect();
            let now = serde_json::Value::from(now).to_string();
            if now != players {
                println!("uscl-bridge: players {now}");
                players = now;
            }
            if !host {
                let g = mm.lobby_data(lobby, "game").unwrap_or_default();
                if g != host_game {
                    println!("uscl-bridge: game {g}");
                    host_game = g;
                }
            }
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
            let now = if host {
                let mut ids = read_ids("USCL_BANS");
                ids.extend(read_ids("USCL_KICKS"));
                ids.remove(&me.raw());
                let in_lobby: Vec<String> = list.iter().filter(|m| ids.contains(&m.raw())).map(|m| m.raw().to_string()).collect();
                if mm.lobby_data(lobby, "removed").unwrap_or_default() != in_lobby.join(",") {
                    mm.set_lobby_data(lobby, "removed", &in_lobby.join(","));
                }
                ids
            } else {
                parse_ids(&mm.lobby_data(lobby, "removed").unwrap_or_default())
            };
            if now.contains(&me.raw()) {
                mm.leave_lobby(lobby);
                return Err("the host removed you from the session".into());
            }
            if now != *removed.borrow() {
                // their connections to the host's servers end here
                tcp.retain(|(peer, _), s| {
                    let keep = !now.contains(peer);
                    if !keep {
                        let _ = s.shutdown(Shutdown::Both);
                    }
                    keep
                });
                fwd_host.retain(|(peer, _), _| !now.contains(peer));
                *removed.borrow_mut() = now;
            }
            if host && !list.contains(&me) {
                match create_session(&client, &slot_of, &fwd, &sudp, &game) {
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
            if req.remote().steam_id().is_some_and(|id| members.contains(&id.raw()) && !removed.borrow().contains(&id.raw())) {
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
                // the server's answer to this slot's player, from the port they asked on (the game sees no offset)
                if let Some(port) = src.port().checked_sub(SLOT_SERVER_OFFSET).filter(|p| host && sudp.contains(p)) {
                    if Some(slot) == my_slot {
                        if let (Some(sock), Some(game)) = (slot_udp.get(&port), slot_game) {
                            let _ = sock.send_to(&buf[..n], game_addr(game.port(), &slot_udp));
                        }
                    } else if let Some(&peer) = member_at.get(&slot) {
                        send(peer, CH_UDP, &slot_frame(port, &buf[..n]), false);
                    }
                    continue;
                }
                if from_rpcn && Some(slot) == my_slot {
                    let _ = rpcn_sock.send_to(&buf[..n], local(RPCS3_P2P));
                } else if Some(slot) != my_slot {
                    if let Some(&peer) = member_at.get(&slot) {
                        let f = if from_rpcn {
                            [&[UDP_RPCN][..], &buf[..n]].concat()
                        } else {
                            p2p_frame(src.port(), &buf[..n])
                        };
                        send(peer, CH_UDP, &f, false);
                    }
                }
            }
        }

        // Slot UDP: our game's request to the host's server, from our slot port (the host's own game included)
        for (&port, sock) in &slot_udp {
            while let Ok((n, src)) = sock.recv_from(&mut buf) {
                slot_game = Some(src);
                if !host {
                    send(host_id.raw(), CH_UDP, &slot_frame(port, &buf[..n]), false);
                } else if let Some(s) = my_slot.and_then(|s| socks.get(&s)) {
                    let _ = s.send_to(&buf[..n], local(port + SLOT_SERVER_OFFSET));
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
            if removed.borrow().contains(&peer) {
                continue;
            }
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
            if let [UDP_SLOT, a, b, d @ ..] = m.data() {
                let port = u16::from_be_bytes([*a, *b]);
                if host && sudp.contains(&port) {
                    // to the server from the member's slot port
                    if let Some(sock) = slot_of.get(&peer).and_then(|s| socks.get(s)) {
                        let _ = sock.send_to(d, local(port + SLOT_SERVER_OFFSET));
                    }
                } else if peer == host_id.raw() {
                    // the server's answer, from the same port it came from on the host
                    if let (Some(sock), Some(game)) = (slot_udp.get(&port), slot_game) {
                        let _ = sock.send_to(d, game_addr(game.port(), &slot_udp));
                    }
                }
                continue;
            }
            let Some(sock) = slot_of.get(&peer).and_then(|s| socks.get(s)) else { continue };
            match m.data().split_first() {
                Some((&UDP_RPCN, d)) if host => { let _ = sock.send_to(d, local(HOST_RPCN_UDP)); }
                Some((&UDP_RPCN, d)) if peer == host_id.raw() => { let _ = rpcn_sock.send_to(d, local(RPCS3_P2P)); }
                Some((&UDP_P2P, d)) => { let _ = sock.send_to(d, local(RPCS3_P2P)); }
                Some((&UDP_P2P_PORT, [a, b, d @ ..])) => {
                    if let Some(port) = delivery_port(u16::from_be_bytes([*a, *b]), slot_game) {
                        let _ = sock.send_to(d, game_addr(port, &slot_udp));
                    }
                }
                _ => {}
            }
        }

        // progress: host hands members' bundles to its uscl-dw (with the Steam id Steam vouched for); member keeps
        // the host's copies at home. uscl-dw work runs off the loop.
        for m in net.receive_messages_on_channel(CH_PROGRESS, 16) {
            let Some(peer) = m.identity_peer().steam_id().map(|s| s.raw()) else { continue };
            let (Some(c), Ok(bundle)) = (carry.clone(), String::from_utf8(m.data().to_vec())) else { continue };
            if removed.borrow().contains(&peer) || bundle.len() > progress::MAX_BUNDLE {
                continue;
            }
            if host && members.contains(&peer) {
                let Some(name) = progress::owner(&bundle).filter(|n| progress::valid_name(n)) else { continue };
                carried.insert(peer, name.clone());
                std::thread::spawn(move || match progress::hand_in(&bundle, peer) {
                    Ok(state) => println!("uscl-bridge: progress: {name}'s progress {state}"),
                    Err(e) => eprintln!("uscl-bridge: progress: {name}'s progress refused: {e}"),
                });
            } else if !host && peer == host_id.raw() && progress::owner(&bundle).as_deref() == Some(c.player.as_str()) {
                std::thread::spawn(move || match c.import(&bundle) {
                    Ok(said) => println!("uscl-bridge: progress: kept at home ({said})"),
                    Err(e) => eprintln!("uscl-bridge: progress: couldn't keep it at home: {e}"),
                });
            }
        }
        if host && last_progress.elapsed() > PROGRESS_EVERY {
            last_progress = Instant::now();
            for (&peer, name) in carried.iter().filter(|(p, _)| members.contains(p)) {
                let (t, name) = (tx.clone(), name.clone());
                std::thread::spawn(move || match progress::fetch(&name) {
                    Ok(Some(bundle)) => {
                        let _ = t.send(Ev::Progress { to: peer, bundle });
                    }
                    Ok(None) => {} // not logged in here yet
                    Err(e) => eprintln!("uscl-bridge: progress: couldn't read {name}'s progress here: {e}"),
                });
            }
        }

        for m in net.receive_messages_on_channel(CH_TCP, 256) {
            let Some(peer) = m.identity_peer().steam_id().map(|s| s.raw()) else { continue };
            if removed.borrow().contains(&peer) {
                continue;
            }
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
    fn p2p_frames_keep_the_port() {
        assert_eq!(p2p_frame(3076, b"x"), [UDP_P2P_PORT, 0x0c, 0x04, b'x']);
        assert!(p2p_port_ok(RPCS3_P2P) && p2p_port_ok(3076));
        for port in [RPCN_TCP, RPCN_UDP, HOST_RPCN_UDP, SLOT_BASE + 1, 80, 22] {
            assert!(!p2p_port_ok(port), "{port}");
        }
    }

    #[test]
    fn game_traffic_reaches_our_game_socket() {
        // two-PC BO2: the member plays from 3076, the host's game from 3075
        let ours = Some(local(3075));
        assert_eq!(delivery_port(3076, ours), Some(3075));
        assert_eq!(delivery_port(3076, None), Some(3076));
        assert_eq!(delivery_port(RPCS3_P2P, ours), Some(RPCS3_P2P));
        assert_eq!(delivery_port(22, ours), None);
        assert_eq!(delivery_port(SLOT_BASE + 1, ours), None);
    }

    #[test]
    fn a_game_sharing_our_port_is_reached_on_127_0_0_2() {
        let ours = HashMap::from([(3074, bind_udp(0).unwrap())]);
        assert_eq!(game_addr(3074, &ours), SocketAddr::from(([127, 0, 0, 2], 3074)));
        assert_eq!(game_addr(3075, &ours), local(3075));
    }

    #[test]
    fn slot_udp_forwards() {
        assert_eq!(parse_slot_udp("bo2"), vec![3074, 3077, 3078]);
        assert_eq!(parse_forwards("bo2").unwrap(), vec![(false, 3074), (false, 3076)]);
        assert_eq!(parse_slot_udp("skate3"), Vec::<u16>::new());
        assert_eq!(slot_frame(3074, b"x"), [UDP_SLOT, 0x0c, 0x02, b'x']);
    }

    #[test]
    fn ids() {
        let bans = "# USCL bans\n76561198000000001 Cheater One\n\n76561198000000002\nnot-an-id\n";
        assert_eq!(parse_ids(bans), HashSet::from([76561198000000001, 76561198000000002]));
        assert_eq!(parse_ids("76561198000000003,76561198000000004"), HashSet::from([76561198000000003, 76561198000000004]));
        assert!(parse_ids("").is_empty());
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
