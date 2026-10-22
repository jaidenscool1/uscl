//! uscl-bridge: the Steam side of USCL (The Ultimate Steam Compatibility Layer).
//! Phase 2 proof of concept: `uscl-bridge selftest` does a full lobby round trip against real Steam.
//! Needs the Steam client running and logged in. Uses Steam's public test app 480 until USCL has its own.

use std::sync::mpsc::{channel, Receiver};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use steamworks::{Client, LobbyId, LobbyKey, LobbyType, StringFilter, StringFilterKind};

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
            let join = std::env::args().nth(2).map(|s| s.parse().map(steamworks::LobbyId::from_raw).unwrap_or_else(|_| {
                eprintln!("session id must be the number the host printed");
                std::process::exit(2);
            }));
            let client = Client::init_app(APP_ID).unwrap_or_else(|e| {
                eprintln!("Steam init failed (is Steam running?): {e:?}");
                std::process::exit(1);
            });
            if let Err(e) = tunnel::run(client, cmd == "host", join) {
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
        Some("client-test") => match serve::client_test() {
            Ok(()) => println!("PASS"),
            Err(e) => {
                eprintln!("FAIL: {e}");
                std::process::exit(1);
            }
        },
        _ => {
            eprintln!("usage: uscl-bridge host | join [session id] | selftest | serve | client-test");
            std::process::exit(2);
        }
    }
}
