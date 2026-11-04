//! Module mounting, delegated to the metamodule the user installed.
//!
//! FinaSU ships no mount engine of its own: the built-in NoMount VFS driver, the
//! rule planner that turned the module directory into a mount plan, the
//! `ksud nomount` control commands and the `nomount` / `off` mode switch are all
//! gone. Module injection is now entirely the job of the metamodule the user
//! installs (NoMount, meta-overlayfs, ...), whose own `metamount.sh` loads a
//! driver and publishes the tree.
//!
//! So the whole contract at boot is: if a metamodule is installed and enabled,
//! run its mount script once; otherwise leave the filesystem alone and say so.
//! There is nothing to fall back to and nothing to configure, which is the point
//! - two engines writing the same paths was the bug this removes.

use std::fs;

use anyhow::{Context, Result};
use log::{info, warn};
use rustix::fs::{FlockOperation, flock};

use crate::defs;
use crate::metamodule;

/// Run the installed metamodule's mount script, at most once per boot.
///
/// Used by both cold boot (`on_post_fs_data`) and late-load.
///
/// # Errors
/// Returns an error when the metamodule's script fails, or when the run cannot be
/// serialised against a concurrent one.
pub fn mount_modules() -> Result<()> {
    if crate::utils::has_magisk() {
        warn!("module mount: Magisk detected, skip module mount");
        return Ok(());
    }
    if crate::utils::is_safe_mode() {
        warn!("module mount: safe mode, skip module mount");
        return Ok(());
    }
    if let Err(e) = crate::ksucalls::ensure_uapi_version_matched() {
        warn!("module mount: {e:#}, skip module mount");
        return Ok(());
    }

    let Some(path) = metamodule::get_metamodule_path() else {
        info!("module mount: no metamodule installed; modules stay inert");
        return Ok(());
    };
    if path.join(defs::DISABLE_FILE_NAME).exists() || path.join(defs::REMOVE_FILE_NAME).exists() {
        info!("module mount: the installed metamodule is disabled; modules stay inert");
        return Ok(());
    }
    let owner = path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("metamodule")
        .to_string();

    let epoch = crate::startup_epoch::current_or_create().context("read the boot id")?;
    let _lock = MountLock::acquire()?;
    if ran_this_boot(&epoch.boot_id) {
        info!("module mount: {owner} already mounted this boot");
        return Ok(());
    }

    // Recorded before the script runs: a script that wedges the boot must not be
    // re-entered on the same boot by the stage that follows it.
    mark_ran(&epoch.boot_id)?;
    info!("module mount: handing injection to {owner}");
    metamodule::exec_mount_script(defs::MODULE_DIR)
        .with_context(|| format!("metamodule {owner} mount script failed"))?;
    info!("module mount: {owner} finished its mount script");
    Ok(())
}

/// Read the boot id the marker was written for, if any.
fn ran_this_boot(boot_id: &str) -> bool {
    match fs::read_to_string(defs::MODULE_MOUNT_MARKER) {
        Ok(stored) => stored.trim() == boot_id,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => false,
        Err(e) => {
            warn!("module mount: cannot read the boot marker: {e}");
            false
        }
    }
}

fn mark_ran(boot_id: &str) -> Result<()> {
    fs::create_dir_all(defs::WORKING_DIR)
        .with_context(|| format!("failed to create {}", defs::WORKING_DIR))?;
    fs::write(defs::MODULE_MOUNT_MARKER, format!("{boot_id}\n"))
        .with_context(|| format!("failed to write {}", defs::MODULE_MOUNT_MARKER))
}

/// Hold an exclusive lock for the duration of one mount attempt, so a cold boot
/// and a late-load never run the script on top of each other.
struct MountLock {
    /// Held for its descriptor only; the lock is released when this drops.
    _file: fs::File,
}

impl MountLock {
    fn acquire() -> Result<Self> {
        fs::create_dir_all(defs::WORKING_DIR)
            .with_context(|| format!("failed to create {}", defs::WORKING_DIR))?;
        let file = fs::OpenOptions::new()
            .create(true)
            .truncate(false)
            .write(true)
            .open(defs::MODULE_MOUNT_LOCK)
            .with_context(|| format!("failed to open {}", defs::MODULE_MOUNT_LOCK))?;
        flock(&file, FlockOperation::NonBlockingLockExclusive)
            .context("another module mount attempt is already running")?;
        Ok(Self { _file: file })
    }
}
