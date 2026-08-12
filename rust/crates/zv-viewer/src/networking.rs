use std::collections::HashMap;
use std::fs;
use std::io::{self, BufRead, BufReader, Write};
use std::net::{Shutdown, TcpListener, TcpStream};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use anyhow::{Context, bail};

use crate::protocol::{
    CAPABILITY_READ_IMAGES, ImageOffer, KNOWN_CAPABILITIES, Message, PROTOCOL_VERSION, read_message, write_message,
};

const SESSION_READY_PREFIX: &str = "ZV_SERVER_SESSION_READY ";
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);
const IMAGE_REQUEST_TIMEOUT: Duration = Duration::from_secs(30);
const MAX_CONCURRENT_SESSIONS: usize = 16;
type RemoteLoadResult = Result<Vec<u8>, String>;
type RemoteWaiter = Sender<RemoteLoadResult>;

#[derive(Default)]
struct RemoteProviderState {
    waiters: HashMap<u64, RemoteWaiter>,
    disconnected_reason: Option<String>,
}

#[derive(Clone)]
pub struct RemoteImageRef {
    id: u64,
    provider: RemoteImageProvider,
}

impl RemoteImageRef {
    pub fn id(&self) -> u64 {
        self.id
    }

    pub fn request_encoded_bytes(&self) -> Result<Vec<u8>, String> {
        self.provider.request(self.id)
    }
}

#[cfg(test)]
pub(crate) fn remote_image_ref_for_test(id: u64) -> RemoteImageRef {
    let (outbound, _outbound_receiver) = mpsc::channel();
    RemoteImageRef {
        id,
        provider: RemoteImageProvider {
            outbound,
            state: Arc::new(Mutex::new(RemoteProviderState::default())),
        },
    }
}

pub enum ServerSessionEvent {
    Connected { capabilities: u32 },
    ImageOffered { offer: ImageOffer, remote: RemoteImageRef },
    Disconnected { reason: String },
}

#[derive(Clone)]
struct RemoteImageProvider {
    outbound: Sender<Message>,
    state: Arc<Mutex<RemoteProviderState>>,
}

impl RemoteImageProvider {
    fn request(&self, id: u64) -> Result<Vec<u8>, String> {
        self.request_with_timeout(id, IMAGE_REQUEST_TIMEOUT)
    }

    fn request_with_timeout(&self, id: u64, timeout: Duration) -> Result<Vec<u8>, String> {
        let (sender, receiver) = mpsc::channel();
        {
            let mut state = self
                .state
                .lock()
                .map_err(|_| "remote image request state is poisoned")?;
            if let Some(reason) = &state.disconnected_reason {
                return Err(reason.clone());
            }
            if state.waiters.contains_key(&id) {
                return Err(format!("remote image {id} is already being requested"));
            }
            state.waiters.insert(id, sender);
        }
        if self.outbound.send(Message::RequestImageData { id }).is_err() {
            self.remove_waiter(id);
            return Err("remote client disconnected before the image request was sent".to_owned());
        }
        match receiver.recv_timeout(timeout) {
            Ok(result) => result,
            Err(RecvTimeoutError::Timeout) => {
                self.remove_waiter(id);
                Err(format!("remote image {id} request timed out"))
            }
            Err(RecvTimeoutError::Disconnected) => Err("remote client disconnected while loading the image".to_owned()),
        }
    }

    fn complete(&self, id: u64, result: Result<Vec<u8>, String>) {
        let waiter = self.state.lock().ok().and_then(|mut state| state.waiters.remove(&id));
        if let Some(waiter) = waiter {
            let _ = waiter.send(result);
        }
    }

    fn remove_waiter(&self, id: u64) {
        if let Ok(mut state) = self.state.lock() {
            state.waiters.remove(&id);
        }
    }

    fn fail_all(&self, reason: &str) {
        let waiters = self
            .state
            .lock()
            .map(|mut state| {
                state.disconnected_reason = Some(reason.to_owned());
                state.waiters.drain().map(|(_, waiter)| waiter).collect::<Vec<_>>()
            })
            .unwrap_or_default();
        for waiter in waiters {
            let _ = waiter.send(Err(reason.to_owned()));
        }
    }
}

pub fn spawn_server_session(
    listener: TcpListener,
    on_event: impl Fn() + Send + Sync + 'static,
) -> Receiver<ServerSessionEvent> {
    let (event_sender, event_receiver) = mpsc::channel();
    let on_event = Arc::new(on_event);
    thread::spawn(move || {
        let result = listener
            .accept()
            .context("failed to accept the proxied client connection")
            .and_then(|(stream, peer)| {
                tracing::info!(%peer, "server session accepted client");
                serve_session_connection(stream, &event_sender, on_event.as_ref())
            });
        if let Err(error) = result {
            let _ = event_sender.send(ServerSessionEvent::Disconnected {
                reason: format!("{error:#}"),
            });
            on_event();
        }
    });
    event_receiver
}

fn serve_session_connection(
    mut stream: TcpStream,
    events: &Sender<ServerSessionEvent>,
    on_event: &dyn Fn(),
) -> anyhow::Result<()> {
    stream.set_nodelay(true).context("failed to configure client socket")?;
    stream
        .set_read_timeout(Some(HANDSHAKE_TIMEOUT))
        .context("failed to set client handshake timeout")?;
    let (version, capabilities) = match read_message(&mut stream).context("failed to read client handshake")? {
        Message::Hello { version, capabilities } => (version, capabilities),
        _ => bail!("client did not begin with a Hello message"),
    };
    if version != PROTOCOL_VERSION {
        bail!("client protocol version {version} is unsupported; expected {PROTOCOL_VERSION}");
    }
    if capabilities & CAPABILITY_READ_IMAGES == 0 {
        bail!("client does not advertise image-read capability");
    }
    stream
        .set_read_timeout(None)
        .context("failed to clear client handshake timeout")?;
    let unknown_capabilities = capabilities & !KNOWN_CAPABILITIES;
    if unknown_capabilities != 0 {
        tracing::debug!(unknown_capabilities, "client advertised unknown capability bits");
    }

    let (outbound, outbound_receiver) = mpsc::channel::<Message>();
    let provider = RemoteImageProvider {
        outbound: outbound.clone(),
        state: Arc::new(Mutex::new(RemoteProviderState::default())),
    };
    let mut writer = stream.try_clone().context("failed to clone client socket")?;
    thread::spawn(move || {
        while let Ok(message) = outbound_receiver.recv() {
            if let Err(error) = write_message(&mut writer, &message) {
                tracing::debug!(%error, "server session writer stopped");
                break;
            }
        }
        let _ = writer.shutdown(Shutdown::Both);
    });

    outbound
        .send(Message::Hello {
            version: PROTOCOL_VERSION,
            capabilities: CAPABILITY_READ_IMAGES,
        })
        .map_err(|_| anyhow::anyhow!("server session writer stopped during handshake"))?;
    if events.send(ServerSessionEvent::Connected { capabilities }).is_ok() {
        on_event();
    }

    let disconnect_reason = loop {
        match read_message(&mut stream) {
            Ok(Message::ImageOffer(offer)) => {
                let remote = RemoteImageRef {
                    id: offer.id,
                    provider: provider.clone(),
                };
                if events.send(ServerSessionEvent::ImageOffered { offer, remote }).is_err() {
                    break "viewer closed".to_owned();
                }
                on_event();
            }
            Ok(Message::ImageData { id, encoded_bytes }) => provider.complete(id, Ok(encoded_bytes)),
            Ok(Message::Error { id: Some(id), message }) => provider.complete(id, Err(message)),
            Ok(Message::Goodbye) => break "remote client closed the session".to_owned(),
            Ok(Message::Hello { .. }) => break "remote client sent a duplicate handshake".to_owned(),
            Ok(other) => tracing::warn!(?other, "ignoring unexpected client message"),
            Err(error) if error.kind() == io::ErrorKind::UnexpectedEof => {
                break "remote client disconnected".to_owned();
            }
            Err(error) => break format!("remote client protocol error: {error}"),
        }
    };
    provider.fail_all(&disconnect_reason);
    let _ = stream.shutdown(Shutdown::Both);
    if events
        .send(ServerSessionEvent::Disconnected {
            reason: disconnect_reason,
        })
        .is_ok()
    {
        on_event();
    }
    Ok(())
}

pub fn run_client(host: &str, port: u16, paths: Vec<PathBuf>) -> anyhow::Result<()> {
    if paths.is_empty() {
        bail!("--client requires at least one image path");
    }
    let mut stream =
        TcpStream::connect((host, port)).with_context(|| format!("failed to connect to ZV server at {host}:{port}"))?;
    stream.set_nodelay(true).context("failed to configure server socket")?;
    write_message(
        &mut stream,
        &Message::Hello {
            version: PROTOCOL_VERSION,
            capabilities: CAPABILITY_READ_IMAGES,
        },
    )?;

    let files = paths
        .into_iter()
        .enumerate()
        .map(|(index, path)| (index as u64 + 1, absolute_client_path(path)))
        .collect::<HashMap<_, _>>();
    let mut ids = files.keys().copied().collect::<Vec<_>>();
    ids.sort_unstable();
    for id in ids {
        let path = &files[&id];
        let dimensions = image::image_dimensions(path).ok();
        let name = path
            .file_name()
            .and_then(|name| name.to_str())
            .map(ToOwned::to_owned)
            .unwrap_or_else(|| path.display().to_string());
        let format_hint = path
            .extension()
            .and_then(|extension| extension.to_str())
            .map(|extension| extension.to_ascii_lowercase());
        write_message(
            &mut stream,
            &Message::ImageOffer(ImageOffer {
                id,
                name,
                remote_path: path.display().to_string(),
                width: dimensions.map(|dimensions| dimensions.0),
                height: dimensions.map(|dimensions| dimensions.1),
                format_hint,
            }),
        )?;
    }

    stream
        .set_read_timeout(Some(HANDSHAKE_TIMEOUT))
        .context("failed to set server handshake timeout")?;
    let mut server_hello_received = false;
    loop {
        match read_message(&mut stream) {
            Ok(Message::Hello { version, .. }) => {
                if server_hello_received {
                    bail!("server sent a duplicate Hello message");
                }
                if version != PROTOCOL_VERSION {
                    bail!("server protocol version {version} is unsupported; expected {PROTOCOL_VERSION}");
                }
                server_hello_received = true;
                stream
                    .set_read_timeout(None)
                    .context("failed to clear server handshake timeout")?;
            }
            Ok(Message::RequestImageData { id }) => {
                if !server_hello_received {
                    bail!("server requested image data before completing the handshake");
                }
                let response = match files.get(&id) {
                    Some(path) => match fs::read(path) {
                        Ok(encoded_bytes) => Message::ImageData { id, encoded_bytes },
                        Err(error) => Message::Error {
                            id: Some(id),
                            message: format!("failed to read '{}': {error}", path.display()),
                        },
                    },
                    None => Message::Error {
                        id: Some(id),
                        message: format!("unknown remote image id {id}"),
                    },
                };
                write_message(&mut stream, &response)?;
            }
            Ok(Message::Goodbye) => return Ok(()),
            Ok(Message::Error { message, .. }) => bail!("server rejected client: {message}"),
            Ok(other) => tracing::warn!(?other, "ignoring unexpected server message"),
            Err(error) if error.kind() == io::ErrorKind::UnexpectedEof && !server_hello_received => {
                bail!("server disconnected before completing the handshake");
            }
            Err(error) if error.kind() == io::ErrorKind::UnexpectedEof => return Ok(()),
            Err(error) => return Err(error).context("failed to read from ZV server"),
        }
    }
}

fn absolute_client_path(path: PathBuf) -> PathBuf {
    std::path::absolute(&path).unwrap_or(path)
}

pub fn run_supervisor(host: &str, port: u16) -> anyhow::Result<()> {
    let listener =
        TcpListener::bind((host, port)).with_context(|| format!("failed to bind ZV server to {host}:{port}"))?;
    tracing::info!(address = %listener.local_addr()?, "ZV Rust server listening");
    let active_sessions = Arc::new(AtomicUsize::new(0));
    for accepted in listener.incoming() {
        match accepted {
            Ok(client) => {
                let Some(session_slot) = try_acquire_session_slot(&active_sessions) else {
                    tracing::warn!(
                        limit = MAX_CONCURRENT_SESSIONS,
                        "rejecting client: session limit reached"
                    );
                    let _ = client.shutdown(Shutdown::Both);
                    continue;
                };
                if let Err(error) = thread::Builder::new()
                    .name("zv-client-session".to_owned())
                    .spawn(move || {
                        let _session_slot = session_slot;
                        if let Err(error) = supervise_client(client) {
                            tracing::error!(%error, "client session failed");
                        }
                    })
                {
                    tracing::error!(%error, "failed to start client session thread");
                }
            }
            Err(error) => tracing::error!(%error, "failed to accept client connection"),
        }
    }
    Ok(())
}

struct ActiveSessionSlot {
    active_sessions: Arc<AtomicUsize>,
}

impl Drop for ActiveSessionSlot {
    fn drop(&mut self) {
        self.active_sessions.fetch_sub(1, Ordering::Relaxed);
    }
}

fn try_acquire_session_slot(active_sessions: &Arc<AtomicUsize>) -> Option<ActiveSessionSlot> {
    active_sessions
        .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |active| {
            (active < MAX_CONCURRENT_SESSIONS).then_some(active + 1)
        })
        .ok()?;
    Some(ActiveSessionSlot {
        active_sessions: active_sessions.clone(),
    })
}

fn supervise_client(client: TcpStream) -> anyhow::Result<()> {
    let executable = std::env::current_exe().context("failed to locate current ZV executable")?;
    let mut child = Command::new(executable)
        .args(["--server-session", "--host", "127.0.0.1", "--port", "0"])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .spawn()
        .context("failed to start viewer session process")?;
    let proxy_result = (|| {
        let stdout = child.stdout.take().context("viewer session stdout was unavailable")?;
        let mut ready_line = String::new();
        BufReader::new(stdout)
            .read_line(&mut ready_line)
            .context("failed to read viewer session ready line")?;
        let port = ready_line
            .trim()
            .strip_prefix(SESSION_READY_PREFIX)
            .context("viewer session returned an invalid ready line")?
            .parse::<u16>()
            .context("viewer session returned an invalid port")?;
        let session = connect_with_retry(port).context("failed to connect to viewer session")?;
        proxy_bidirectional(client, session)
    })();
    let _ = child.kill();
    let _ = child.wait();
    proxy_result
}

fn connect_with_retry(port: u16) -> io::Result<TcpStream> {
    let mut last_error = None;
    for _ in 0..20 {
        match TcpStream::connect(("127.0.0.1", port)) {
            Ok(stream) => return Ok(stream),
            Err(error) => last_error = Some(error),
        }
        thread::sleep(Duration::from_millis(25));
    }
    Err(last_error.unwrap_or_else(|| io::Error::other("no connection attempt was made")))
}

fn proxy_bidirectional(client: TcpStream, session: TcpStream) -> anyhow::Result<()> {
    client.set_nodelay(true)?;
    session.set_nodelay(true)?;
    let mut client_reader = client.try_clone()?;
    let mut session_writer = session.try_clone()?;
    let first = thread::spawn(move || {
        let result = io::copy(&mut client_reader, &mut session_writer);
        let _ = session_writer.shutdown(Shutdown::Both);
        result
    });
    let mut session_reader = session;
    let mut client_writer = client;
    let second = thread::spawn(move || {
        let result = io::copy(&mut session_reader, &mut client_writer);
        let _ = client_writer.shutdown(Shutdown::Both);
        result
    });
    first
        .join()
        .map_err(|_| anyhow::anyhow!("client-to-session proxy thread panicked"))??;
    second
        .join()
        .map_err(|_| anyhow::anyhow!("session-to-client proxy thread panicked"))??;
    Ok(())
}

pub fn bind_server_session(host: &str, port: u16) -> anyhow::Result<TcpListener> {
    TcpListener::bind((host, port)).with_context(|| format!("failed to bind viewer session to {host}:{port}"))
}

pub fn announce_server_session(listener: &TcpListener) -> anyhow::Result<()> {
    let port = listener.local_addr()?.port();
    println!("{SESSION_READY_PREFIX}{port}");
    io::stdout()
        .flush()
        .context("failed to flush viewer session ready line")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn client_normalizes_relative_paths_for_remote_metadata() {
        let path = absolute_client_path(PathBuf::from("../tests/books_4k.jpg"));

        assert!(path.is_absolute());
        assert!(path.ends_with("tests/books_4k.jpg"));
    }

    #[test]
    fn client_offers_and_serves_original_file_bytes() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let path = std::env::temp_dir().join(format!("zv-network-client-{}.png", std::process::id()));
        let expected = b"encoded-image-data".to_vec();
        fs::write(&path, &expected).unwrap();

        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            assert!(matches!(read_message(&mut stream).unwrap(), Message::Hello { .. }));
            let offer = match read_message(&mut stream).unwrap() {
                Message::ImageOffer(offer) => offer,
                other => panic!("expected offer, got {other:?}"),
            };
            write_message(
                &mut stream,
                &Message::Hello {
                    version: PROTOCOL_VERSION,
                    capabilities: CAPABILITY_READ_IMAGES,
                },
            )
            .unwrap();
            write_message(&mut stream, &Message::RequestImageData { id: offer.id }).unwrap();
            match read_message(&mut stream).unwrap() {
                Message::ImageData { id, encoded_bytes } => {
                    assert_eq!(id, offer.id);
                    assert_eq!(encoded_bytes, expected);
                }
                other => panic!("expected image data, got {other:?}"),
            }
            write_message(&mut stream, &Message::Goodbye).unwrap();
        });

        run_client("127.0.0.1", port, vec![path.clone()]).unwrap();
        server.join().unwrap();
        let _ = fs::remove_file(path);
    }

    #[test]
    fn client_rejects_mismatched_server_hello_version() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let path = std::env::temp_dir().join(format!("zv-network-version-{}.png", std::process::id()));
        fs::write(&path, b"encoded-image-data").unwrap();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            assert!(matches!(read_message(&mut stream).unwrap(), Message::Hello { .. }));
            assert!(matches!(read_message(&mut stream).unwrap(), Message::ImageOffer(_)));
            write_message(
                &mut stream,
                &Message::Hello {
                    version: PROTOCOL_VERSION + 1,
                    capabilities: CAPABILITY_READ_IMAGES,
                },
            )
            .unwrap();
        });

        let error = run_client("127.0.0.1", port, vec![path.clone()]).unwrap_err();

        assert!(format!("{error:#}").contains("unsupported"));
        server.join().unwrap();
        let _ = fs::remove_file(path);
    }

    #[test]
    fn client_reports_disconnect_before_server_hello() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let path = std::env::temp_dir().join(format!("zv-network-no-hello-{}.png", std::process::id()));
        fs::write(&path, b"encoded-image-data").unwrap();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            assert!(matches!(read_message(&mut stream).unwrap(), Message::Hello { .. }));
            assert!(matches!(read_message(&mut stream).unwrap(), Message::ImageOffer(_)));
        });

        let error = run_client("127.0.0.1", port, vec![path.clone()]).unwrap_err();

        assert!(format!("{error:#}").contains("before completing the handshake"));
        server.join().unwrap();
        let _ = fs::remove_file(path);
    }

    #[test]
    fn server_session_requests_remote_bytes_lazily() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let (wake_sender, wake_receiver) = mpsc::channel();
        let events = spawn_server_session(listener, move || {
            let _ = wake_sender.send(());
        });
        let client = thread::spawn(move || {
            let mut stream = TcpStream::connect(("127.0.0.1", port)).unwrap();
            stream.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
            write_message(
                &mut stream,
                &Message::Hello {
                    version: PROTOCOL_VERSION,
                    capabilities: CAPABILITY_READ_IMAGES,
                },
            )
            .unwrap();
            write_message(
                &mut stream,
                &Message::ImageOffer(ImageOffer {
                    id: 7,
                    name: "remote.png".to_owned(),
                    remote_path: "/client/remote.png".to_owned(),
                    width: Some(2),
                    height: Some(3),
                    format_hint: Some("png".to_owned()),
                }),
            )
            .unwrap();
            assert!(matches!(read_message(&mut stream).unwrap(), Message::Hello { .. }));
            assert_eq!(read_message(&mut stream).unwrap(), Message::RequestImageData { id: 7 });
            write_message(
                &mut stream,
                &Message::ImageData {
                    id: 7,
                    encoded_bytes: vec![9, 8, 7],
                },
            )
            .unwrap();
            write_message(&mut stream, &Message::Goodbye).unwrap();
        });

        assert!(matches!(
            events.recv_timeout(Duration::from_secs(2)).unwrap(),
            ServerSessionEvent::Connected { .. }
        ));
        wake_receiver.recv_timeout(Duration::from_secs(2)).unwrap();
        let remote = match events.recv_timeout(Duration::from_secs(2)).unwrap() {
            ServerSessionEvent::ImageOffered { offer, remote } => {
                assert_eq!(offer.name, "remote.png");
                remote
            }
            _ => panic!("expected a remote image offer"),
        };
        wake_receiver.recv_timeout(Duration::from_secs(2)).unwrap();
        assert_eq!(remote.request_encoded_bytes().unwrap(), [9, 8, 7]);
        client.join().unwrap();
        assert!(matches!(
            events.recv_timeout(Duration::from_secs(2)).unwrap(),
            ServerSessionEvent::Disconnected { .. }
        ));
        wake_receiver.recv_timeout(Duration::from_secs(2)).unwrap();
        assert!(remote.request_encoded_bytes().is_err());
    }

    #[test]
    fn remote_image_request_times_out_and_removes_waiter() {
        let (outbound, requests) = mpsc::channel();
        let provider = RemoteImageProvider {
            outbound,
            state: Arc::new(Mutex::new(RemoteProviderState::default())),
        };

        let error = provider.request_with_timeout(17, Duration::from_millis(1)).unwrap_err();

        assert!(error.contains("timed out"), "{error}");
        assert_eq!(requests.recv().unwrap(), Message::RequestImageData { id: 17 });
        assert!(provider.state.lock().unwrap().waiters.is_empty());
    }

    #[test]
    fn supervisor_session_limit_is_released_when_slots_drop() {
        let active = Arc::new(AtomicUsize::new(0));
        let slots = (0..MAX_CONCURRENT_SESSIONS)
            .map(|_| try_acquire_session_slot(&active).expect("slot should be available"))
            .collect::<Vec<_>>();

        assert!(try_acquire_session_slot(&active).is_none());
        drop(slots);
        assert_eq!(active.load(Ordering::Relaxed), 0);
        assert!(try_acquire_session_slot(&active).is_some());
    }
}
