//! The distributed Rust executable carries everything needed to build its Cocoa launcher.
#[cfg(target_os = "macos")]
#[path = "macos_app/install.rs"]
mod macos;

#[cfg(target_os = "macos")]
pub use macos::{install, install_staged};

#[cfg(not(target_os = "macos"))]
pub fn install() -> anyhow::Result<()> {
    anyhow::bail!("--install-desktop is only supported on macOS")
}

#[cfg(not(target_os = "macos"))]
pub fn install_staged(_: &std::path::Path) -> anyhow::Result<()> {
    install()
}
