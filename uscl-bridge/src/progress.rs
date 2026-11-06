//! Progress that travels with the player (PLAN.md): a member's bridge hands its player's saved progress (a bundle
//! from their own uscl-dw, `dw-server --export`) to the host's bridge over the Steam session, which passes it to the
//! host's uscl-dw with the sender's Steam id; the host's bridge sends updated copies back, and the member's bridge
//! stores them at home (`dw-server --import`). Off unless USCL sets USCL_PLAYER (online id) and USCL_DW_DATA
//! (that PC's uscl-dw folder).
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::Duration;

/// A Steam message holds up to 512 KB; bundles are a few KB plus emblems (50 KB each, hex-doubled).
pub const MAX_BUNDLE: usize = 500 * 1024;

pub struct Carry {
    pub player: String,
    data_dir: PathBuf,
    dw: PathBuf,
}

impl Carry {
    pub fn from_env() -> Option<Carry> {
        let player = std::env::var("USCL_PLAYER").ok().filter(|p| !p.is_empty())?;
        let data_dir = PathBuf::from(std::env::var_os("USCL_DW_DATA")?);
        // our uscl-dw sits next to us (USCL install folder or USCL_TOOLS)
        let dw = std::env::current_exe().ok()?.with_file_name(if cfg!(windows) { "dw-server.exe" } else { "dw-server" });
        Some(Carry { player, data_dir, dw })
    }

    /// Our player's progress as kept on this PC.
    pub fn export(&self) -> Result<String, String> {
        let (stdout, _) = self.run(&["--export", &self.player], None)?;
        Ok(stdout.trim().to_string())
    }

    /// Keeps the host's copy of our player's progress on this PC.
    pub fn import(&self, bundle: &str) -> Result<String, String> {
        let (_, stderr) = self.run(&["--import"], Some(bundle))?;
        Ok(stderr.trim().to_string())
    }

    /// Runs our uscl-dw on this PC's folder: (stdout, stderr). A uscl-dw from before carried progress doesn't know
    /// these options and would start serving instead, so it gets 30 s and is then stopped.
    fn run(&self, args: &[&str], input: Option<&str>) -> Result<(String, String), String> {
        std::fs::create_dir_all(&self.data_dir).map_err(|e| e.to_string())?;
        let mut child = Command::new(&self.dw)
            .args(args)
            .current_dir(&self.data_dir)
            .stdin(if input.is_some() { Stdio::piped() } else { Stdio::null() })
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| format!("{}: {e}", self.dw.display()))?;
        if let Some(input) = input {
            child.stdin.take().expect("stdin piped").write_all(input.as_bytes()).map_err(|e| e.to_string())?;
        }
        // read both pipes off this thread: a bundle is bigger than a pipe's buffer
        let read = |mut pipe: Box<dyn Read + Send>| std::thread::spawn(move || {
            let mut text = String::new();
            let _ = pipe.read_to_string(&mut text);
            text
        });
        let stdout = read(Box::new(child.stdout.take().expect("stdout piped")));
        let stderr = read(Box::new(child.stderr.take().expect("stderr piped")));
        let deadline = std::time::Instant::now() + Duration::from_secs(30);
        let status = loop {
            match child.try_wait().map_err(|e| e.to_string())? {
                Some(status) => break status,
                None if std::time::Instant::now() > deadline => {
                    let _ = child.kill();
                    let _ = child.wait();
                    return Err(format!("{} didn't answer (too old for carried progress?)", self.dw.display()));
                }
                None => std::thread::sleep(Duration::from_millis(50)),
            }
        };
        let (stdout, stderr) = (stdout.join().unwrap_or_default(), stderr.join().unwrap_or_default());
        if status.success() { Ok((stdout, stderr)) } else { Err(format!("dw-server {} failed: {}", args[0], stderr.trim())) }
    }
}

/// The player a bundle belongs to.
pub fn owner(bundle: &str) -> Option<String> {
    serde_json::from_str::<serde_json::Value>(bundle).ok()?.get("online_id")?.as_str().map(str::to_string)
}

fn admin_addr() -> String {
    std::env::var("DW_ADMIN_ADDR").unwrap_or_else(|_| "127.0.0.1:3180".into())
}

/// One HTTP/1.1 request to the host's uscl-dw admin port: (status, body).
fn http(request: &str) -> Result<(u16, String), String> {
    let mut s = TcpStream::connect(admin_addr()).map_err(|e| format!("uscl-dw admin {}: {e}", admin_addr()))?;
    s.set_read_timeout(Some(Duration::from_secs(10))).ok();
    s.write_all(request.as_bytes()).map_err(|e| e.to_string())?;
    let mut reply = String::new();
    s.read_to_string(&mut reply).map_err(|e| e.to_string())?;
    let (head, body) = reply.split_once("\r\n\r\n").ok_or("bad reply from uscl-dw admin")?;
    let status = head.split_whitespace().nth(1).and_then(|c| c.parse().ok()).ok_or("bad status from uscl-dw admin")?;
    Ok((status, body.to_string()))
}

/// Host: a member's bundle, from the Steam account the session authenticated.
pub fn hand_in(bundle: &str, steam_id: u64) -> Result<String, String> {
    let (status, body) = http(&format!(
        "POST /progress HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Type: application/json\r\nx-uscl-steam-id: {steam_id}\r\nContent-Length: {}\r\n\r\n{bundle}",
        bundle.len()
    ))?;
    if status == 200 { Ok(body) } else { Err(format!("{status} {body}")) }
}

/// RPCN online ids: 3-16 of A-Z a-z 0-9 - _. A member's bundle names its owner, so check before it goes in a URL.
pub fn valid_name(name: &str) -> bool {
    (3..=16).contains(&name.len()) && name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'_')
}

/// Host: the current copy of a member's progress (None: they haven't logged in yet).
pub fn fetch(online_id: &str) -> Result<Option<String>, String> {
    if !valid_name(online_id) {
        return Err(format!("not an online id: {online_id:?}"));
    }
    let (status, body) = http(&format!("GET /progress/{online_id} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"))?;
    match status {
        200 => Ok(Some(body)),
        404 => Ok(None),
        _ => Err(format!("{status} {body}")),
    }
}

#[cfg(test)]
mod tests {
    use super::{owner, valid_name};

    #[test]
    fn names_are_checked() {
        assert!(valid_name("Pete_Hegseth") && valid_name("a-b"));
        assert!(!valid_name("x") && !valid_name("a b c") && !valid_name("../admin") && !valid_name("averyverylongname1"));
    }

    #[test]
    fn talks_to_the_admin_port() {
        use std::io::{Read, Write};
        // a stand-in for uscl-dw's admin port: answers two requests, hands back what it was sent
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        // SAFETY: the only test that reads or writes DW_ADMIN_ADDR
        unsafe { std::env::set_var("DW_ADMIN_ADDR", listener.local_addr().unwrap().to_string()) };
        let server = std::thread::spawn(move || {
            let mut seen = Vec::new();
            for reply in ["HTTP/1.1 200 OK\r\ncontent-length: 7\r\n\r\npending", "HTTP/1.1 404 Not Found\r\ncontent-length: 0\r\n\r\n"] {
                let (mut s, _) = listener.accept().unwrap();
                let mut buf = [0u8; 4096];
                let n = s.read(&mut buf).unwrap();
                seen.push(String::from_utf8_lossy(&buf[..n]).to_string());
                s.write_all(reply.as_bytes()).unwrap();
            }
            seen
        });
        assert_eq!(super::hand_in(r#"{"online_id":"Pete_Hegseth"}"#, 765).unwrap(), "pending");
        assert_eq!(super::fetch("Pete_Hegseth").unwrap(), None);
        assert!(super::fetch("../x").is_err());
        let seen = server.join().unwrap();
        assert!(seen[0].starts_with("POST /progress ") && seen[0].contains("x-uscl-steam-id: 765\r\n") && seen[0].ends_with(r#"{"online_id":"Pete_Hegseth"}"#));
        assert!(seen[1].starts_with("GET /progress/Pete_Hegseth "));
    }

    #[cfg(unix)]
    #[test]
    fn runs_uscl_dw() {
        let carry = |dw: &str| super::Carry { player: "Pete_Hegseth".into(), data_dir: std::env::temp_dir().join("uscl-progress-test"), dw: dw.into() };
        assert_eq!(carry("true").export().unwrap(), "");
        assert!(carry("false").export().unwrap_err().contains("--export failed"));
        assert!(carry("/nonexistent/dw-server").import("{}").is_err());
    }

    #[test]
    fn finds_the_owner() {
        assert_eq!(owner(r#"{"online_id":"Pete_Hegseth","files":[]}"#).as_deref(), Some("Pete_Hegseth"));
        assert_eq!(owner("not json"), None);
        assert_eq!(owner(r#"{"files":[]}"#), None);
    }
}
