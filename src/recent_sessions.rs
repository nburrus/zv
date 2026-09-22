use std::collections::HashSet;
use std::fs::OpenOptions;
use std::io;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

const MAX_RECENT_SESSIONS: usize = 25;
const HISTORY_VERSION: u32 = 1;

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct RecentSession {
    pub paths: Vec<PathBuf>,
    pub last_opened_unix_seconds: u64,
}

#[derive(Debug, Deserialize, Serialize)]
struct RecentSessionsFile {
    version: u32,
    sessions: Vec<RecentSession>,
}

#[derive(Debug, Default)]
pub struct RecentSessions {
    path: Option<PathBuf>,
    sessions: Vec<RecentSession>,
}

impl RecentSessions {
    pub fn load() -> Self {
        let path = recent_sessions_path();
        let sessions = path
            .as_deref()
            .and_then(|path| {
                load_locked(path)
                    .map_err(|error| {
                        tracing::warn!(path = %path.display(), %error, "failed to load recent sessions");
                        error
                    })
                    .ok()
            })
            .unwrap_or_default();
        Self {
            path,
            sessions: normalize_sessions(sessions),
        }
    }

    pub fn sessions(&self) -> &[RecentSession] {
        &self.sessions
    }

    pub fn record(&mut self, paths: Vec<PathBuf>) {
        let paths = normalize_paths(paths);
        if paths.is_empty() {
            return;
        }
        let session = RecentSession {
            paths,
            last_opened_unix_seconds: now_unix_seconds(),
        };
        record_session(&mut self.sessions, session.clone());
        self.update_file(move |sessions| record_session(sessions, session));
    }

    pub fn clear(&mut self) {
        self.sessions.clear();
        self.update_file(Vec::clear);
    }

    fn update_file(&mut self, update: impl FnOnce(&mut Vec<RecentSession>)) {
        let Some(path) = &self.path else {
            return;
        };
        match update_locked(path, update) {
            Ok(sessions) => self.sessions = sessions,
            Err(error) => {
                tracing::warn!(path = %path.display(), %error, "failed to update recent sessions");
            }
        }
    }
}

fn record_session(sessions: &mut Vec<RecentSession>, session: RecentSession) {
    sessions.retain(|existing| existing.paths != session.paths);
    sessions.insert(0, session);
    sessions.truncate(MAX_RECENT_SESSIONS);
}

fn load_locked(path: &Path) -> io::Result<Vec<RecentSession>> {
    with_history_lock(path, || Ok(read_sessions(path)))
}

fn update_locked(path: &Path, update: impl FnOnce(&mut Vec<RecentSession>)) -> io::Result<Vec<RecentSession>> {
    with_history_lock(path, || {
        let mut sessions = read_sessions(path);
        update(&mut sessions);
        let sessions = normalize_sessions(sessions);
        write_sessions(path, &sessions)?;
        Ok(sessions)
    })
}

fn with_history_lock<T>(path: &Path, operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
    let parent = path
        .parent()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "recent sessions path has no parent"))?;
    std::fs::create_dir_all(parent)?;
    let lock_path = path.with_extension("lock");
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(lock_path)?;
    lock.lock()?;
    operation()
}

fn read_sessions(path: &Path) -> Vec<RecentSession> {
    std::fs::read(path)
        .ok()
        .and_then(|contents| serde_json::from_slice::<RecentSessionsFile>(&contents).ok())
        .filter(|file| file.version == HISTORY_VERSION)
        .map(|file| normalize_sessions(file.sessions))
        .unwrap_or_default()
}

fn write_sessions(path: &Path, sessions: &[RecentSession]) -> io::Result<()> {
    let file = RecentSessionsFile {
        version: HISTORY_VERSION,
        sessions: sessions.to_vec(),
    };
    let contents = serde_json::to_vec_pretty(&file).map_err(io::Error::other)?;
    let temporary_path = path.with_extension(format!("json.{}.tmp", std::process::id()));
    let result = std::fs::write(&temporary_path, contents).and_then(|()| replace_file(&temporary_path, path));
    if result.is_err() {
        let _ = std::fs::remove_file(&temporary_path);
    }
    result
}

fn replace_file(from: &Path, to: &Path) -> io::Result<()> {
    match std::fs::rename(from, to) {
        Ok(()) => Ok(()),
        #[cfg(target_os = "windows")]
        Err(error) if to.exists() => {
            std::fs::remove_file(to)?;
            std::fs::rename(from, to).map_err(|_| error)
        }
        Err(error) => Err(error),
    }
}

pub fn session_label(paths: &[PathBuf]) -> String {
    if paths.len() > 1
        && let Some(parent) = common_parent(paths)
        && let Some(name) = parent.file_name()
    {
        return format!("{}/ ({} images)", name.to_string_lossy(), paths.len());
    }
    let first = paths
        .first()
        .and_then(|path| path.file_name())
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_else(|| "Image session".to_owned());
    match paths.len() {
        0 | 1 => first,
        count => format!("{first} + {} more", count - 1),
    }
}

fn common_parent(paths: &[PathBuf]) -> Option<&Path> {
    let parent = paths.first()?.parent()?;
    paths.iter().all(|path| path.parent() == Some(parent)).then_some(parent)
}

fn normalize_paths(paths: Vec<PathBuf>) -> Vec<PathBuf> {
    paths
        .into_iter()
        .map(|path| std::fs::canonicalize(&path).unwrap_or(path))
        .collect()
}

fn normalize_sessions(mut sessions: Vec<RecentSession>) -> Vec<RecentSession> {
    sessions.retain(|session| !session.paths.is_empty());
    sessions.sort_by_key(|session| std::cmp::Reverse(session.last_opened_unix_seconds));
    let mut seen = HashSet::new();
    sessions.retain(|session| seen.insert(session.paths.clone()));
    sessions.truncate(MAX_RECENT_SESSIONS);
    sessions
}

fn now_unix_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn recent_sessions_path() -> Option<PathBuf> {
    #[cfg(target_os = "macos")]
    {
        std::env::var_os("HOME")
            .map(PathBuf::from)
            .map(|home| home.join("Library/Application Support/zv/recent-sessions.json"))
    }
    #[cfg(target_os = "windows")]
    {
        std::env::var_os("APPDATA")
            .map(PathBuf::from)
            .map(|config| config.join("zv/recent-sessions.json"))
    }
    #[cfg(not(any(target_os = "macos", target_os = "windows")))]
    {
        std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .or_else(|| {
                std::env::var_os("HOME")
                    .map(PathBuf::from)
                    .map(|home| home.join(".config"))
            })
            .map(|config| config.join("zv/recent-sessions.json"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temporary_history_path(name: &str) -> PathBuf {
        std::env::temp_dir()
            .join(format!(
                "zv-recent-sessions-{name}-{}-{}",
                std::process::id(),
                SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()
            ))
            .join("recent-sessions.json")
    }

    #[test]
    fn labels_same_directory_groups_by_directory() {
        let paths = vec![PathBuf::from("/tmp/photos/a.png"), PathBuf::from("/tmp/photos/b.png")];
        assert_eq!(session_label(&paths), "photos/ (2 images)");
    }

    #[test]
    fn labels_mixed_directories_from_first_image() {
        let paths = vec![PathBuf::from("/tmp/a.png"), PathBuf::from("/other/b.png")];
        assert_eq!(session_label(&paths), "a.png + 1 more");
    }

    #[test]
    fn history_is_newest_first_deduplicated_and_bounded() {
        let sessions = (0..MAX_RECENT_SESSIONS + 2)
            .map(|index| RecentSession {
                paths: vec![PathBuf::from(format!("{index}.png"))],
                last_opened_unix_seconds: index as u64,
            })
            .chain(std::iter::once(RecentSession {
                paths: vec![PathBuf::from(format!("{}.png", MAX_RECENT_SESSIONS + 1))],
                last_opened_unix_seconds: 100,
            }))
            .collect();
        let sessions = normalize_sessions(sessions);
        assert_eq!(sessions.len(), MAX_RECENT_SESSIONS);
        assert_eq!(
            sessions[0].paths,
            vec![PathBuf::from(format!("{}.png", MAX_RECENT_SESSIONS + 1))]
        );
        assert_eq!(sessions.last().unwrap().paths, vec![PathBuf::from("2.png")]);
    }

    #[test]
    fn recording_an_existing_group_moves_it_to_the_front() {
        let mut history = RecentSessions::default();
        for index in 0..MAX_RECENT_SESSIONS + 2 {
            history.record(vec![PathBuf::from(format!("{index}.png"))]);
        }
        history.record(vec![PathBuf::from("5.png")]);

        assert_eq!(history.sessions.len(), MAX_RECENT_SESSIONS);
        assert_eq!(history.sessions[0].paths, vec![PathBuf::from("5.png")]);
        assert_eq!(
            history
                .sessions
                .iter()
                .filter(|session| session.paths == vec![PathBuf::from("5.png")])
                .count(),
            1
        );
    }

    #[test]
    fn stale_instances_merge_updates_while_holding_the_file_lock() {
        let path = temporary_history_path("merge");
        let mut first = RecentSessions {
            path: Some(path.clone()),
            sessions: Vec::new(),
        };
        let mut second = RecentSessions {
            path: Some(path.clone()),
            sessions: Vec::new(),
        };

        first.record(vec![PathBuf::from("first.png")]);
        second.record(vec![PathBuf::from("second.png")]);

        let saved = read_sessions(&path);
        assert_eq!(saved.len(), 2);
        assert!(
            saved
                .iter()
                .any(|session| session.paths == vec![PathBuf::from("first.png")])
        );
        assert!(
            saved
                .iter()
                .any(|session| session.paths == vec![PathBuf::from("second.png")])
        );
        assert_eq!(second.sessions, saved);
    }

    #[test]
    fn clear_does_not_merge_stale_entries_back_in() {
        let path = temporary_history_path("clear");
        let mut first = RecentSessions {
            path: Some(path.clone()),
            sessions: Vec::new(),
        };
        let mut stale = RecentSessions {
            path: Some(path.clone()),
            sessions: Vec::new(),
        };
        first.record(vec![PathBuf::from("first.png")]);

        stale.clear();

        assert!(read_sessions(&path).is_empty());
        assert!(stale.sessions.is_empty());
    }

    #[test]
    fn simultaneous_writers_preserve_every_group() {
        let path = temporary_history_path("concurrent");
        let writers = 8;
        let barrier = std::sync::Arc::new(std::sync::Barrier::new(writers));
        let handles = (0..writers)
            .map(|index| {
                let path = path.clone();
                let barrier = barrier.clone();
                std::thread::spawn(move || {
                    let mut history = RecentSessions {
                        path: Some(path),
                        sessions: Vec::new(),
                    };
                    barrier.wait();
                    history.record(vec![PathBuf::from(format!("{index}.png"))]);
                })
            })
            .collect::<Vec<_>>();
        for handle in handles {
            handle.join().unwrap();
        }

        let saved = read_sessions(&path);
        assert_eq!(saved.len(), writers);
        for index in 0..writers {
            assert!(
                saved
                    .iter()
                    .any(|session| session.paths == vec![PathBuf::from(format!("{index}.png"))])
            );
        }
    }
}
