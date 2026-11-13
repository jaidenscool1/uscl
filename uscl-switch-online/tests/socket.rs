use serde_json::{Value, json};
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::process::{Command, Stdio};
use std::time::Duration;

#[test]
fn real_server_round_trip() {
    struct Child(std::process::Child);
    impl Drop for Child {
        fn drop(&mut self) {
            let _ = self.0.kill();
            let _ = self.0.wait();
        }
    }
    let mut child = Child(
        Command::new(env!("CARGO_BIN_EXE_uscl-switch-online"))
            .args(["--port", "0"])
            .stdout(Stdio::piped())
            .spawn()
            .unwrap(),
    );
    let mut ready = String::new();
    BufReader::new(child.0.stdout.take().unwrap())
        .read_line(&mut ready)
        .unwrap();
    let port: u16 = ready
        .trim()
        .strip_prefix("USCL_SWITCH_ONLINE_READY ")
        .unwrap()
        .parse()
        .unwrap();
    let rpc = |request: Value| -> Value {
        let mut socket = TcpStream::connect((std::net::Ipv4Addr::LOCALHOST, port)).unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        writeln!(socket, "{request}").unwrap();
        let mut reply = String::new();
        BufReader::new(socket).read_line(&mut reply).unwrap();
        serde_json::from_str(&reply).unwrap()
    };
    assert_eq!(rpc(json!({"op":"hello","protocol":1}))["ok"], true);
    let host = rpc(json!({"op":"register","name":"Host"}))["result"]["token"].clone();
    let guest = rpc(json!({"op":"register","name":"Guest"}))["result"]["token"].clone();
    let game = json!({"title_id":"0100152000022000","update":"4.0.1","dlc":[],"adapter":1});
    let room = rpc(
        json!({"op":"create_room","token":host,"game":game,"region":"NA","capacity":2,"visibility":"public"}),
    );
    let rooms = rpc(json!({"op":"list_rooms","token":guest,"game":game}));
    assert_eq!(rooms["result"]["rooms"].as_array().unwrap().len(), 1);
    assert_eq!(
        rpc(json!({"op":"join_room","token":guest,"game":game,"room":room["result"]["room"]}))["ok"],
        true
    );
    assert_eq!(
        rpc(json!({"op":"room","token":guest}))["result"]["members"]
            .as_array()
            .unwrap()
            .len(),
        2
    );
    assert_eq!(rpc(json!({"op":"unknown"}))["error"], "invalid_request");
    assert_eq!(rpc(json!({"op":"hello","protocol":1}))["ok"], true);
}
