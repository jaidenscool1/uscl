//! uscl-bridge: the Steam side of USCL (The Ultimate Steam Compatibility Layer).
//! Phase 2 proof of concept: `uscl-bridge selftest` does a full lobby round trip against real Steam.
//! Needs the Steam client running and logged in. Uses Steam's public test app 480 until USCL has its own.

use std::sync::mpsc::{channel, Receiver};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use steamworks::{Client, FriendFlags, FriendState, LobbyId, LobbyKey, LobbyType, StringFilter, StringFilterKind};

mod progress;
mod serve;
mod tunnel;

const APP_ID: u32 = 480;

/// Pump Steam callbacks until `rx` delivers a value or `secs` pass.
fn wait<T>(client: &Client, rx: &Receiver<T>, secs: u64, what: &str) -> Result<T, String> {
    let deadline = Instant::now() + Duration::from_secs(secs);
    while Instant::now() < deadline {
        client.run_callbacks();
        if let Ok(v) = rx.try_recv() {
            return Ok(v);
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    Err(format!("timed out waiting for {what}"))
}

fn selftest() -> Result<(), String> {
    let client = Client::init_app(APP_ID).map_err(|e| format!("Steam init failed (is Steam running?): {e:?}"))?;
    let me = client.user().steam_id();
    println!("signed in as {} ({})", client.friends().name(), me.raw());

    // Rich Presence: what friends see next to your name
    client.friends().set_rich_presence("status", Some("USCL selftest"));

    let mm = client.matchmaking();
    let (tx, rx) = channel();
    mm.create_lobby(LobbyType::Public, 4, move |r| {
        let _ = tx.send(r);
    });
    let lobby: LobbyId = wait(&client, &rx, 20, "lobby creation")?.map_err(|e| format!("create_lobby: {e:?}"))?;
    println!("created lobby {}", lobby.raw());

    // A unique tag so the search below finds this lobby and not someone else's test-app lobby.
    let nonce = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos().to_string();
    if !(mm.set_lobby_data(lobby, "uscl", &nonce) && mm.set_lobby_data(lobby, "game", "BLUS31426")) {
        return Err("set_lobby_data failed".into());
    }

    // Lobby search: what sceNpMatching2GetRoomDataExternalList will map to.
    let mut found = false;
    for attempt in 1..=5 {
        let (tx, rx) = channel();
        mm.add_request_lobby_list_string_filter(StringFilter(LobbyKey::new("uscl"), &nonce, StringFilterKind::Equal));
        mm.request_lobby_list(move |r| {
            let _ = tx.send(r);
        });
        let list = wait(&client, &rx, 20, "lobby list")?.map_err(|e| format!("request_lobby_list: {e:?}"))?;
        found = list.contains(&lobby);
        println!("search attempt {attempt}: {} match(es), ours {}", list.len(), if found { "found" } else { "not yet" });
        if found {
            break;
        }
        std::thread::sleep(Duration::from_secs(2));
    }

    let owner_ok = mm.lobby_owner(lobby) == me;
    let member_ok = mm.lobby_members(lobby).contains(&me);
    let data_ok = mm.lobby_data(lobby, "game").as_deref() == Some("BLUS31426");
    mm.leave_lobby(lobby);
    client.friends().set_rich_presence("status", None);
    client.run_callbacks();

    println!("owner={owner_ok} member={member_ok} data={data_ok} search={found}");
    if owner_ok && member_ok && data_ok && found {
        Ok(())
    } else {
        Err("lobby round trip incomplete".into())
    }
}

/// `friends [avatar dir]`: Steam friends, as a JSON array for the USCL home screen. "session" is the USCL session
/// (lobby) a friend is in, as a string because it doesn't fit a JS number. With a folder, each friend's Steam
/// profile picture is saved there as <steam id>.ppm and named in "avatar".
fn friends() -> Result<(), String> {
    let client = Client::init_app(APP_ID).map_err(|e| format!("Steam init failed (is Steam running?): {e:?}"))?;
    let app = client.utils().app_id();
    let avatar_dir = std::env::args().nth(2).map(std::path::PathBuf::from);
    let all = client.friends().get_friends(FriendFlags::IMMEDIATE);
    // Steam loads big pictures on request: ask, give it a moment, then take what's there (medium otherwise)
    let mut pictures = std::collections::HashMap::new();
    if avatar_dir.is_some() {
        // levels (and big pictures) arrive with the friend's full profile
        for f in &all {
            client.friends().request_user_information(f.id(), false);
        }
        let start = std::time::Instant::now();
        loop {
            for f in &all {
                if !pictures.contains_key(&f.id().raw()) {
                    if let Some(rgba) = f.large_avatar() {
                        pictures.insert(f.id().raw(), (184, rgba));
                    }
                }
            }
            if pictures.len() == all.len() || start.elapsed() > std::time::Duration::from_millis(1500) {
                break;
            }
            client.run_callbacks();
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        for f in &all {
            if !pictures.contains_key(&f.id().raw()) {
                if let Some(rgba) = f.medium_avatar() {
                    pictures.insert(f.id().raw(), (64, rgba));
                }
            }
        }
    }
    let list: Vec<serde_json::Value> = all
        .iter()
        .map(|f| {
            let state = f.state();
            let session = f.game_played().filter(|g| g.game.app_id() == app && g.lobby.raw() != 0).map(|g| g.lobby.raw().to_string());
            let avatar = match (&avatar_dir, pictures.get(&f.id().raw())) {
                (Some(dir), Some((size, rgba))) => {
                    let path = dir.join(format!("{}.ppm", f.id().raw()));
                    std::fs::write(&path, ppm(*size, rgba)).ok().map(|_| path.to_string_lossy().into_owned())
                }
                _ => None,
            };
            // Steam level: the ★ number on the PS3's friend card
            let level = unsafe {
                steamworks_sys::SteamAPI_ISteamFriends_GetFriendSteamLevel(steamworks_sys::SteamAPI_SteamFriends_v018(), f.id().raw())
            };
            let color = pictures.get(&f.id().raw()).map(|(_, rgba)| card_color(rgba));
            let playing = f.game_played().is_some_and(|g| g.game.app_id() != app);
            serde_json::json!({"id": f.id().raw().to_string(), "name": f.name(), "state": format!("{state:?}"),
                "online": !matches!(state, FriendState::Offline | FriendState::Invisible), "session": session, "avatar": avatar,
                "level": level, "color": color, "playing": playing})
        })
        .collect();
    println!("{}", serde_json::Value::Array(list));
    Ok(())
}

/// The PS3 colours each friend's card with their profile colour; Steam has none, so take the picture's average,
/// brightened so the card reads as a colour rather than grey, as "#rrggbb"
fn card_color(rgba: &[u8]) -> String {
    let (mut r, mut g, mut b, mut n) = (0u64, 0u64, 0u64, 0u64);
    for px in rgba.chunks_exact(4) {
        r += px[0] as u64;
        g += px[1] as u64;
        b += px[2] as u64;
        n += 1;
    }
    let n = n.max(1);
    let (r, g, b) = ((r / n) as f64, (g / n) as f64, (b / n) as f64);
    let max = r.max(g).max(b).max(1.0);
    let scale = 200.0 / max;
    format!("#{:02x}{:02x}{:02x}", (r * scale).min(255.0) as u8, (g * scale).min(255.0) as u8, (b * scale).min(255.0) as u8)
}

/// A square RGBA picture as a binary PPM (RGB), which Qt reads without extra image plugins
fn ppm(size: usize, rgba: &[u8]) -> Vec<u8> {
    let mut out = format!("P6\n{size} {size}\n255\n").into_bytes();
    for px in rgba.chunks_exact(4).take(size * size) {
        out.extend_from_slice(&px[..3]);
    }
    out
}

#[cfg(test)]
mod picture_tests {
    #[test]
    fn card_color_brightens_the_average() {
        assert_eq!(super::card_color(&[100, 50, 0, 255, 100, 50, 0, 255]), "#c86400");
    }

    #[test]
    fn ppm_drops_alpha() {
        let out = super::ppm(1, &[1, 2, 3, 255]);
        assert_eq!(out, b"P6\n1 1\n255\n\x01\x02\x03".to_vec());
    }
}

fn main() {
    match std::env::args().nth(1).as_deref() {
        Some("selftest") => match selftest() {
            Ok(()) => println!("PASS"),
            Err(e) => {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        },
        Some(cmd @ ("host" | "join")) => {
            let arg = std::env::args().nth(2);
            let (join, spec) = if cmd == "host" {
                let spec = arg.unwrap_or_default();
                if let Err(e) = tunnel::parse_forwards(&spec) {
                    eprintln!("{e}");
                    std::process::exit(2);
                }
                (None, spec)
            } else {
                let join = arg.map(|s| s.parse().map(steamworks::LobbyId::from_raw).unwrap_or_else(|_| {
                    eprintln!("session id must be the number the host printed");
                    std::process::exit(2);
                }));
                (join, String::new())
            };
            let client = Client::init_app(APP_ID).unwrap_or_else(|e| {
                eprintln!("Steam init failed (is Steam running?): {e:?}");
                std::process::exit(1);
            });
            if let Err(e) = tunnel::run(client, cmd == "host", join, &spec) {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        }
        Some("serve") => {
            let client = Client::init_app(APP_ID).unwrap_or_else(|e| {
                eprintln!("Steam init failed (is Steam running?): {e:?}");
                std::process::exit(1);
            });
            if let Err(e) = serve::serve(client) {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        }
        Some("friends") => {
            if let Err(e) = friends() {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        }
        Some("client-test") => match serve::client_test() {
            Ok(()) => println!("PASS"),
            Err(e) => {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        },
        _ => {
            eprintln!("usage: uscl-bridge host [skate3 | bo2 | tcp:PORT,udp:FIRST-LAST,sudp:PORT,...] | join [session id] | friends | selftest | serve | client-test");
            std::process::exit(2);
        }
    }
}
