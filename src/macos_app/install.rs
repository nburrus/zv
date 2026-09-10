use std::fs;
use std::io::{self, IsTerminal, Write};
use std::os::unix::fs::{DirBuilderExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use anyhow::{Context, bail, ensure};

const DESTINATION: &str = "/Applications/Zv.app";
const LAUNCHER: &str = include_str!("launcher.m");
const INFO: &str = include_str!("Info.plist");
const ICON: &[u8] = include_bytes!("Zv.icns");

pub fn install() -> anyhow::Result<()> {
    let executable = std::env::current_exe().context("locate the installed zv binary")?;
    let executable_text = executable
        .to_str()
        .context("the zv executable path must be valid UTF-8")?;
    let plist = INFO
        .replace("@VERSION@", env!("CARGO_PKG_VERSION"))
        .replace("@EXECUTABLE@", &xml_escape(executable_text));
    let destination = Path::new(DESTINATION);
    check_destination(destination)?;
    // Unchanged resources and a valid signature mean we can retain the exact
    // launcher identity, even after replacing the external viewer executable.
    if matches_installation(destination, &plist) {
        println!("{DESTINATION} is already up to date (launches {executable_text}).");
        register(destination);
        return Ok(());
    }
    ensure_tools()?;
    let work = TemporaryDirectory::new(&std::env::temp_dir(), "zv-app")?;
    let bundle = work.0.join("Zv.app");
    println!("Building the launcher with the macOS SDK…");
    build_bundle(&bundle, &plist)?;

    match install_bundle(&bundle, destination) {
        Ok(()) => {}
        Err(error) if permission_denied(&error) => {
            let command = format!(
                "sudo {} --install-app-staged {}",
                shell_quote(executable_text),
                shell_quote(&bundle.to_string_lossy())
            );
            if !confirm(&format!(
                "Administrator permission is needed to install {DESTINATION}.\nRun: {command}"
            ))? {
                bail!("installation cancelled; rerun zv --install-app from a terminal to approve installation");
            }
            // Only copying and replacing the completed bundle runs as root.
            run(Command::new("/usr/bin/sudo")
                .arg(&executable)
                .arg("--install-app-staged")
                .arg(&bundle))?;
        }
        Err(error) => return Err(error),
    }
    register(destination);
    println!(
        "Installed {DESTINATION}\nLauncher target: {executable_text}\n\nChoose Zv in Finder’s Open With menu. To grant Full Disk Access, add\n{DESTINATION} in System Settings > Privacy & Security > Full Disk Access.\nRerun zv --install-app if you move the zv binary."
    );
    Ok(())
}

fn build_bundle(bundle: &Path, plist: &str) -> anyhow::Result<()> {
    fs::create_dir_all(bundle.join("Contents/MacOS"))?;
    fs::create_dir_all(bundle.join("Contents/Resources"))?;
    fs::write(bundle.join("Contents/Info.plist"), plist)?;
    fs::write(bundle.join("Contents/Resources/launcher.m"), LAUNCHER)?;
    fs::write(bundle.join("Contents/Resources/Zv.icns"), ICON)?;

    run(Command::new("/usr/bin/xcrun")
        .args([
            "--sdk",
            "macosx",
            "clang",
            "-fobjc-arc",
            "-Os",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-mmacosx-version-min=11.0",
            "-framework",
            "AppKit",
        ])
        .arg(bundle.join("Contents/Resources/launcher.m"))
        .arg("-o")
        .arg(bundle.join("Contents/MacOS/Zv")))?;
    run(Command::new("/usr/bin/plutil")
        .arg("-lint")
        .arg(bundle.join("Contents/Info.plist")))?;
    run(Command::new("/usr/bin/codesign")
        .args(["--force", "--sign", "-", "--identifier", "com.nburrus.zv"])
        .arg(bundle))?;
    verify_bundle(bundle)?;

    for file in [
        "Contents/Info.plist",
        "Contents/Resources/launcher.m",
        "Contents/Resources/Zv.icns",
    ] {
        fs::set_permissions(bundle.join(file), fs::Permissions::from_mode(0o644))?;
    }
    fs::set_permissions(bundle.join("Contents/MacOS/Zv"), fs::Permissions::from_mode(0o755))?;
    for dir in ["", "Contents", "Contents/MacOS", "Contents/Resources"] {
        fs::set_permissions(bundle.join(dir), fs::Permissions::from_mode(0o755))?;
    }
    Ok(())
}

pub fn install_staged(bundle: &Path) -> anyhow::Result<()> {
    install_bundle(bundle, Path::new(DESTINATION))
}

fn ensure_tools() -> anyhow::Result<()> {
    let available = [vec!["--find", "clang"], vec!["--sdk", "macosx", "--show-sdk-path"]]
        .iter()
        .all(|args| {
            Command::new("/usr/bin/xcrun")
                .args(args)
                .output()
                .is_ok_and(|out| out.status.success())
        });
    if available {
        return Ok(());
    }
    let proposed = "xcode-select --install";
    if confirm(&format!(
        "The macOS Command Line Tools or SDK are missing or unavailable.\nFull Xcode is not required. To open Apple’s installer, run:\n  {proposed}"
    ))? {
        run(Command::new("/usr/bin/xcode-select").arg("--install"))?;
        bail!("finish Apple’s Command Line Tools installation, then rerun zv --install-app");
    }
    bail!(
        "Command Line Tools are required. Run `{proposed}`, finish installation, then rerun zv --install-app. If already installed, check `xcode-select -p` and `xcrun --sdk macosx --show-sdk-path`"
    )
}

fn confirm(message: &str) -> anyhow::Result<bool> {
    println!("{message}");
    if !io::stdin().is_terminal() {
        return Ok(false);
    }
    print!("Run this command? [y/N] ");
    io::stdout().flush()?;
    let mut answer = String::new();
    io::stdin().read_line(&mut answer)?;
    Ok(matches!(answer.trim().to_ascii_lowercase().as_str(), "y" | "yes"))
}

fn run(command: &mut Command) -> anyhow::Result<()> {
    let status = command
        .status()
        .with_context(|| format!("could not execute {command:?}"))?;
    ensure!(status.success(), "command failed ({status}): {command:?}");
    Ok(())
}

fn plist_value(bundle: &Path, key: &str) -> Option<String> {
    let output = Command::new("/usr/bin/plutil")
        .args(["-extract", key, "raw", "-o", "-"])
        .arg(bundle.join("Contents/Info.plist"))
        .output()
        .ok()?;
    output.status.success().then(|| {
        let value = String::from_utf8_lossy(&output.stdout);
        value.strip_suffix('\n').unwrap_or(&value).to_owned()
    })
}

fn check_destination(destination: &Path) -> anyhow::Result<()> {
    match fs::symlink_metadata(destination) {
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(e) => return Err(e.into()),
        Ok(meta) => ensure!(
            meta.is_dir() && !meta.file_type().is_symlink(),
            "refusing to replace {}: not a regular app directory",
            destination.display()
        ),
    }
    ensure!(
        plist_value(destination, "CFBundleIdentifier").as_deref() == Some("com.nburrus.zv")
            && plist_value(destination, "ZvLauncherFormat").as_deref() == Some("1"),
        "refusing to replace unrelated app at {}",
        destination.display()
    );
    Ok(())
}

fn verify_bundle(bundle: &Path) -> anyhow::Result<()> {
    ensure!(bundle.is_dir(), "missing staged app: {}", bundle.display());
    check_destination(bundle)?;
    let executable = plist_value(bundle, "ZvExecutable").context("missing ZvExecutable in staged app")?;
    ensure!(
        Path::new(&executable).is_absolute(),
        "launcher target must be an absolute path"
    );
    ensure!(
        plist_value(bundle, "CFBundleExecutable").as_deref() == Some("Zv"),
        "unexpected bundle executable"
    );
    run(Command::new("/usr/bin/codesign")
        .args(["--verify", "--strict"])
        .arg(bundle))
}

fn matches_installation(bundle: &Path, plist: &str) -> bool {
    fs::read(bundle.join("Contents/Info.plist")).is_ok_and(|v| v == plist.as_bytes())
        && fs::read(bundle.join("Contents/Resources/launcher.m")).is_ok_and(|v| v == LAUNCHER.as_bytes())
        && fs::read(bundle.join("Contents/Resources/Zv.icns")).is_ok_and(|v| v == ICON)
        && Command::new("/usr/bin/codesign")
            .args(["--verify", "--strict"])
            .arg(bundle)
            .output()
            .is_ok_and(|v| v.status.success())
}

fn install_bundle(source: &Path, destination: &Path) -> anyhow::Result<()> {
    verify_bundle(source)?;
    let parent = destination.parent().context("app must have a parent directory")?;
    // Serialize installers and keep the old app intact throughout compilation/copy.
    let lock = parent.join(".Zv.install-lock");
    fs::create_dir(&lock).with_context(|| {
        format!(
            "cannot acquire {} (if an installer crashed, remove this empty directory before retrying)",
            lock.display()
        )
    })?;
    let _lock = TemporaryDirectory(lock);
    check_destination(destination)?;
    let stage = TemporaryDirectory::new(parent, ".Zv.install")?;
    // mkdtemp-style directories are private. The installed app must be readable
    // by every user, even when the copying helper runs under sudo.
    let incoming = stage.0.join("Zv.app");
    run(Command::new("/usr/bin/ditto").arg(source).arg(&incoming))?;
    fs::set_permissions(&incoming, fs::Permissions::from_mode(0o755))?;
    verify_bundle(&incoming)?;
    let backup = stage.0.join("previous.app");
    replace_bundle(&incoming, destination, &backup)?;
    if backup.exists()
        && let Err(error) = fs::remove_dir_all(&backup)
    {
        eprintln!(
            "Installed successfully, but could not remove {}: {error}",
            backup.display()
        );
    }
    Ok(())
}

fn replace_bundle(incoming: &Path, destination: &Path, backup: &Path) -> anyhow::Result<()> {
    let existed = destination.try_exists()?;
    if existed {
        fs::rename(destination, backup).context("could not preserve the existing Zv.app")?;
    }
    if let Err(error) = fs::rename(incoming, destination) {
        if existed {
            fs::rename(backup, destination).with_context(|| {
                format!(
                    "install failed ({error}); rollback also failed; recover the previous app from {}",
                    backup.display()
                )
            })?;
        }
        return Err(error).context("could not install Zv.app; previous installation restored");
    }
    Ok(())
}

fn permission_denied(error: &anyhow::Error) -> bool {
    error.chain().any(|cause| {
        cause
            .downcast_ref::<io::Error>()
            .is_some_and(|e| e.kind() == io::ErrorKind::PermissionDenied)
    })
}

fn register(bundle: &Path) {
    // Launch Services has no public registration CLI; this is the system tool
    // used by Finder. Registration is best effort and never changes defaults.
    let result = Command::new(
        "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister",
    )
    .arg("-f")
    .arg(bundle)
    .output();
    if !result.is_ok_and(|output| output.status.success()) {
        eprintln!("Could not refresh Launch Services; open {DESTINATION} once in Finder to register it.");
    }
}

fn xml_escape(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

fn shell_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}

struct TemporaryDirectory(PathBuf);
impl TemporaryDirectory {
    fn new(parent: &Path, prefix: &str) -> io::Result<Self> {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let path = parent.join(format!("{prefix}-{}-{stamp}", std::process::id()));
        fs::DirBuilder::new().mode(0o700).create(&path)?;
        Ok(Self(path))
    }
}
impl Drop for TemporaryDirectory {
    fn drop(&mut self) {
        // Never discard a backup if rollback itself failed.
        if self.0.join("previous.app").exists() {
            eprintln!("Preserving recovery files at {}", self.0.display());
            return;
        }
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_bundle_install_update_and_tamper_detection() {
        let work = TemporaryDirectory::new(&std::env::temp_dir(), "zv-bundle-test").unwrap();
        let source = work.0.join("source.app");
        let destination = work.0.join("Zv.app");
        let plist = INFO
            .replace("@VERSION@", "0.2.0")
            .replace("@EXECUTABLE@", "/tmp/zv &amp; viewer");
        build_bundle(&source, &plist).unwrap();
        install_bundle(&source, &destination).unwrap();
        assert!(matches_installation(&destination, &plist));
        // A modified signed resource must not be mistaken for an unchanged app.
        fs::write(destination.join("Contents/Resources/launcher.m"), "tampered").unwrap();
        assert!(!matches_installation(&destination, &plist));
        install_bundle(&source, &destination).unwrap();
        assert!(matches_installation(&destination, &plist));
        assert!(!work.0.join(".Zv.install-lock").exists());
        assert_eq!(fs::read_dir(&work.0).unwrap().count(), 2);
    }

    #[test]
    fn plist_preserves_unusual_executable_paths() {
        let work = TemporaryDirectory::new(&std::env::temp_dir(), "zv-plist-test").unwrap();
        fs::create_dir(work.0.join("Contents")).unwrap();
        let path = "/Users/a & b/日本語/'<>\"/zv";
        fs::write(
            work.0.join("Contents/Info.plist"),
            INFO.replace("@EXECUTABLE@", &xml_escape(path)),
        )
        .unwrap();
        assert_eq!(plist_value(&work.0, "ZvExecutable").as_deref(), Some(path));
    }

    #[test]
    fn failed_replacement_restores_previous_app() {
        let work = TemporaryDirectory::new(&std::env::temp_dir(), "zv-rollback-test").unwrap();
        let destination = work.0.join("Zv.app");
        fs::create_dir(&destination).unwrap();
        fs::write(destination.join("original"), "keep me").unwrap();
        assert!(replace_bundle(&work.0.join("missing"), &destination, &work.0.join("backup")).is_err());
        assert_eq!(fs::read_to_string(destination.join("original")).unwrap(), "keep me");
        assert!(!work.0.join("backup").exists());
    }

    #[test]
    fn unrelated_app_and_symlink_are_not_replaced() {
        let work = TemporaryDirectory::new(&std::env::temp_dir(), "zv-destination-test").unwrap();
        let app = work.0.join("other.app");
        fs::create_dir(&app).unwrap();
        assert!(check_destination(&app).is_err());
        let link = work.0.join("Zv.app");
        std::os::unix::fs::symlink(&app, &link).unwrap();
        assert!(check_destination(&link).is_err());
        fs::remove_dir(&app).unwrap();
        assert!(check_destination(&link).is_err(), "also reject dangling symlinks");
    }
}
