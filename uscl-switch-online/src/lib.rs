//! Private USCL protocol, not an implementation of Nintendo's services.
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::time::{Duration, Instant};

pub const PROTOCOL: u32 = 1;
pub const MAX_FRAME: usize = 16 * 1024;
const LEASE: Duration = Duration::from_secs(120);
const MAX_PLAYERS: usize = 256;
const MAX_ROOMS: usize = 64;

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct Game {
    pub title_id: String,
    pub update: String,
    /// Sorted unique content IDs. Empty means no DLC; no key material belongs here.
    pub dlc: Vec<String>,
    pub adapter: u32,
}

impl Game {
    fn valid(&self) -> bool {
        self.title_id.len() == 16
            && self
                .title_id
                .bytes()
                .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_lowercase())
            && !self.update.is_empty()
            && self.update.len() <= 32
            && self
                .update
                .bytes()
                .all(|c| c.is_ascii_alphanumeric() || c == b'.' || c == b'-')
            && self.adapter == 1
            && self.dlc.len() <= 64
            && self.dlc.iter().all(|id| {
                id.len() == 16
                    && id
                        .bytes()
                        .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_lowercase())
            })
            && self.dlc.windows(2).all(|w| w[0] < w[1])
    }
}

#[derive(Debug, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case", deny_unknown_fields)]
pub enum Request {
    Hello {
        protocol: u32,
    },
    Register {
        name: String,
    },
    Heartbeat {
        token: String,
    },
    Logout {
        token: String,
    },
    CreateRoom {
        token: String,
        game: Game,
        region: String,
        capacity: usize,
        visibility: Visibility,
    },
    ListRooms {
        token: String,
        game: Game,
        region: Option<String>,
    },
    JoinRoom {
        token: String,
        room: String,
        game: Game,
        invite: Option<String>,
    },
    LeaveRoom {
        token: String,
    },
    Room {
        token: String,
    },
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Visibility {
    Public,
    Invite,
}

struct Player {
    name: String,
    expires: Instant,
}
struct Room {
    game: Game,
    region: String,
    capacity: usize,
    visibility: Visibility,
    host: String,
    members: Vec<String>,
    invite: String,
}

#[derive(Default)]
pub struct Backend {
    players: BTreeMap<String, Player>,
    rooms: BTreeMap<String, Room>,
}

fn random_id() -> Result<String, &'static str> {
    let mut bytes = [0u8; 32];
    getrandom::getrandom(&mut bytes).map_err(|_| "entropy_unavailable")?;
    Ok(bytes.iter().map(|b| format!("{b:02x}")).collect())
}

fn error(code: &str) -> Value {
    json!({"ok": false, "error": code})
}
fn ok(value: Value) -> Value {
    json!({"ok": true, "result": value})
}

impl Backend {
    pub fn handle(&mut self, req: Request, now: Instant) -> Value {
        self.expire(now);
        if let Request::Hello { protocol } = req {
            return if protocol != PROTOCOL {
                error("protocol_mismatch")
            } else {
                ok(json!({"service":"uscl-switch-online", "protocol":PROTOCOL,
                    "capabilities":["ephemeral_players", "public_rooms", "invite_rooms", "region_filter", "compatibility_filter"],
                    "native_online_play":false, "friends":false, "tournaments":false, "lease_seconds":LEASE.as_secs()}))
            };
        }
        if let Request::Register { name } = req {
            if name.trim() != name
                || !(4..=20).contains(&name.len())
                || !name
                    .bytes()
                    .all(|c| c.is_ascii_alphanumeric() || b"_. -".contains(&c))
            {
                return error("invalid_name");
            }
            if self.players.len() >= MAX_PLAYERS {
                return error("player_limit");
            }
            let token = match random_id() {
                Ok(t) => t,
                Err(e) => return error(e),
            };
            self.players.insert(
                token.clone(),
                Player {
                    name,
                    expires: now + LEASE,
                },
            );
            return ok(json!({"token":token,"lease_seconds":LEASE.as_secs()}));
        }
        let token = match &req {
            Request::Heartbeat { token }
            | Request::Logout { token }
            | Request::CreateRoom { token, .. }
            | Request::ListRooms { token, .. }
            | Request::JoinRoom { token, .. }
            | Request::LeaveRoom { token }
            | Request::Room { token } => token.clone(),
            _ => unreachable!(),
        };
        let Some(player) = self.players.get_mut(&token) else {
            return error("unauthorized");
        };
        player.expires = now + LEASE;
        match req {
            Request::Heartbeat { .. } => ok(json!({"lease_seconds":LEASE.as_secs()})),
            Request::Logout { .. } => {
                self.leave(&token);
                self.players.remove(&token);
                ok(json!({}))
            }
            Request::CreateRoom {
                game,
                region,
                capacity,
                visibility,
                ..
            } => {
                if !game.valid()
                    || region.is_empty()
                    || region.len() > 16
                    || !region.bytes().all(|c| c.is_ascii_uppercase() || c == b'-')
                    || !(2..=12).contains(&capacity)
                {
                    return error("invalid_room");
                }
                if self.membership(&token).is_some() {
                    return error("already_in_room");
                }
                if self.rooms.len() >= MAX_ROOMS {
                    return error("room_limit");
                }
                let (id, invite) = match (random_id(), random_id()) {
                    (Ok(id), Ok(invite)) => (id, invite),
                    _ => return error("entropy_unavailable"),
                };
                self.rooms.insert(
                    id.clone(),
                    Room {
                        game,
                        region,
                        capacity,
                        visibility,
                        host: token.clone(),
                        members: vec![token],
                        invite: invite.clone(),
                    },
                );
                ok(json!({"room":id, "invite":invite}))
            }
            Request::ListRooms { game, region, .. } => {
                if !game.valid() {
                    return error("invalid_game");
                }
                let rooms: Vec<_> = self
                    .rooms
                    .iter()
                    .filter(|(_, r)| {
                        r.visibility == Visibility::Public
                            && r.game == game
                            && region.as_ref().is_none_or(|v| *v == r.region)
                    })
                    .map(|(id, r)| self.view(id, r))
                    .collect();
                ok(json!({"rooms":rooms}))
            }
            Request::JoinRoom {
                room, game, invite, ..
            } => {
                if !game.valid() {
                    return error("invalid_game");
                }
                if self.membership(&token).is_some() {
                    return error("already_in_room");
                }
                let Some(r) = self.rooms.get_mut(&room) else {
                    return error("room_unavailable");
                };
                if r.visibility == Visibility::Invite && invite.as_deref() != Some(&r.invite) {
                    return error("room_unavailable");
                }
                if r.game != game {
                    return error("incompatible_game");
                }
                if r.members.len() >= r.capacity {
                    return error("room_full");
                }
                r.members.push(token);
                ok(json!({"room":room}))
            }
            Request::LeaveRoom { .. } => {
                self.leave(&token);
                ok(json!({}))
            }
            Request::Room { .. } => match self.membership(&token) {
                Some((id, r)) => ok(self.view(id, r)),
                None => error("not_in_room"),
            },
            _ => unreachable!(),
        }
    }

    fn membership(&self, token: &str) -> Option<(&String, &Room)> {
        self.rooms
            .iter()
            .find(|(_, r)| r.members.iter().any(|m| m == token))
    }

    fn view(&self, id: &str, r: &Room) -> Value {
        // Never return bearer tokens or invite secrets in room discovery/member lists.
        let members: Vec<_> = r
            .members
            .iter()
            .filter_map(|t| {
                self.players
                    .get(t)
                    .map(|p| json!({"name":p.name, "host":*t == r.host}))
            })
            .collect();
        json!({"room":id,"game":r.game,"region":r.region,"capacity":r.capacity,"visibility":r.visibility,"members":members})
    }

    fn leave(&mut self, token: &str) {
        // Initial policy: a host departure closes the room; no false host-migration claim.
        self.rooms.retain(|_, r| {
            if r.host == token {
                return false;
            }
            r.members.retain(|t| t != token);
            true
        });
    }

    fn expire(&mut self, now: Instant) {
        let dead: Vec<_> = self
            .players
            .iter()
            .filter(|(_, p)| p.expires <= now)
            .map(|(t, _)| t.clone())
            .collect();
        for token in dead {
            self.leave(&token);
            self.players.remove(&token);
        }
    }
}

/// Read one bounded newline-delimited frame without allocating an unbounded request.
pub fn read_frame(reader: &mut impl std::io::BufRead) -> std::io::Result<Option<Vec<u8>>> {
    let mut frame = Vec::new();
    loop {
        let chunk = reader.fill_buf()?;
        if chunk.is_empty() {
            return if frame.is_empty() {
                Ok(None)
            } else {
                Err(std::io::ErrorKind::UnexpectedEof.into())
            };
        }
        let newline = chunk.iter().position(|b| *b == b'\n');
        let n = newline.map_or(chunk.len(), |p| p + 1);
        if frame.len() + n > MAX_FRAME {
            return Err(std::io::ErrorKind::InvalidData.into());
        }
        frame.extend_from_slice(&chunk[..n]);
        reader.consume(n);
        if newline.is_some() {
            return Ok(Some(frame));
        }
    }
}
