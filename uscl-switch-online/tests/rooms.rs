use serde_json::{Value, json};
use std::time::{Duration, Instant};
use uscl_switch_online::{Backend, MAX_FRAME, read_frame};

fn call(b: &mut Backend, req: Value, now: Instant) -> Value {
    b.handle(serde_json::from_value(req).unwrap(), now)
}
fn register(b: &mut Backend, name: &str, now: Instant) -> String {
    call(b, json!({"op":"register","name":name}), now)["result"]["token"]
        .as_str()
        .unwrap()
        .into()
}
fn game() -> Value {
    json!({"title_id":"0100152000022000","update":"4.0.1","dlc":[],"adapter":1})
}
fn create(b: &mut Backend, token: &str, visibility: &str, now: Instant) -> Value {
    call(
        b,
        json!({"op":"create_room","token":token,"game":game(),"region":"NA","capacity":2,"visibility":visibility}),
        now,
    )
}

#[test]
fn compatible_players_discover_join_and_leave() {
    let mut b = Backend::default();
    let now = Instant::now();
    let a = register(&mut b, "Host", now);
    let g = register(&mut b, "Guest", now);
    let r = create(&mut b, &a, "public", now);
    let id = &r["result"]["room"];
    let list = call(
        &mut b,
        json!({"op":"list_rooms","token":g,"game":game()}),
        now,
    );
    assert_eq!(list["result"]["rooms"].as_array().unwrap().len(), 1);
    assert!(!list.to_string().contains(&a));
    assert!(
        !list
            .to_string()
            .contains(r["result"]["invite"].as_str().unwrap())
    );
    assert_eq!(
        call(
            &mut b,
            json!({"op":"join_room","token":g,"room":id,"game":game()}),
            now
        )["ok"],
        true
    );
    let members = call(&mut b, json!({"op":"room","token":g}), now);
    assert_eq!(members["result"]["members"].as_array().unwrap().len(), 2);
    call(&mut b, json!({"op":"leave_room","token":g}), now);
    assert_eq!(
        call(&mut b, json!({"op":"room","token":g}), now)["error"],
        "not_in_room"
    );
    call(&mut b, json!({"op":"logout","token":a}), now);
    assert_eq!(
        call(
            &mut b,
            json!({"op":"list_rooms","token":g,"game":game()}),
            now
        )["result"]["rooms"],
        json!([])
    );
}

#[test]
fn incompatible_builds_and_regions_do_not_match() {
    let mut b = Backend::default();
    let now = Instant::now();
    let a = register(&mut b, "Host", now);
    let g = register(&mut b, "Guest", now);
    let r = create(&mut b, &a, "public", now);
    for field in ["update", "dlc", "title_id"] {
        let mut wrong = game();
        wrong[field] = match field {
            "update" => json!("4.0.0"),
            "dlc" => json!(["0100152000023001"]),
            _ => json!("0100152000022001"),
        };
        assert_eq!(
            call(
                &mut b,
                json!({"op":"list_rooms","token":g,"game":wrong}),
                now
            )["result"]["rooms"],
            json!([])
        );
        assert_eq!(
            call(
                &mut b,
                json!({"op":"join_room","token":g,"game":wrong,"room":r["result"]["room"]}),
                now
            )["error"],
            "incompatible_game"
        );
    }
    assert_eq!(
        call(
            &mut b,
            json!({"op":"list_rooms","token":g,"game":game(),"region":"EU"}),
            now
        )["result"]["rooms"],
        json!([])
    );
}

#[test]
fn invite_rooms_require_secret_and_capacity_is_enforced() {
    let mut b = Backend::default();
    let now = Instant::now();
    let a = register(&mut b, "Host", now);
    let g = register(&mut b, "Guest", now);
    let third = register(&mut b, "Third", now);
    let r = create(&mut b, &a, "invite", now);
    assert_eq!(
        call(
            &mut b,
            json!({"op":"list_rooms","token":g,"game":game()}),
            now
        )["result"]["rooms"],
        json!([])
    );
    let mut join = json!({"op":"join_room","token":g,"game":game(),"room":r["result"]["room"]});
    assert_eq!(call(&mut b, join.clone(), now)["error"], "room_unavailable");
    join["invite"] = r["result"]["invite"].clone();
    assert_eq!(call(&mut b, join.clone(), now)["ok"], true);
    join["token"] = json!(third);
    assert_eq!(call(&mut b, join, now)["error"], "room_full");
}

#[test]
fn expired_host_closes_room_without_migrating() {
    let mut b = Backend::default();
    let now = Instant::now();
    let a = register(&mut b, "Host", now);
    create(&mut b, &a, "public", now);
    let g = register(&mut b, "Guest", now + Duration::from_secs(60));
    assert_eq!(
        call(
            &mut b,
            json!({"op":"list_rooms","token":g,"game":game()}),
            now + Duration::from_secs(121)
        )["result"]["rooms"],
        json!([])
    );
    assert_eq!(
        call(
            &mut b,
            json!({"op":"heartbeat","token":a}),
            now + Duration::from_secs(121)
        )["error"],
        "unauthorized"
    );
}

#[test]
fn identity_and_protocol_are_explicit() {
    let mut b = Backend::default();
    let now = Instant::now();
    let hello = call(&mut b, json!({"op":"hello","protocol":1}), now);
    assert_eq!(hello["result"]["native_online_play"], false);
    assert_eq!(
        call(&mut b, json!({"op":"hello","protocol":2}), now)["error"],
        "protocol_mismatch"
    );
    assert_eq!(
        call(&mut b, json!({"op":"register","name":"../bad"}), now)["error"],
        "invalid_name"
    );
    assert_eq!(
        call(&mut b, json!({"op":"room","token":"made-up"}), now)["error"],
        "unauthorized"
    );
    assert!(
        serde_json::from_value::<uscl_switch_online::Request>(
            json!({"op":"register","name":"Host","extra":true})
        )
        .is_err()
    );
}

#[test]
fn malformed_frames_are_bounded_and_do_not_consume_next_frame() {
    use std::io::Cursor;
    let mut frames = Cursor::new(b"{}\n{}\n");
    assert_eq!(read_frame(&mut frames).unwrap().unwrap(), b"{}\n");
    assert_eq!(read_frame(&mut frames).unwrap().unwrap(), b"{}\n");
    assert!(read_frame(&mut frames).unwrap().is_none());
    assert!(read_frame(&mut Cursor::new(vec![b'x'; MAX_FRAME + 1])).is_err());
    assert!(read_frame(&mut Cursor::new(b"{}")).is_err());
}
