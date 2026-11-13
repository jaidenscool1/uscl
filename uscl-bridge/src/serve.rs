//! `uscl-bridge serve`: the local service RPCS3 talks to.
//!
//! Unix socket at $XDG_RUNTIME_DIR/uscl.sock (Windows: TCP 127.0.0.1:47321, or $USCL_BRIDGE_ADDR),
//! one client at a time, newline-delimited JSON.
//!   request : {"id":1,"op":"create_room","max":4,"data":{"game":"BLUS31426"}}
//!   reply   : {"id":1,"ok":true,"result":{...}}  or  {"id":1,"ok":false,"error":"..."}
//!   event   : {"event":"member","room":"...","user":"...","change":"entered"}   (pushed, no id)
//! Room and user ids are Steam ids as decimal strings (they don't fit a JSON double).
//!
//! ops: hello, create_room{max,data}, search_rooms{filters}, join_room{room}, leave_room{room},
//!      set_room_data{room,data}, room_info{room}, set_presence{text|null}
//! A room created a moment ago can take ~2 s to show up in search_rooms: callers retry.

use serde_json::{json, Map, Value};
use std::io::{BufRead, BufReader, ErrorKind, Write};
use sock::Stream;
use std::sync::mpsc::{channel, Sender};
use std::time::Duration;
use steamworks::{
    ChatMemberStateChange, Client, GameLobbyJoinRequested, LobbyChatUpdate, LobbyDataUpdate, LobbyId, LobbyKey,
    LobbyType, StringFilter, StringFilterKind,
};

#[cfg(unix)]
mod sock {
    pub use std::os::unix::net::{UnixListener as Listener, UnixStream as Stream};

    pub fn socket_path() -> String {
        let dir = std::env::var("XDG_RUNTIME_DIR").unwrap_or_else(|_| "/tmp".into());
        format!("{dir}/uscl.sock")
    }

    pub fn bind(path: &str) -> std::io::Result<Listener> {
        let _ = std::fs::remove_file(path); // stale socket from a previous run
        Listener::bind(path)
    }
}

// Rust's std has no Unix sockets on Windows. A TCP port bound to 127.0.0.1 only: like the Unix socket,
// any local process can connect, nothing outside this PC can.
#[cfg(windows)]
mod sock {
    pub use std::net::{TcpListener as Listener, TcpStream as Stream};

    pub fn socket_path() -> String {
        std::env::var("USCL_BRIDGE_ADDR").unwrap_or_else(|_| "127.0.0.1:47321".into())
    }

    pub fn bind(addr: &str) -> std::io::Result<Listener> {
        Listener::bind(addr)
    }
}

pub use sock::socket_path;

fn room_id(req: &Value) -> Result<LobbyId, String> {
    req["room"].as_str().and_then(|s| s.parse().ok()).map(LobbyId::from_raw).ok_or("missing or bad \"room\"".into())
}

fn room_info(client: &Client, lobby: LobbyId) -> Value {
    let mm = client.matchmaking();
    let mut data = Map::new();
    for i in 0..mm.lobby_data_count(lobby) {
        if let Some((k, v)) = mm.lobby_data_by_index(lobby, i) {
            data.insert(k, Value::String(v));
        }
    }
    let members: Vec<String> = mm.lobby_members(lobby).iter().map(|m| m.raw().to_string()).collect();
    json!({"room": lobby.raw().to_string(), "owner": mm.lobby_owner(lobby).raw().to_string(),
           "members": members, "data": data})
}

fn set_data(client: &Client, lobby: LobbyId, data: &Value) -> Result<(), String> {
    for (k, v) in data.as_object().into_iter().flatten() {
        let v = v.as_str().ok_or(format!("room data \"{k}\" must be a string"))?;
        if !client.matchmaking().set_lobby_data(lobby, k, v) {
            return Err(format!("set_lobby_data \"{k}\" failed (not the room owner?)"));
        }
    }
    Ok(())
}

/// Handle one request. Synchronous ops reply right away; Steam calls reply later through `out`.
fn handle(client: &Client, req: &Value, out: &Sender<Value>) -> Option<Value> {
    let id = req["id"].clone();
    let reply = |r: Result<Value, String>| match r {
        Ok(v) => json!({"id": id, "ok": true, "result": v}),
        Err(e) => json!({"id": id, "ok": false, "error": e}),
    };
    let mm = client.matchmaking();
    let later = out.clone();
    match req["op"].as_str().unwrap_or("") {
        "hello" => Some(reply(Ok(json!({"me": client.user().steam_id().raw().to_string(),
                                       "name": client.friends().name()})))),
        "create_room" => {
            let max = req["max"].as_u64().unwrap_or(4).clamp(1, 250) as u32;
            let data = req["data"].clone();
            let c = client.clone();
            mm.create_lobby(LobbyType::Public, max, move |r| {
                let res = r.map_err(|e| format!("create_lobby: {e:?}"))
                    .and_then(|lobby| set_data(&c, lobby, &data).map(|_| room_info(&c, lobby)));
                let _ = later.send(json_reply(&id, res));
            });
            None
        }
        "search_rooms" => {
            let filters: Vec<(String, String)> = req["filters"].as_object().into_iter().flatten()
                .filter_map(|(k, v)| v.as_str().map(|v| (k.clone(), v.to_string()))).collect();
            for (k, v) in &filters {
                mm.add_request_lobby_list_string_filter(StringFilter(LobbyKey::new(k), v, StringFilterKind::Equal));
            }
            let c = client.clone();
            mm.request_lobby_list(move |r| {
                let res = r.map_err(|e| format!("request_lobby_list: {e:?}"))
                    .map(|rooms| Value::Array(rooms.into_iter().map(|l| room_info(&c, l)).collect()));
                let _ = later.send(json_reply(&id, res));
            });
            None
        }
        "join_room" => match room_id(req) {
            Ok(lobby) => {
                let c = client.clone();
                mm.join_lobby(lobby, move |r| {
                    let res = r.map_err(|_| "join refused (full, closed or gone)".to_string()).map(|l| room_info(&c, l));
                    let _ = later.send(json_reply(&id, res));
                });
                None
            }
            Err(e) => Some(reply(Err(e))),
        },
        "leave_room" => Some(reply(room_id(req).map(|l| {
            mm.leave_lobby(l);
            json!({})
        }))),
        "set_room_data" => Some(reply(room_id(req).and_then(|l| set_data(client, l, &req["data"]).map(|_| room_info(client, l))))),
        "room_info" => Some(reply(room_id(req).map(|l| room_info(client, l)))),
        "set_presence" => {
            let text = req["text"].as_str();
            client.friends().set_rich_presence("status", text);
            Some(reply(Ok(json!({}))))
        }
        other => Some(reply(Err(format!("unknown op \"{other}\"")))),
    }
}

fn json_reply(id: &Value, r: Result<Value, String>) -> Value {
    match r {
        Ok(v) => json!({"id": id, "ok": true, "result": v}),
        Err(e) => json!({"id": id, "ok": false, "error": e}),
    }
}

fn change_name(c: ChatMemberStateChange) -> &'static str {
    match c {
        ChatMemberStateChange::Entered => "entered",
        ChatMemberStateChange::Left => "left",
        ChatMemberStateChange::Disconnected => "disconnected",
        ChatMemberStateChange::Kicked => "kicked",
        ChatMemberStateChange::Banned => "banned",
    }
}

pub fn serve(client: Client) -> Result<(), String> {
    let path = socket_path();
    let listener = sock::bind(&path).map_err(|e| format!("bind {path}: {e}"))?;
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    eprintln!("uscl-bridge: listening on {path}");

    // Everything that goes to the client (late replies + Steam events) funnels through one channel.
    let (out, outbox) = channel::<Value>();
    let ev = out.clone();
    let _h1 = client.register_callback(move |u: LobbyChatUpdate| {
        let _ = ev.send(json!({"event": "member", "room": u.lobby.raw().to_string(),
            "user": u.user_changed.raw().to_string(), "change": change_name(u.member_state_change)}));
    });
    let ev = out.clone();
    let _h2 = client.register_callback(move |u: LobbyDataUpdate| {
        let _ = ev.send(json!({"event": "room_data", "room": u.lobby.raw().to_string(), "member": u.member.raw().to_string()}));
    });
    let ev = out.clone();
    let _h3 = client.register_callback(move |j: GameLobbyJoinRequested| {
        // a friend clicked "Join game" / accepted a Steam invite: RPCS3 should join this room
        let _ = ev.send(json!({"event": "join_requested", "room": j.lobby_steam_id.raw().to_string(),
            "friend": j.friend_steam_id.raw().to_string()}));
    });

    let mut conn: Option<(Stream, BufReader<Stream>)> = None;
    let mut line = String::new();
    loop {
        client.run_callbacks();
        if conn.is_none() {
            match listener.accept() {
                Ok((s, _)) => {
                    s.set_nonblocking(true).map_err(|e| e.to_string())?;
                    let r = BufReader::new(s.try_clone().map_err(|e| e.to_string())?);
                    conn = Some((s, r));
                }
                Err(e) if e.kind() == ErrorKind::WouldBlock => {}
                Err(e) => return Err(e.to_string()),
            }
        }
        let mut drop_conn = false;
        if let Some((w, r)) = conn.as_mut() {
            loop {
                line.clear();
                match r.read_line(&mut line) {
                    Ok(0) => {
                        drop_conn = true;
                        break;
                    }
                    Ok(_) => {
                        let reply = match serde_json::from_str::<Value>(&line) {
                            Ok(req) => handle(&client, &req, &out),
                            Err(e) => Some(json!({"id": null, "ok": false, "error": format!("bad json: {e}")})),
                        };
                        if let Some(v) = reply {
                            let _ = out.send(v);
                        }
                    }
                    Err(e) if e.kind() == ErrorKind::WouldBlock => break,
                    Err(_) => {
                        drop_conn = true;
                        break;
                    }
                }
            }
            while let Ok(v) = outbox.try_recv() {
                if writeln!(w, "{v}").is_err() {
                    drop_conn = true;
                    break;
                }
            }
        } else {
            while outbox.try_recv().is_ok() {} // nobody listening: drop events
        }
        if drop_conn {
            conn = None;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

/// `uscl-bridge client-test`: talks to a running `serve` the way RPCS3 will. No Steam in this process.
pub fn client_test() -> Result<(), String> {
    let path = socket_path();
    let s = Stream::connect(&path).map_err(|e| format!("connect {path}: {e} (is `uscl-bridge serve` running?)"))?;
    s.set_read_timeout(Some(Duration::from_secs(30))).map_err(|e| e.to_string())?;
    let mut w = s.try_clone().map_err(|e| e.to_string())?;
    let mut r = BufReader::new(s);
    let mut next_id = 0;
    let mut call = |req: Value| -> Result<Value, String> {
        next_id += 1;
        let mut req = req;
        req["id"] = json!(next_id);
        writeln!(w, "{req}").map_err(|e| e.to_string())?;
        loop {
            let mut line = String::new();
            r.read_line(&mut line).map_err(|e| format!("read: {e}"))?;
            let v: Value = serde_json::from_str(&line).map_err(|e| e.to_string())?;
            if v.get("event").is_some() {
                println!("  event: {v}");
                continue;
            }
            if v["id"] != json!(next_id) {
                continue;
            }
            return if v["ok"] == json!(true) { Ok(v["result"].clone()) } else { Err(v["error"].to_string()) };
        }
    };

    let hello = call(json!({"op": "hello"}))?;
    let me = hello["me"].as_str().unwrap_or("").to_string();
    println!("hello: me={me}");
    let tag = format!("{}", std::process::id());
    let room = call(json!({"op": "create_room", "max": 4, "data": {"uscl": tag, "game": "BLUS31426"}}))?;
    let rid = room["room"].as_str().unwrap_or("").to_string();
    println!("create_room: {rid} owner={}", room["owner"]);
    let mut found = false;
    for attempt in 1..=5 {
        let rooms = call(json!({"op": "search_rooms", "filters": {"uscl": tag}}))?;
        found = rooms.as_array().into_iter().flatten().any(|r| r["room"] == json!(rid));
        println!("search_rooms attempt {attempt}: {}", if found { "found" } else { "not yet" });
        if found {
            break;
        }
        std::thread::sleep(Duration::from_secs(2));
    }
    let info = call(json!({"op": "set_room_data", "room": rid, "data": {"state": "open"}}))?;
    let data_ok = info["data"]["state"] == json!("open") && info["data"]["game"] == json!("BLUS31426");
    let owner_ok = info["owner"] == json!(me) && info["members"].as_array().is_some_and(|m| m.contains(&json!(me)));
    call(json!({"op": "set_presence", "text": "USCL client-test"}))?;
    call(json!({"op": "leave_room", "room": rid}))?;
    call(json!({"op": "set_presence", "text": null}))?;
    let bad = call(json!({"op": "nope"})).is_err();
    println!("owner={owner_ok} data={data_ok} search={found} unknown_op_rejected={bad}");
    if owner_ok && data_ok && found && bad { Ok(()) } else { Err("client round trip incomplete".into()) }
}
