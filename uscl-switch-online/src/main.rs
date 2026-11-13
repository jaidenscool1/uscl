use std::io::{BufReader, Write};
use std::net::{Ipv4Addr, TcpListener, TcpStream};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use uscl_switch_online::{Backend, Request, read_frame};

fn serve(mut socket: TcpStream, backend: &Mutex<Backend>) -> std::io::Result<()> {
    socket.set_read_timeout(Some(Duration::from_secs(5)))?;
    socket.set_write_timeout(Some(Duration::from_secs(5)))?;
    let mut reader = BufReader::new(socket.try_clone()?);
    // One connection handles one request. Idle clients cannot hold a session worker indefinitely.
    if let Some(frame) = read_frame(&mut reader)? {
        let response = match serde_json::from_slice::<Request>(&frame) {
            Ok(request) => backend.lock().unwrap().handle(request, Instant::now()),
            Err(_) => serde_json::json!({"ok":false,"error":"invalid_request"}),
        };
        serde_json::to_writer(&mut socket, &response)?;
        socket.write_all(b"\n")?;
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let port: u16 = match args.as_slice() {
        [] => 24873,
        [flag, port] if flag == "--port" => port.parse()?,
        _ => return Err("usage: uscl-switch-online [--port PORT] (loopback only)".into()),
    };
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, port))?;
    println!("USCL_SWITCH_ONLINE_READY {}", listener.local_addr()?.port());
    std::io::stdout().flush()?;
    let backend = Arc::new(Mutex::new(Backend::default()));
    let active = Arc::new(AtomicUsize::new(0));
    for connection in listener.incoming() {
        let socket = connection?;
        if active.fetch_add(1, Ordering::AcqRel) >= 32 {
            active.fetch_sub(1, Ordering::AcqRel);
            continue;
        }
        let backend = backend.clone();
        let active = active.clone();
        std::thread::spawn(move || {
            let _ = serve(socket, &backend);
            active.fetch_sub(1, Ordering::AcqRel);
        });
    }
    Ok(())
}
