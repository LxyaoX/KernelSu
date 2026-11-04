use anyhow::{Context, Result, bail, ensure};
use clap::{Subcommand, ValueEnum};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::os::fd::AsRawFd;
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::path::{Path, PathBuf};
#[cfg(target_os = "android")]
use std::sync::mpsc;
#[cfg(target_os = "android")]
use std::thread;
use std::time::{Duration, Instant};

const SCHEMA_VERSION: u32 = 1;
const CONFIG_FILE: &str = "config.json";
const MODULE_FILE: &str = "module.ko";
const DISABLE_FILE: &str = "disable";
const LAST_RESULT_FILE: &str = "last_result.json";
const BANNER_FILE: &str = "banner.png";
const STATE_DIR: &str = ".state";
const STAGE_DIR: &str = ".stage";
const ROLLBACK_DIR: &str = ".rollback";
const REGISTRY_LOCK: &str = "registry.lock";
const LOCKS_DIR: &str = "locks";
const MAX_MODULE_BYTES: usize = 64 * 1024 * 1024;
const MAX_MODINFO_BYTES: usize = 1024 * 1024;
const MAX_ELF_SECTIONS: usize = 4096;
const MAX_MODINFO_ENTRIES: usize = 4096;
const MAX_FIELD_BYTES: usize = 16 * 1024;
const MAX_PARAM_BYTES: usize = 4096;
const MAX_LOG_BYTES: usize = 4096;
const MAX_CONFIG_BYTES: usize = 256 * 1024;
const LOCK_TIMEOUT: Duration = Duration::from_secs(2);
#[cfg(target_os = "android")]
const AUTOLOAD_STARTUP_WAIT: Duration = Duration::from_secs(8);
const PROTECTED_NAMES: &[&str] = &["kernelsu"];

#[derive(Debug, Subcommand)]
pub enum Command {
    /// List installed kernel modules as JSON
    List,
    /// Show one installed kernel module as JSON
    Info { id: String },
    /// Install or update a kernel module from a .ko file
    Install { path: PathBuf },
    /// Remove a managed kernel module
    Remove { id: String },
    /// Clear the persistent disable marker
    Enable { id: String },
    /// Persistently disable future loading without unloading now
    Disable { id: String },
    /// Set the next-boot autoload preference
    Autoload { id: String, state: AutoloadState },
    /// Store load parameters used by load and autoload
    Params { id: String, params: String },
    /// Load one managed kernel module now
    Load { id: String },
    /// Show the last operation result as JSON
    Log { id: String },
}

#[derive(Clone, Copy, Debug, ValueEnum)]
pub enum AutoloadState {
    On,
    Off,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum LoadState {
    Loaded,
    NotLoaded,
    Unknown,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ParameterInfo {
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub value_type: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ModInfoEntry {
    pub key: String,
    pub value: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct KernelModuleConfig {
    pub schema_version: u32,
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub internal_name: Option<String>,
    pub display_name: String,
    #[serde(default)]
    pub version: String,
    #[serde(default)]
    pub authors: Vec<String>,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub license: String,
    #[serde(default)]
    pub vermagic: String,
    #[serde(default)]
    pub architecture: String,
    #[serde(default)]
    pub depends: Vec<String>,
    #[serde(default)]
    pub parameters: Vec<ParameterInfo>,
    #[serde(default)]
    pub modinfo: Vec<ModInfoEntry>,
    #[serde(default)]
    pub diagnostics: Vec<String>,
    pub file_size: u64,
    pub sha256: String,
    #[serde(default)]
    pub autoload: bool,
    #[serde(default)]
    pub params: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LastResult {
    pub boot_id: String,
    pub operation: String,
    pub outcome: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub message: String,
}

#[allow(clippy::struct_excessive_bools)]
#[derive(Clone, Debug, Serialize)]
pub struct KernelModule {
    pub id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub internal_name: Option<String>,
    pub display_name: String,
    pub version: String,
    pub authors: Vec<String>,
    pub description: String,
    pub license: String,
    pub vermagic: String,
    pub architecture: String,
    pub depends: Vec<String>,
    pub parameters: Vec<ParameterInfo>,
    pub modinfo: Vec<ModInfoEntry>,
    pub diagnostics: Vec<String>,
    pub file_size: u64,
    pub sha256: String,
    pub autoload: bool,
    pub disabled: bool,
    pub effective_autoload: bool,
    pub loaded: LoadState,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub state_error: Option<String>,
    pub params: String,
    pub loadable: bool,
    pub protected: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[cfg_attr(not(target_os = "android"), allow(dead_code))]
#[derive(Clone, Debug, Serialize)]
pub struct ListOutput {
    pub schema_version: u32,
    pub safe_mode: bool,
    pub modules: Vec<KernelModule>,
}

#[cfg_attr(not(target_os = "android"), allow(dead_code))]
#[derive(Clone, Debug, Serialize)]
pub struct InfoOutput {
    pub schema_version: u32,
    pub safe_mode: bool,
    pub module: KernelModule,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_result: Option<LastResult>,
}

#[cfg_attr(not(target_os = "android"), allow(dead_code))]
#[derive(Debug, Serialize)]
pub struct ErrorOutput {
    pub schema_version: u32,
    pub safe_mode: bool,
    pub ok: bool,
    pub error: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct ActionOutput {
    pub schema_version: u32,
    pub ok: bool,
    pub id: String,
    pub operation: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub module: Option<KernelModule>,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct AutoloadReport {
    pub schema_version: u32,
    pub attempted: Vec<String>,
    pub loaded: Vec<String>,
    pub skipped: Vec<String>,
    pub failures: Vec<AutoloadFailure>,
}

#[derive(Clone, Debug, Serialize)]
pub struct AutoloadFailure {
    pub id: String,
    pub message: String,
}

#[derive(Debug)]
struct ParsedModule {
    internal_name: Option<String>,
    display_name: String,
    version: String,
    authors: Vec<String>,
    description: String,
    license: String,
    vermagic: String,
    architecture: String,
    depends: Vec<String>,
    parameters: Vec<ParameterInfo>,
    modinfo: Vec<ModInfoEntry>,
    diagnostics: Vec<String>,
    sha256: String,
    file_size: u64,
}

#[derive(Clone, Debug)]
struct StoredEntry {
    id: String,
    config: KernelModuleConfig,
    disabled: bool,
}

trait Runtime {
    fn boot_id(&self) -> Result<String>;
    fn loaded(&self, name: &str) -> Result<bool>;
    fn load(&self, path: &Path, params: &str) -> Result<()>;
}

struct SystemRuntime;

impl Runtime for SystemRuntime {
    fn boot_id(&self) -> Result<String> {
        let value =
            fs::read_to_string("/proc/sys/kernel/random/boot_id").context("read kernel boot id")?;
        let value = value.trim();
        ensure!(!value.is_empty(), "kernel boot id is empty");
        Ok(value.to_owned())
    }

    fn loaded(&self, name: &str) -> Result<bool> {
        validate_module_name(name)?;
        let path = Path::new("/sys/module").join(name);
        match fs::symlink_metadata(&path) {
            Ok(metadata) if metadata.file_type().is_symlink() => Ok(path.is_dir()),
            Ok(metadata) => Ok(metadata.is_dir()),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(false),
            Err(error) => Err(error).with_context(|| format!("inspect {}", path.display())),
        }
    }

    #[cfg(target_os = "android")]
    fn load(&self, path: &Path, params: &str) -> Result<()> {
        crate::module_loader::load_module(path, params)
    }

    #[cfg(not(target_os = "android"))]
    fn load(&self, path: &Path, params: &str) -> Result<()> {
        validate_params(params)?;
        ensure!(
            symlink_file_metadata(path)?.is_file(),
            "module image is not a regular file"
        );
        let file = OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW)
            .open(path)
            .with_context(|| format!("open {}", path.display()))?;
        let params = std::ffi::CString::new(params).context("module parameters contain NUL")?;
        rustix::system::finit_module(&file, &params, 0).context("finit_module failed")
    }
}

struct KernelModuleStore<R: Runtime = SystemRuntime> {
    root: PathBuf,
    runtime: R,
}

#[cfg(target_os = "android")]
impl KernelModuleStore<SystemRuntime> {
    fn managed() -> Self {
        Self {
            root: PathBuf::from(crate::defs::LKM_DIR),
            runtime: SystemRuntime,
        }
    }
}

impl<R: Runtime> KernelModuleStore<R> {
    #[cfg(test)]
    fn with_runtime(root: PathBuf, runtime: R) -> Self {
        Self { root, runtime }
    }

    fn list(&self) -> Result<Vec<KernelModule>> {
        let mut modules = Vec::new();
        for id in self.entry_ids()? {
            match self.read_entry(&id) {
                Ok(entry) => modules.push(self.view(entry)),
                Err(error) => modules.push(KernelModule {
                    id,
                    internal_name: None,
                    display_name: String::new(),
                    version: String::new(),
                    authors: Vec::new(),
                    description: String::new(),
                    license: String::new(),
                    vermagic: String::new(),
                    architecture: String::new(),
                    depends: Vec::new(),
                    parameters: Vec::new(),
                    modinfo: Vec::new(),
                    diagnostics: Vec::new(),
                    file_size: 0,
                    sha256: String::new(),
                    autoload: false,
                    disabled: false,
                    effective_autoload: false,
                    loaded: LoadState::Unknown,
                    state_error: None,
                    params: String::new(),
                    loadable: false,
                    protected: false,
                    error: Some(format!("{error:#}")),
                }),
            }
        }
        modules.sort_by(|left, right| {
            left.display_name
                .to_lowercase()
                .cmp(&right.display_name.to_lowercase())
                .then_with(|| left.id.cmp(&right.id))
        });
        Ok(modules)
    }

    #[cfg_attr(not(target_os = "android"), allow(dead_code))]
    fn info(&self, id: &str) -> Result<(KernelModule, Option<LastResult>)> {
        validate_id(id)?;
        let entry_lock = self.lock_entry(id)?;
        let entry = self.read_entry(id)?;
        let last_result = self.read_last_result(id);
        let view = self.view(entry);
        drop(entry_lock);
        Ok((view, last_result))
    }

    fn install(&self, source: &Path) -> Result<ActionOutput> {
        let _registry_lock = self.lock_registry()?;
        self.ensure_root()?;
        self.recover()?;

        let data = read_regular(source, MAX_MODULE_BYTES)?;
        let parsed = parse_module(&data, source)?;
        #[cfg(target_os = "android")]
        {
            let module_arch = parsed.architecture.as_str();
            let device_arch = std::env::consts::ARCH;
            ensure!(
                module_arch == device_arch,
                "module architecture {module_arch} does not match this device ({device_arch})"
            );
        }
        if let Some(name) = parsed.internal_name.as_deref()
            && is_protected(name)
        {
            bail!("internal module name {name:?} is reserved");
        }

        let candidate_id = parsed
            .internal_name
            .clone()
            .filter(|name| valid_id(name))
            .or_else(|| {
                source
                    .file_stem()
                    .and_then(|name| name.to_str())
                    .map(sanitize_id)
            })
            .context("cannot derive a stable id from the module")?;
        validate_id(&candidate_id)?;

        let id = if let Some(name) = parsed.internal_name.as_deref() {
            self.find_id_for_internal(name)?.unwrap_or(candidate_id)
        } else {
            candidate_id
        };
        let target = self.entry_path(&id);
        if target.exists()
            && let Ok(existing) = self.read_entry(&id)
            && let Some(old) = existing.config.internal_name.as_deref()
        {
            let Some(new) = parsed.internal_name.as_deref() else {
                bail!(
                    "id {id:?} already belongs to internal module {old:?}; the new file has no trustworthy internal name"
                );
            };
            if old != new {
                bail!("id {id:?} already belongs to internal module {old:?}, not {new:?}");
            }
        }

        let _entry_lock = self.lock_entry(&id)?;
        if self
            .read_last_result(&id)
            .is_some_and(|last| last.outcome == "inflight")
        {
            bail!("module {id:?} has a load in progress");
        }
        let old_entry = self.read_entry(&id).ok();
        let old_config = old_entry.as_ref().map(|entry| &entry.config);
        let was_disabled = old_entry.as_ref().is_some_and(|entry| entry.disabled);
        let mut config = config_from_parsed(id.clone(), parsed);
        if let Some(old) = old_config {
            config.autoload = old.autoload;
            config.params.clone_from(&old.params);
        }

        let stage = self
            .root
            .join(STAGE_DIR)
            .join(format!("{id}__{}", std::process::id()));
        if stage.exists() {
            fs::remove_dir_all(&stage)
                .with_context(|| format!("remove stale stage {}", stage.display()))?;
        }
        create_private_dir(&stage)?;
        write_private(&stage.join(MODULE_FILE), &data)?;
        write_json_bounded(&stage.join(CONFIG_FILE), &config, MAX_CONFIG_BYTES)?;
        if was_disabled {
            write_marker(&stage.join(DISABLE_FILE))?;
        }
        if old_entry.is_some() {
            self.preserve_entry_file(&id, LAST_RESULT_FILE, 64 * 1024, &stage)?;
            self.preserve_entry_file(&id, BANNER_FILE, 16 * 1024 * 1024, &stage)?;
        }
        sync_dir(&stage)?;

        if target.exists() {
            let rollback = self
                .root
                .join(ROLLBACK_DIR)
                .join(format!("update-{id}__{}", std::process::id()));
            if rollback.exists() {
                fs::remove_dir_all(&rollback)
                    .with_context(|| format!("remove stale rollback {}", rollback.display()))?;
            }
            create_private_parent(&rollback)?;
            fs::rename(&target, &rollback)
                .with_context(|| format!("move {} aside", target.display()))?;
            sync_dir(&self.root)?;
            sync_dir(&self.root.join(ROLLBACK_DIR))?;
            if let Err(error) = fs::rename(&stage, &target) {
                let _ = fs::rename(&rollback, &target);
                return Err(error).with_context(|| format!("publish {}", target.display()));
            }
            sync_dir(&self.root)?;
            sync_dir(&self.root.join(STAGE_DIR))?;
            fs::remove_dir_all(&rollback).ok();
        } else {
            fs::rename(&stage, &target).with_context(|| format!("publish {}", target.display()))?;
            sync_dir(&self.root)?;
            sync_dir(&self.root.join(STAGE_DIR))?;
        }
        sync_dir(&self.root)?;

        let entry = self.read_entry(&id)?;
        Ok(ActionOutput {
            schema_version: SCHEMA_VERSION,
            ok: true,
            id,
            operation: "install".to_owned(),
            module: Some(self.view(entry)),
        })
    }

    fn remove(&self, id: &str) -> Result<ActionOutput> {
        validate_id(id)?;
        let _registry_lock = self.lock_registry()?;
        let _entry_lock = self.lock_entry(id)?;
        if self
            .read_last_result(id)
            .is_some_and(|last| last.outcome == "inflight")
        {
            bail!("module {id:?} has a load in progress");
        }
        let view = self.read_entry(id).ok().map(|entry| self.view(entry));
        let target = self.entry_path(id);
        let trash = self
            .root
            .join(ROLLBACK_DIR)
            .join(format!("removed-{id}__{}", std::process::id()));
        create_private_parent(&trash)?;
        fs::rename(&target, &trash).with_context(|| format!("remove {}", target.display()))?;
        sync_dir(&self.root)?;
        sync_dir(&self.root.join(ROLLBACK_DIR))?;
        fs::remove_dir_all(&trash).ok();
        sync_dir(&self.root.join(ROLLBACK_DIR))?;
        Ok(ActionOutput {
            schema_version: SCHEMA_VERSION,
            ok: true,
            id: id.to_owned(),
            operation: "remove".to_owned(),
            module: view,
        })
    }

    fn set_enabled(&self, id: &str, enabled: bool) -> Result<ActionOutput> {
        ensure_enable_policy(enabled, safe_mode())?;
        let _registry_lock = self.lock_registry()?;
        let _entry_lock = self.lock_entry(id)?;
        self.ensure_entry_config(id)?;
        let marker = self.entry_path(id).join(DISABLE_FILE);
        if enabled {
            match fs::remove_file(&marker) {
                Ok(()) => {}
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                Err(error) => {
                    return Err(error).with_context(|| format!("remove {}", marker.display()));
                }
            }
        } else {
            write_marker(&marker)?;
        }
        sync_dir(&self.entry_path(id))?;
        self.action(id, if enabled { "enable" } else { "disable" })
    }

    fn set_autoload(&self, id: &str, enabled: bool) -> Result<ActionOutput> {
        if enabled {
            ensure!(!safe_mode(), "safe mode prohibits enabling autoload");
        }
        let _registry_lock = self.lock_registry()?;
        let _entry_lock = self.lock_entry(id)?;
        let mut config = self.ensure_entry_config(id)?;
        ensure!(
            config.internal_name.is_some(),
            "module has no trustworthy internal name; autoload is unavailable"
        );
        if let Some(name) = config.internal_name.as_deref() {
            ensure!(!is_protected(name), "protected modules cannot autoload");
        }
        config.autoload = enabled;
        self.write_config(&config)?;
        if enabled {
            let marker = self.entry_path(id).join(DISABLE_FILE);
            if marker.exists() {
                fs::remove_file(&marker)?;
                sync_dir(&self.entry_path(id))?;
            }
        }
        self.action(id, "autoload")
    }

    fn set_params(&self, id: &str, params: &str) -> Result<ActionOutput> {
        validate_params(params)?;
        let _registry_lock = self.lock_registry()?;
        let _entry_lock = self.lock_entry(id)?;
        let mut config = self.ensure_entry_config(id)?;
        params.clone_into(&mut config.params);
        self.write_config(&config)?;
        self.action(id, "params")
    }

    fn load_manual(&self, id: &str) -> Result<ActionOutput> {
        ensure!(!safe_mode(), "safe mode prohibits loading kernel modules");
        let entry_lock = {
            let _registry_lock = self.lock_registry()?;
            self.lock_entry(id)?
        };
        let entry = self.read_entry(id)?;
        ensure!(
            !entry.disabled,
            "module is disabled; enable it before loading"
        );
        let name = entry
            .config
            .internal_name
            .as_deref()
            .context("module has no trustworthy internal name")?;
        ensure!(
            !is_protected(name),
            "protected modules cannot be loaded through kmod"
        );
        let module_path = self.verify_module_image(id, &entry.config)?;

        match self.runtime.loaded(name) {
            Ok(true) => {
                self.write_result(
                    id,
                    &LastResult {
                        boot_id: self.boot_id().unwrap_or_else(|_| "unknown".to_owned()),
                        operation: "load".to_owned(),
                        outcome: "already_loaded".to_owned(),
                        message: String::new(),
                    },
                )?;
                bail!("kernel module {name:?} is already loaded");
            }
            Ok(false) => {}
            Err(error) => {
                return Err(error).context("cannot determine current kernel module state");
            }
        }

        let boot_id = self.boot_id()?;
        if let Some(last) = self.read_last_result_checked(id)?
            && last.outcome == "inflight"
        {
            bail!("kernel module {name:?} has a load already in progress");
        }
        self.write_result(
            id,
            &LastResult {
                boot_id: boot_id.clone(),
                operation: "load".to_owned(),
                outcome: "inflight".to_owned(),
                message: String::new(),
            },
        )?;
        drop(entry_lock);
        let result = self.runtime.load(&module_path, &entry.config.params);
        let _entry_lock = self
            .lock_entry(id)
            .context("reacquire module lock after load")?;
        let (outcome, message) = match &result {
            Ok(()) => ("success".to_owned(), String::new()),
            Err(error) => ("error".to_owned(), bounded_message(error)),
        };
        self.write_result(
            id,
            &LastResult {
                boot_id,
                operation: "load".to_owned(),
                outcome,
                message,
            },
        )?;
        result.context("load managed kernel module")?;
        self.action(id, "load")
    }

    fn log(&self, id: &str) -> Result<(KernelModule, Option<LastResult>)> {
        validate_id(id)?;
        let entry_lock = self.lock_entry(id)?;
        let entry = self.read_entry(id)?;
        let last_result = self.read_last_result_checked(id)?;
        let view = self.view(entry);
        drop(entry_lock);
        Ok((view, last_result))
    }

    fn autoload(&self) -> Result<AutoloadReport> {
        let mut report = AutoloadReport {
            schema_version: SCHEMA_VERSION,
            ..AutoloadReport::default()
        };
        if safe_mode() {
            report.failures.push(AutoloadFailure {
                id: String::new(),
                message: "safe mode prohibits autoload".to_owned(),
            });
            return Ok(report);
        }

        let mut entries = Vec::new();
        {
            let _registry_lock = self.lock_registry()?;
            self.recover()?;
            for id in self.entry_ids()? {
                match self.read_entry(&id) {
                    Ok(entry) => entries.push(entry),
                    Err(error) => report.failures.push(AutoloadFailure {
                        id,
                        message: bounded_message(&error),
                    }),
                }
            }
        }

        let boot_id = self.boot_id()?;
        let mut by_name = HashMap::new();
        for entry in &entries {
            if let Some(name) = entry.config.internal_name.as_deref()
                && let Some(existing) = by_name.insert(name.to_owned(), entry.id.clone())
                && existing != entry.id
            {
                report.failures.push(AutoloadFailure {
                    id: entry.id.clone(),
                    message: format!("internal module name {name:?} is also used by {existing:?}"),
                });
            }
        }

        let mut candidates = BTreeMap::new();
        for entry in entries {
            let Some(name) = entry.config.internal_name.as_deref() else {
                if entry.config.autoload && !entry.disabled {
                    report.failures.push(AutoloadFailure {
                        id: entry.id,
                        message: "module has no trustworthy internal name".to_owned(),
                    });
                }
                continue;
            };
            if !entry.config.autoload || entry.disabled || is_protected(name) {
                continue;
            }
            candidates.insert(entry.id.clone(), entry);
        }

        let candidate_ids: Vec<String> = candidates.keys().cloned().collect();
        for id in candidate_ids {
            match self.read_last_result_checked(&id) {
                Ok(Some(last))
                    if last.boot_id == boot_id
                        && matches!(last.operation.as_str(), "load" | "autoload") =>
                {
                    candidates.remove(&id);
                    report.skipped.push(id);
                }
                Ok(_) => {}
                Err(error) => {
                    let message = bounded_message(&error);
                    candidates.remove(&id);
                    report.failures.push(AutoloadFailure {
                        id: id.clone(),
                        message: message.clone(),
                    });
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: "error".to_owned(),
                            message,
                        },
                    )?;
                }
            }
        }

        let ordered = order_candidates(&candidates, &by_name, &self.runtime, &mut report.failures);
        for failure in &report.failures {
            if candidates.contains_key(&failure.id) {
                self.write_result(
                    &failure.id,
                    &LastResult {
                        boot_id: boot_id.clone(),
                        operation: "autoload".to_owned(),
                        outcome: "error".to_owned(),
                        message: failure.message.clone(),
                    },
                )?;
            }
        }

        for id in ordered {
            let entry_lock = match self.lock_entry(&id) {
                Ok(lock) => lock,
                Err(error) => {
                    report.failures.push(AutoloadFailure {
                        id,
                        message: bounded_message(&error),
                    });
                    continue;
                }
            };
            let entry = match self.read_entry(&id) {
                Ok(entry) => entry,
                Err(error) => {
                    report.failures.push(AutoloadFailure {
                        id,
                        message: bounded_message(&error),
                    });
                    continue;
                }
            };
            let Some(entry_name) = entry.config.internal_name.as_deref() else {
                report.failures.push(AutoloadFailure {
                    id,
                    message: "module has no trustworthy internal name".to_owned(),
                });
                continue;
            };
            if !entry.config.autoload || entry.disabled || is_protected(entry_name) {
                report.skipped.push(id);
                continue;
            }
            let last_result = match self.read_last_result_checked(&id) {
                Ok(result) => result,
                Err(error) => {
                    report.failures.push(AutoloadFailure {
                        id: id.clone(),
                        message: bounded_message(&error),
                    });
                    continue;
                }
            };
            if let Some(last) = last_result {
                if last.outcome == "interrupted" {
                    report.skipped.push(id);
                    continue;
                }
                if last.outcome == "inflight" {
                    let message = if last.boot_id == boot_id {
                        "autoload already started for this boot"
                    } else {
                        "previous load did not finish; automatic retry is held"
                    };
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: if last.boot_id == boot_id {
                                "skipped".to_owned()
                            } else {
                                "interrupted".to_owned()
                            },
                            message: message.to_owned(),
                        },
                    )?;
                    report.skipped.push(id);
                    continue;
                }
                if last.boot_id == boot_id && matches!(last.operation.as_str(), "load" | "autoload")
                {
                    report.skipped.push(id);
                    continue;
                }
            }

            let name = entry
                .config
                .internal_name
                .as_deref()
                .expect("candidate has internal name");
            let module_path = match self.verify_module_image(&id, &entry.config) {
                Ok(path) => path,
                Err(error) => {
                    let message = bounded_message(&error);
                    report.failures.push(AutoloadFailure {
                        id: id.clone(),
                        message: message.clone(),
                    });
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: "error".to_owned(),
                            message,
                        },
                    )?;
                    continue;
                }
            };
            match self.runtime.loaded(name) {
                Ok(true) => {
                    report.skipped.push(id);
                    continue;
                }
                Ok(false) => {}
                Err(error) => {
                    let message = bounded_message(&error);
                    report.failures.push(AutoloadFailure {
                        id: id.clone(),
                        message: message.clone(),
                    });
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: "error".to_owned(),
                            message,
                        },
                    )?;
                    continue;
                }
            }

            report.attempted.push(id.clone());
            self.write_result(
                &id,
                &LastResult {
                    boot_id: boot_id.clone(),
                    operation: "autoload".to_owned(),
                    outcome: "inflight".to_owned(),
                    message: String::new(),
                },
            )?;
            drop(entry_lock);
            let load_result = self.runtime.load(&module_path, &entry.config.params);
            let _entry_lock = match self.lock_entry(&id) {
                Ok(lock) => lock,
                Err(error) => {
                    report.failures.push(AutoloadFailure {
                        id,
                        message: bounded_message(&error),
                    });
                    continue;
                }
            };
            match load_result {
                Ok(()) => {
                    report.loaded.push(id.clone());
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: "success".to_owned(),
                            message: String::new(),
                        },
                    )?;
                }
                Err(error) => {
                    let message = bounded_message(&error);
                    report.failures.push(AutoloadFailure {
                        id: id.clone(),
                        message: message.clone(),
                    });
                    self.write_result(
                        &id,
                        &LastResult {
                            boot_id: boot_id.clone(),
                            operation: "autoload".to_owned(),
                            outcome: "error".to_owned(),
                            message,
                        },
                    )?;
                }
            }
        }
        Ok(report)
    }

    fn disable_all_for_safe_mode(&self) -> Result<()> {
        let _registry_lock = self.lock_registry()?;
        self.ensure_root()?;
        let mut failures = Vec::new();
        for id in self.entry_ids()? {
            let marker = self.entry_path(&id).join(DISABLE_FILE);
            if let Err(error) = write_marker(&marker) {
                failures.push(format!("{id}: {error:#}"));
            }
        }
        ensure!(
            failures.is_empty(),
            "could not disable {} kernel module(s): {}",
            failures.len(),
            failures.join("; ")
        );
        Ok(())
    }

    fn ensure_root(&self) -> Result<()> {
        fs::create_dir_all(&self.root)
            .with_context(|| format!("create {}", self.root.display()))?;
        let metadata = symlink_file_metadata(&self.root)?;
        ensure!(metadata.is_dir(), "LKM root is not a regular directory");
        fs::set_permissions(&self.root, fs::Permissions::from_mode(0o700))
            .with_context(|| format!("set permissions on {}", self.root.display()))?;
        for directory in [STATE_DIR, STAGE_DIR, ROLLBACK_DIR] {
            create_private_dir(&self.root.join(directory))?;
        }
        create_private_dir(&self.root.join(STATE_DIR).join(LOCKS_DIR))?;
        Ok(())
    }

    fn clean_staging(&self) -> Result<()> {
        let stage_root = self.root.join(STAGE_DIR);
        if !stage_root.exists() {
            return Ok(());
        }
        for item in
            fs::read_dir(&stage_root).with_context(|| format!("read {}", stage_root.display()))?
        {
            let item = item?;
            let metadata = symlink_file_metadata(&item.path())?;
            if metadata.is_dir() {
                fs::remove_dir_all(item.path())?;
            } else {
                fs::remove_file(item.path())?;
            }
        }
        Ok(())
    }

    fn recover(&self) -> Result<()> {
        let rollback_root = self.root.join(ROLLBACK_DIR);
        if rollback_root.exists() {
            for item in fs::read_dir(&rollback_root)? {
                let item = item?;
                let path = item.path();
                let metadata = fs::symlink_metadata(&path)?;
                if metadata.file_type().is_symlink() {
                    fs::remove_file(&path).ok();
                    continue;
                }
                if !metadata.is_dir() {
                    fs::remove_file(&path).ok();
                    continue;
                }
                let name = item.file_name();
                let name = name.to_string_lossy();
                if name.starts_with("removed-") {
                    fs::remove_dir_all(&path).ok();
                } else if let Some(payload) = name.strip_prefix("update-") {
                    recover_rollback_entry(payload, &path, self)?;
                } else if name.contains("__") {
                    recover_rollback_entry(name.as_ref(), &path, self)?;
                }
            }
        }
        self.clean_staging()
    }

    fn entry_ids(&self) -> Result<Vec<String>> {
        if !self.root.exists() {
            return Ok(Vec::new());
        }
        let root_metadata = symlink_file_metadata(&self.root)?;
        ensure!(
            root_metadata.is_dir(),
            "LKM root is not a regular directory"
        );
        let mut ids = Vec::new();
        for item in
            fs::read_dir(&self.root).with_context(|| format!("read {}", self.root.display()))?
        {
            let item = item?;
            let name = item.file_name();
            let Some(id) = name.to_str() else {
                continue;
            };
            if !valid_id(id) {
                continue;
            }
            let metadata = fs::symlink_metadata(item.path())
                .with_context(|| format!("inspect {}", item.path().display()))?;
            if metadata.is_dir() && !metadata.file_type().is_symlink() {
                ids.push(id.to_owned());
            }
        }
        ids.sort();
        Ok(ids)
    }

    fn read_entry(&self, id: &str) -> Result<StoredEntry> {
        validate_id(id)?;
        let root = self.entry_path(id);
        let metadata = symlink_file_metadata(&root)?;
        ensure!(metadata.is_dir(), "{} is not a directory", root.display());
        let config_path = root.join(CONFIG_FILE);
        let bytes = read_regular(&config_path, MAX_CONFIG_BYTES)?;
        let config: KernelModuleConfig =
            serde_json::from_slice(&bytes).context("parse config.json")?;
        ensure!(
            config.schema_version == SCHEMA_VERSION,
            "unsupported config schema {}",
            config.schema_version
        );
        ensure!(config.id == id, "config id does not match directory");
        let module_metadata = symlink_file_metadata(&root.join(MODULE_FILE))?;
        ensure!(module_metadata.is_file(), "module.ko is not a regular file");
        ensure!(
            module_metadata.len() == config.file_size,
            "module.ko size changed after install"
        );
        if let Some(name) = config.internal_name.as_deref() {
            validate_module_name(name)?;
        }
        let disabled = match fs::symlink_metadata(root.join(DISABLE_FILE)) {
            Ok(metadata) => {
                ensure!(
                    !metadata.file_type().is_symlink(),
                    "disable marker is a symbolic link"
                );
                ensure!(metadata.is_file(), "disable marker is not a regular file");
                true
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => false,
            Err(error) => return Err(error.into()),
        };
        Ok(StoredEntry {
            id: id.to_owned(),
            config,
            disabled,
        })
    }

    fn ensure_entry_config(&self, id: &str) -> Result<KernelModuleConfig> {
        self.ensure_root()?;
        self.read_entry(id).map(|entry| entry.config)
    }

    fn view(&self, entry: StoredEntry) -> KernelModule {
        let internal_name = entry.config.internal_name.clone();
        let protected = internal_name.as_deref().is_some_and(is_protected);
        let (loaded, state_error) = internal_name.as_deref().map_or_else(
            || {
                (
                    LoadState::Unknown,
                    Some("internal module name is unavailable".to_owned()),
                )
            },
            |name| match self.runtime.loaded(name) {
                Ok(loaded) => (
                    if loaded {
                        LoadState::Loaded
                    } else {
                        LoadState::NotLoaded
                    },
                    None,
                ),
                Err(error) => (LoadState::Unknown, Some(bounded_message(&error))),
            },
        );
        let effective_autoload = entry.config.autoload && !entry.disabled;
        let loadable = internal_name.is_some() && !entry.disabled && !protected;
        KernelModule {
            id: entry.id,
            internal_name,
            display_name: entry.config.display_name,
            version: entry.config.version,
            authors: entry.config.authors,
            description: entry.config.description,
            license: entry.config.license,
            vermagic: entry.config.vermagic,
            architecture: entry.config.architecture,
            depends: entry.config.depends,
            parameters: entry.config.parameters,
            modinfo: entry.config.modinfo,
            diagnostics: entry.config.diagnostics,
            file_size: entry.config.file_size,
            sha256: entry.config.sha256,
            autoload: entry.config.autoload,
            disabled: entry.disabled,
            effective_autoload,
            loaded,
            state_error,
            params: entry.config.params,
            loadable,
            protected,
            error: None,
        }
    }

    fn action(&self, id: &str, operation: &str) -> Result<ActionOutput> {
        let entry = self.read_entry(id)?;
        Ok(ActionOutput {
            schema_version: SCHEMA_VERSION,
            ok: true,
            id: id.to_owned(),
            operation: operation.to_owned(),
            module: Some(self.view(entry)),
        })
    }

    fn write_config(&self, config: &KernelModuleConfig) -> Result<()> {
        let path = self.entry_path(&config.id).join(CONFIG_FILE);
        write_json_atomic(&path, config)
    }

    fn write_result(&self, id: &str, result: &LastResult) -> Result<()> {
        let path = self.entry_path(id).join(LAST_RESULT_FILE);
        write_json_atomic(&path, result)
    }

    #[cfg_attr(not(target_os = "android"), allow(dead_code))]
    fn read_last_result(&self, id: &str) -> Option<LastResult> {
        self.read_last_result_checked(id).ok().flatten()
    }

    fn read_last_result_checked(&self, id: &str) -> Result<Option<LastResult>> {
        let path = self.entry_path(id).join(LAST_RESULT_FILE);
        match fs::symlink_metadata(&path) {
            Ok(_) => Ok(Some(serde_json::from_slice(&read_regular(
                &path,
                64 * 1024,
            )?)?)),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(error) => Err(error).with_context(|| format!("inspect {}", path.display())),
        }
    }

    fn find_id_for_internal(&self, internal_name: &str) -> Result<Option<String>> {
        let mut found = None;
        for id in self.entry_ids()? {
            if let Ok(entry) = self.read_entry(&id)
                && entry.config.internal_name.as_deref() == Some(internal_name)
                && found.replace(id.clone()).is_some()
            {
                bail!("multiple managed entries claim internal module {internal_name:?}");
            }
        }
        Ok(found)
    }

    fn boot_id(&self) -> Result<String> {
        self.runtime.boot_id()
    }

    fn verify_module_image(&self, id: &str, config: &KernelModuleConfig) -> Result<PathBuf> {
        let path = self.entry_path(id).join(MODULE_FILE);
        let data = read_regular(&path, MAX_MODULE_BYTES)?;
        ensure!(
            sha256::digest(&data) == config.sha256,
            "module.ko digest changed after install"
        );
        let parsed = parse_module(&data, &path)?;
        ensure!(
            parsed.internal_name == config.internal_name,
            "module.ko internal name changed after install"
        );
        Ok(path)
    }

    fn preserve_entry_file(
        &self,
        id: &str,
        name: &str,
        limit: usize,
        destination: &Path,
    ) -> Result<()> {
        let source = self.entry_path(id).join(name);
        match fs::symlink_metadata(&source) {
            Ok(metadata) if metadata.file_type().is_symlink() => {
                bail!("{} is a symbolic link", source.display());
            }
            Ok(metadata) if metadata.is_file() => {
                let data = read_regular(&source, limit)?;
                write_private(&destination.join(name), &data)?;
            }
            Ok(_) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => {
                return Err(error).with_context(|| format!("inspect {}", source.display()));
            }
        }
        Ok(())
    }

    fn entry_path(&self, id: &str) -> PathBuf {
        self.root.join(id)
    }

    fn lock_registry(&self) -> Result<FileLock> {
        self.ensure_root()?;
        FileLock::acquire(&self.root.join(STATE_DIR).join(REGISTRY_LOCK))
    }

    fn lock_entry(&self, id: &str) -> Result<FileLock> {
        validate_id(id)?;
        self.ensure_root()?;
        FileLock::acquire(
            &self
                .root
                .join(STATE_DIR)
                .join(LOCKS_DIR)
                .join(format!("{id}.lock")),
        )
    }
}

struct FileLock {
    file: File,
}

impl FileLock {
    fn acquire(path: &Path) -> Result<Self> {
        create_private_parent(path)?;
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .mode(0o600)
            .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW)
            .open(path)
            .with_context(|| format!("open lock {}", path.display()))?;
        let deadline = Instant::now() + LOCK_TIMEOUT;
        loop {
            let result = unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) };
            if result == 0 {
                return Ok(Self { file });
            }
            let error = std::io::Error::last_os_error();
            if !matches!(error.raw_os_error(), Some(code) if code == libc::EWOULDBLOCK || code == libc::EAGAIN)
            {
                return Err(error).with_context(|| format!("lock {}", path.display()));
            }
            if Instant::now() >= deadline {
                bail!("lock busy: {}", path.display());
            }
            std::thread::sleep(Duration::from_millis(20));
        }
    }
}

impl Drop for FileLock {
    fn drop(&mut self) {
        unsafe {
            libc::flock(self.file.as_raw_fd(), libc::LOCK_UN);
        }
    }
}

fn config_from_parsed(id: String, parsed: ParsedModule) -> KernelModuleConfig {
    KernelModuleConfig {
        schema_version: SCHEMA_VERSION,
        id,
        internal_name: parsed.internal_name,
        display_name: parsed.display_name,
        version: parsed.version,
        authors: parsed.authors,
        description: parsed.description,
        license: parsed.license,
        vermagic: parsed.vermagic,
        architecture: parsed.architecture,
        depends: parsed.depends,
        parameters: parsed.parameters,
        modinfo: parsed.modinfo,
        diagnostics: parsed.diagnostics,
        file_size: parsed.file_size,
        sha256: parsed.sha256,
        autoload: false,
        params: String::new(),
    }
}

fn parse_module(data: &[u8], source: &Path) -> Result<ParsedModule> {
    ensure!(!data.is_empty(), "module file is empty");
    ensure!(
        data.len() <= MAX_MODULE_BYTES,
        "module exceeds {MAX_MODULE_BYTES} bytes"
    );
    ensure!(data.len() >= 52, "ELF header is truncated");
    ensure!(&data[..4] == b"\x7fELF", "module is not an ELF file");

    let class = data[4];
    let endian = data[5];
    ensure!(matches!(class, 1 | 2), "unsupported ELF class");
    ensure!(matches!(endian, 1 | 2), "unsupported ELF byte order");
    let is_64 = class == 2;
    let little = endian == 1;
    let minimum_header = if is_64 { 64 } else { 52 };
    ensure!(data.len() >= minimum_header, "ELF header is truncated");

    let e_type = read_u16(data, 16, little)?;
    ensure!(e_type == 1, "ELF file is not a relocatable kernel module");
    let e_machine = read_u16(data, 18, little)?;
    let architecture = match e_machine {
        183 => "aarch64",
        62 => "x86_64",
        243 => "riscv64",
        40 => "arm",
        3 => "x86",
        other => bail!("unsupported ELF machine {other}"),
    };

    let (section_offset, section_entry_size, section_count, shstr_index) = if is_64 {
        (
            checked_usize(read_u64(data, 40, little)?, "section table offset")?,
            checked_usize(u64::from(read_u16(data, 58, little)?), "section entry size")?,
            checked_usize(u64::from(read_u16(data, 60, little)?), "section count")?,
            u64::from(read_u16(data, 62, little)?),
        )
    } else {
        (
            checked_usize(
                u64::from(read_u32(data, 32, little)?),
                "section table offset",
            )?,
            checked_usize(u64::from(read_u16(data, 46, little)?), "section entry size")?,
            checked_usize(u64::from(read_u16(data, 48, little)?), "section count")?,
            u64::from(read_u16(data, 50, little)?),
        )
    };
    let minimum_section_size = if is_64 { 64 } else { 40 };
    ensure!(
        section_entry_size >= minimum_section_size,
        "ELF section header is too small"
    );
    ensure!(
        section_count > 0 && section_count <= MAX_ELF_SECTIONS,
        "ELF section count is invalid"
    );
    let section_table_size = section_entry_size
        .checked_mul(section_count)
        .context("ELF section table size overflow")?;
    ensure!(
        section_offset
            .checked_add(section_table_size)
            .is_some_and(|end| end <= data.len()),
        "ELF section table is outside the file"
    );
    ensure!(
        shstr_index < section_count as u64,
        "ELF section name table index is invalid"
    );

    let shstr = read_section(
        data,
        section_offset,
        section_entry_size,
        shstr_index as usize,
        is_64,
        little,
    )?;
    ensure!(
        shstr.kind == 3,
        "ELF section name table is not a string table"
    );
    let shstr_data = slice_range(data, shstr.offset, shstr.size, "ELF section name table")?;
    let mut modinfo_section = None;

    for index in 0..section_count {
        let section = read_section(
            data,
            section_offset,
            section_entry_size,
            index,
            is_64,
            little,
        )?;
        let name = c_string_at(shstr_data, section.name_offset, "ELF section name")?;
        if name == b".modinfo" {
            modinfo_section = Some(section);
            break;
        }
    }
    let section = modinfo_section.context("module has no .modinfo section")?;
    ensure!(
        section.size <= MAX_MODINFO_BYTES,
        ".modinfo section is too large"
    );
    let modinfo_data = slice_range(data, section.offset, section.size, ".modinfo")?;
    let entries = parse_modinfo(modinfo_data)?;

    let mut names = Vec::new();
    let mut versions = Vec::new();
    let mut authors = Vec::new();
    let mut descriptions = Vec::new();
    let mut licenses = Vec::new();
    let mut vermagic = Vec::new();
    let mut depends = BTreeSet::new();
    let mut parameter_types = BTreeMap::new();
    let mut parameters = Vec::new();
    let mut diagnostics = Vec::new();

    for entry in &entries {
        match entry.key.as_str() {
            "name" => names.push(entry.value.clone()),
            "version" => versions.push(entry.value.clone()),
            "author" => authors.push(entry.value.clone()),
            "description" => descriptions.push(entry.value.clone()),
            "license" => licenses.push(entry.value.clone()),
            "vermagic" => vermagic.push(entry.value.clone()),
            "depends" => {
                for name in split_list(&entry.value) {
                    depends.insert(name);
                }
            }
            "parmtype" => {
                if let Some((name, value_type)) = entry.value.split_once(':') {
                    parameter_types.insert(name.to_owned(), value_type.to_owned());
                }
            }
            "parm" => {
                let name = entry
                    .value
                    .split_once(':')
                    .map_or(entry.value.as_str(), |(name, _)| name)
                    .trim();
                if valid_module_name(name) {
                    parameters.push(ParameterInfo {
                        name: name.to_owned(),
                        value_type: None,
                    });
                } else {
                    diagnostics.push(format!("ignored invalid parameter name {name:?}"));
                }
            }
            _ => {}
        }
    }
    for parameter in &mut parameters {
        parameter.value_type = parameter_types.remove(&parameter.name);
    }
    parameters.sort_by(|left, right| left.name.cmp(&right.name));
    parameters.dedup_by(|left, right| left.name == right.name);

    let internal_name = unique_valid_name(&names, &mut diagnostics);
    let display_name = internal_name
        .clone()
        .or_else(|| {
            source
                .file_stem()
                .and_then(|value| value.to_str())
                .map(ToOwned::to_owned)
        })
        .unwrap_or_else(|| "Kernel module".to_owned());
    let sha256 = sha256::digest(data);
    Ok(ParsedModule {
        internal_name,
        display_name,
        version: join_first_or_newlines(versions),
        authors,
        description: join_first_or_newlines(descriptions),
        license: join_first_or_newlines(licenses),
        vermagic: join_first_or_newlines(vermagic),
        architecture: architecture.to_owned(),
        depends: depends.into_iter().collect(),
        parameters,
        modinfo: entries,
        diagnostics,
        sha256,
        file_size: data.len() as u64,
    })
}

struct ElfSection {
    name_offset: u32,
    kind: u32,
    offset: usize,
    size: usize,
}

fn read_section(
    data: &[u8],
    table_offset: usize,
    entry_size: usize,
    index: usize,
    is_64: bool,
    little: bool,
) -> Result<ElfSection> {
    let offset = table_offset
        .checked_add(
            index
                .checked_mul(entry_size)
                .context("ELF section offset overflow")?,
        )
        .context("ELF section offset overflow")?;
    let header = data
        .get(
            offset
                ..offset
                    .checked_add(entry_size)
                    .context("ELF section overflow")?,
        )
        .context("ELF section is outside the file")?;
    if is_64 {
        Ok(ElfSection {
            name_offset: read_u32(header, 0, little)?,
            kind: read_u32(header, 4, little)?,
            offset: checked_usize(read_u64(header, 24, little)?, "ELF section data offset")?,
            size: checked_usize(read_u64(header, 32, little)?, "ELF section data size")?,
        })
    } else {
        Ok(ElfSection {
            name_offset: read_u32(header, 0, little)?,
            kind: read_u32(header, 4, little)?,
            offset: checked_usize(
                u64::from(read_u32(header, 16, little)?),
                "ELF section data offset",
            )?,
            size: checked_usize(
                u64::from(read_u32(header, 20, little)?),
                "ELF section data size",
            )?,
        })
    }
}

fn parse_modinfo(data: &[u8]) -> Result<Vec<ModInfoEntry>> {
    let mut entries = Vec::new();
    for raw in data.split(|byte| *byte == 0) {
        if raw.is_empty() {
            continue;
        }
        ensure!(
            entries.len() < MAX_MODINFO_ENTRIES,
            ".modinfo contains too many entries"
        );
        ensure!(raw.len() <= MAX_FIELD_BYTES, ".modinfo entry is too large");
        let Some(separator) = raw.iter().position(|byte| *byte == b'=') else {
            continue;
        };
        let key = &raw[..separator];
        let value = &raw[separator + 1..];
        ensure!(
            !key.is_empty()
                && key
                    .iter()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(*byte, b'_' | b'-')),
            ".modinfo contains an invalid key"
        );
        entries.push(ModInfoEntry {
            key: String::from_utf8_lossy(key).into_owned(),
            value: clean_text(&String::from_utf8_lossy(value), MAX_FIELD_BYTES),
        });
    }
    Ok(entries)
}

fn unique_valid_name(values: &[String], diagnostics: &mut Vec<String>) -> Option<String> {
    let names: BTreeSet<_> = values
        .iter()
        .filter_map(|value| {
            let trimmed = value.trim();
            if valid_module_name(trimmed) {
                Some(trimmed.to_owned())
            } else {
                diagnostics.push(format!("ignored invalid internal module name {trimmed:?}"));
                None
            }
        })
        .collect();
    if names.len() == 1 {
        names.into_iter().next()
    } else {
        if names.len() > 1 {
            diagnostics.push("multiple internal module names were found".to_owned());
        } else {
            diagnostics.push("no trustworthy internal module name was found".to_owned());
        }
        None
    }
}

fn join_first_or_newlines(values: Vec<String>) -> String {
    if values.len() <= 1 {
        values.into_iter().next().unwrap_or_default()
    } else {
        values.join("\n")
    }
}

fn split_list(value: &str) -> impl Iterator<Item = String> + '_ {
    value
        .split(',')
        .map(str::trim)
        .filter(|item| !item.is_empty())
        .map(ToOwned::to_owned)
}

fn order_candidates<R: Runtime>(
    candidates: &BTreeMap<String, StoredEntry>,
    by_name: &HashMap<String, String>,
    runtime: &R,
    failures: &mut Vec<AutoloadFailure>,
) -> Vec<String> {
    let mut result = Vec::new();
    let mut permanent = HashSet::new();
    for id in candidates.keys() {
        let mut temporary = HashSet::new();
        if let Err(error) = visit_candidate(
            id,
            candidates,
            by_name,
            runtime,
            &mut permanent,
            &mut temporary,
            &mut result,
        ) {
            failures.push(AutoloadFailure {
                id: id.clone(),
                message: bounded_message(&error),
            });
        }
    }
    result
}

#[allow(clippy::too_many_arguments)]
fn visit_candidate<R: Runtime>(
    id: &str,
    candidates: &BTreeMap<String, StoredEntry>,
    by_name: &HashMap<String, String>,
    runtime: &R,
    permanent: &mut HashSet<String>,
    temporary: &mut HashSet<String>,
    result: &mut Vec<String>,
) -> Result<()> {
    if permanent.contains(id) {
        return Ok(());
    }
    if !temporary.insert(id.to_owned()) {
        bail!("kernel module dependency cycle includes {id:?}");
    }
    let entry = candidates
        .get(id)
        .context("dependency candidate disappeared")?;
    for dependency in &entry.config.depends {
        match runtime.loaded(dependency) {
            Ok(true) => continue,
            Ok(false) => {}
            Err(error) => {
                return Err(error).with_context(|| {
                    format!("cannot determine dependency {dependency:?} for module {id:?}")
                });
            }
        }
        if let Some(dependency_id) = by_name.get(dependency)
            && candidates.contains_key(dependency_id)
        {
            visit_candidate(
                dependency_id,
                candidates,
                by_name,
                runtime,
                permanent,
                temporary,
                result,
            )?;
            continue;
        }
        bail!("kernel module {id:?} has unmet dependency {dependency:?}");
    }
    temporary.remove(id);
    permanent.insert(id.to_owned());
    result.push(id.to_owned());
    Ok(())
}

fn validate_module_name(name: &str) -> Result<()> {
    ensure!(
        valid_module_name(name),
        "invalid internal module name {name:?}"
    );
    Ok(())
}

fn valid_module_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 64
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
}

fn validate_id(id: &str) -> Result<()> {
    ensure!(valid_id(id), "invalid kernel module id {id:?}");
    Ok(())
}

fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id
            .bytes()
            .next()
            .is_some_and(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
        && id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

fn sanitize_id(value: &str) -> String {
    let mut id: String = value
        .chars()
        .take(64)
        .map(|character| {
            if character.is_ascii_alphanumeric() || matches!(character, '.' | '_' | '-') {
                character
            } else {
                '-'
            }
        })
        .collect();
    if id.is_empty()
        || !id
            .bytes()
            .next()
            .is_some_and(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
    {
        id.insert_str(0, "lkm-");
    }
    id
}

fn is_protected(name: &str) -> bool {
    PROTECTED_NAMES.contains(&name)
}

fn validate_params(params: &str) -> Result<()> {
    ensure!(
        params.len() <= MAX_PARAM_BYTES,
        "module parameters exceed {MAX_PARAM_BYTES} bytes"
    );
    ensure!(!params.contains('\0'), "module parameters contain NUL");
    Ok(())
}

fn clean_text(value: &str, limit: usize) -> String {
    value
        .chars()
        .filter(|character| !character.is_control() || matches!(character, '\n' | '\t'))
        .take(limit)
        .collect()
}

fn bounded_message(error: &anyhow::Error) -> String {
    let mut message = format!("{error:#}");
    if message.len() > MAX_LOG_BYTES {
        let mut boundary = MAX_LOG_BYTES;
        while !message.is_char_boundary(boundary) {
            boundary -= 1;
        }
        message.truncate(boundary);
        message.push_str("...");
    }
    message
}

fn c_string_at<'a>(data: &'a [u8], offset: u32, context: &str) -> Result<&'a [u8]> {
    let start = usize::try_from(offset).context("string offset overflow")?;
    let tail = data
        .get(start..)
        .with_context(|| format!("{context} is outside section"))?;
    let end = tail
        .iter()
        .position(|byte| *byte == 0)
        .with_context(|| format!("{context} is not NUL terminated"))?;
    Ok(&tail[..end])
}

fn slice_range<'a>(data: &'a [u8], offset: usize, size: usize, context: &str) -> Result<&'a [u8]> {
    let end = offset
        .checked_add(size)
        .with_context(|| format!("{context} range overflow"))?;
    data.get(offset..end)
        .with_context(|| format!("{context} is outside the file"))
}

fn checked_usize(value: u64, context: &str) -> Result<usize> {
    usize::try_from(value).with_context(|| format!("{context} overflow"))
}

fn read_u16(data: &[u8], offset: usize, little: bool) -> Result<u16> {
    let bytes: [u8; 2] = data
        .get(offset..offset + 2)
        .context("ELF read is outside the file")?
        .try_into()?;
    Ok(if little {
        u16::from_le_bytes(bytes)
    } else {
        u16::from_be_bytes(bytes)
    })
}

fn read_u32(data: &[u8], offset: usize, little: bool) -> Result<u32> {
    let bytes: [u8; 4] = data
        .get(offset..offset + 4)
        .context("ELF read is outside the file")?
        .try_into()?;
    Ok(if little {
        u32::from_le_bytes(bytes)
    } else {
        u32::from_be_bytes(bytes)
    })
}

fn read_u64(data: &[u8], offset: usize, little: bool) -> Result<u64> {
    let bytes: [u8; 8] = data
        .get(offset..offset + 8)
        .context("ELF read is outside the file")?
        .try_into()?;
    Ok(if little {
        u64::from_le_bytes(bytes)
    } else {
        u64::from_be_bytes(bytes)
    })
}

fn read_regular(path: &Path, limit: usize) -> Result<Vec<u8>> {
    let metadata = symlink_file_metadata(path)?;
    ensure!(
        metadata.is_file(),
        "{} is not a regular file",
        path.display()
    );
    ensure!(
        metadata.len() <= limit as u64,
        "{} exceeds {} bytes",
        path.display(),
        limit
    );
    let mut file = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW)
        .open(path)
        .with_context(|| format!("open {}", path.display()))?;
    let mut data = Vec::with_capacity(metadata.len() as usize);
    file.read_to_end(&mut data)?;
    ensure!(
        data.len() <= limit,
        "{} changed while it was being read",
        path.display()
    );
    Ok(data)
}

fn symlink_file_metadata(path: &Path) -> Result<fs::Metadata> {
    let metadata =
        fs::symlink_metadata(path).with_context(|| format!("inspect {}", path.display()))?;
    ensure!(
        !metadata.file_type().is_symlink(),
        "{} is a symbolic link",
        path.display()
    );
    Ok(metadata)
}

fn create_private_dir(path: &Path) -> Result<()> {
    fs::create_dir_all(path).with_context(|| format!("create {}", path.display()))?;
    let metadata = symlink_file_metadata(path)?;
    ensure!(metadata.is_dir(), "{} is not a directory", path.display());
    fs::set_permissions(path, fs::Permissions::from_mode(0o700))
        .with_context(|| format!("set permissions on {}", path.display()))
}

fn create_private_parent(path: &Path) -> Result<()> {
    if let Some(parent) = path.parent() {
        create_private_dir(parent)?;
    }
    Ok(())
}

fn write_private(path: &Path, data: &[u8]) -> Result<()> {
    create_private_parent(path)?;
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW)
        .open(path)
        .with_context(|| format!("open {}", path.display()))?;
    file.write_all(data)?;
    file.sync_all()?;
    fs::set_permissions(path, fs::Permissions::from_mode(0o600))?;
    Ok(())
}

fn write_marker(path: &Path) -> Result<()> {
    write_private(path, b"")?;
    if let Some(parent) = path.parent() {
        sync_dir(parent)?;
    }
    Ok(())
}

fn write_json<T: Serialize>(path: &Path, value: &T) -> Result<()> {
    let bytes = serde_json::to_vec_pretty(value)?;
    write_private(path, &bytes)
}

fn write_json_bounded<T: Serialize>(path: &Path, value: &T, limit: usize) -> Result<()> {
    let bytes = serde_json::to_vec_pretty(value)?;
    ensure!(
        bytes.len() <= limit,
        "serialized config is {} bytes; maximum is {limit}",
        bytes.len()
    );
    write_private(path, &bytes)
}

fn write_json_atomic<T: Serialize>(path: &Path, value: &T) -> Result<()> {
    let parent = path.parent().context("JSON path has no parent")?;
    create_private_dir(parent)?;
    let temporary = parent.join(format!(
        ".{}.{}.tmp",
        path.file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("config"),
        std::process::id()
    ));
    write_json(&temporary, value)?;
    fs::rename(&temporary, path).with_context(|| format!("replace {}", path.display()))?;
    sync_dir(parent)
}

fn sync_dir(path: &Path) -> Result<()> {
    File::open(path)
        .and_then(|file| file.sync_all())
        .with_context(|| format!("sync {}", path.display()))
}

fn recover_rollback_entry<R: Runtime>(
    payload: &str,
    path: &Path,
    store: &KernelModuleStore<R>,
) -> Result<()> {
    let id = payload.rsplit_once("__").map_or_default(|(id, _)| id);
    if valid_id(id) && !store.entry_path(id).exists() {
        fs::rename(path, store.entry_path(id))?;
    } else {
        fs::remove_dir_all(path).ok();
    }
    Ok(())
}

#[cfg(target_os = "android")]
fn safe_mode() -> bool {
    crate::utils::is_safe_mode()
}

fn ensure_enable_policy(enabled: bool, in_safe_mode: bool) -> Result<()> {
    ensure!(
        !enabled || !in_safe_mode,
        "safe mode prohibits clearing the disable marker"
    );
    Ok(())
}

#[cfg(not(target_os = "android"))]
fn safe_mode() -> bool {
    false
}

#[cfg(target_os = "android")]
pub fn run(command: Command) -> Result<()> {
    let store = KernelModuleStore::managed();
    let result = (|| -> Result<String> {
        match command {
            Command::List => Ok(serde_json::to_string(&ListOutput {
                schema_version: SCHEMA_VERSION,
                safe_mode: safe_mode(),
                modules: store.list()?,
            })?),
            Command::Info { id } => {
                let (module, last_result) = store.info(&id)?;
                Ok(serde_json::to_string(&InfoOutput {
                    schema_version: SCHEMA_VERSION,
                    safe_mode: safe_mode(),
                    module,
                    last_result,
                })?)
            }
            Command::Install { path } => Ok(serde_json::to_string(&store.install(&path)?)?),
            Command::Remove { id } => Ok(serde_json::to_string(&store.remove(&id)?)?),
            Command::Enable { id } => Ok(serde_json::to_string(&store.set_enabled(&id, true)?)?),
            Command::Disable { id } => Ok(serde_json::to_string(&store.set_enabled(&id, false)?)?),
            Command::Autoload { id, state } => Ok(serde_json::to_string(
                &store.set_autoload(&id, matches!(state, AutoloadState::On))?,
            )?),
            Command::Params { id, params } => {
                Ok(serde_json::to_string(&store.set_params(&id, &params)?)?)
            }
            Command::Load { id } => Ok(serde_json::to_string(&store.load_manual(&id)?)?),
            Command::Log { id } => {
                let (module, last_result) = store.log(&id)?;
                Ok(serde_json::to_string(&InfoOutput {
                    schema_version: SCHEMA_VERSION,
                    safe_mode: safe_mode(),
                    module,
                    last_result,
                })?)
            }
        }
    })();
    match result {
        Ok(output) => {
            println!("{output}");
            Ok(())
        }
        Err(error) => {
            println!(
                "{}",
                serde_json::to_string(&ErrorOutput {
                    schema_version: SCHEMA_VERSION,
                    safe_mode: safe_mode(),
                    ok: false,
                    error: bounded_message(&error),
                })?
            );
            Err(error)
        }
    }
}

#[cfg(target_os = "android")]
pub fn disable_all_for_safe_mode() -> Result<()> {
    KernelModuleStore::managed().disable_all_for_safe_mode()
}

#[cfg(target_os = "android")]
pub fn autoload() -> Result<AutoloadReport> {
    KernelModuleStore::managed().autoload()
}

#[cfg(target_os = "android")]
pub fn autoload_with_startup_timeout() -> Result<Option<AutoloadReport>> {
    let (sender, receiver) = mpsc::sync_channel(1);
    thread::Builder::new()
        .name("kmod-autoload".to_owned())
        .spawn(move || {
            let _ = sender.send(autoload());
        })
        .context("spawn kernel module autoload worker")?;
    match receiver.recv_timeout(AUTOLOAD_STARTUP_WAIT) {
        Ok(result) => result.map(Some),
        Err(mpsc::RecvTimeoutError::Timeout) => Ok(None),
        Err(mpsc::RecvTimeoutError::Disconnected) => {
            bail!("kernel module autoload worker exited without a result")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;
    use std::sync::{Arc, Condvar, Mutex};
    use std::thread;

    #[derive(Clone)]
    struct FakeRuntime {
        loaded_names: Rc<RefCell<HashSet<String>>>,
        load_calls: Rc<RefCell<Vec<(String, String)>>>,
        fail_names: Rc<RefCell<HashSet<String>>>,
    }

    impl FakeRuntime {
        fn new(loaded: &[&str]) -> Self {
            Self {
                loaded_names: Rc::new(RefCell::new(
                    loaded.iter().map(ToString::to_string).collect(),
                )),
                load_calls: Rc::new(RefCell::new(Vec::new())),
                fail_names: Rc::new(RefCell::new(HashSet::new())),
            }
        }
    }

    impl Runtime for FakeRuntime {
        fn boot_id(&self) -> Result<String> {
            Ok("test-boot".to_owned())
        }

        fn loaded(&self, name: &str) -> Result<bool> {
            Ok(self.loaded_names.borrow().contains(name))
        }

        fn load(&self, path: &Path, params: &str) -> Result<()> {
            let name = parse_module(&fs::read(path)?, path)?
                .internal_name
                .context("fixture has no name")?;
            if self.fail_names.borrow().contains(&name) {
                bail!("injected failure for {name}");
            }
            self.load_calls
                .borrow_mut()
                .push((name.clone(), params.to_owned()));
            self.loaded_names.borrow_mut().insert(name);
            Ok(())
        }
    }

    #[derive(Clone)]
    struct BlockingRuntime {
        started: Arc<(Mutex<bool>, Condvar)>,
        release: Arc<(Mutex<bool>, Condvar)>,
        loaded_names: Arc<Mutex<HashSet<String>>>,
    }

    impl BlockingRuntime {
        fn new() -> Self {
            Self {
                started: Arc::new((Mutex::new(false), Condvar::new())),
                release: Arc::new((Mutex::new(false), Condvar::new())),
                loaded_names: Arc::new(Mutex::new(HashSet::new())),
            }
        }

        fn wait_started(&self) {
            let (lock, condvar) = &*self.started;
            let started = lock.lock().unwrap();
            let (started, timeout) = condvar
                .wait_timeout_while(started, Duration::from_secs(2), |started| !*started)
                .unwrap();
            assert!(*started, "load did not start before timeout: {timeout:?}");
        }

        fn release(&self) {
            let (lock, condvar) = &*self.release;
            *lock.lock().unwrap() = true;
            condvar.notify_all();
        }
    }

    impl Runtime for BlockingRuntime {
        fn boot_id(&self) -> Result<String> {
            Ok("test-boot".to_owned())
        }

        fn loaded(&self, name: &str) -> Result<bool> {
            Ok(self.loaded_names.lock().unwrap().contains(name))
        }

        fn load(&self, path: &Path, _params: &str) -> Result<()> {
            let name = parse_module(&fs::read(path)?, path)?
                .internal_name
                .context("fixture has no name")?;
            let (lock, condvar) = &*self.started;
            *lock.lock().unwrap() = true;
            condvar.notify_all();

            let (lock, condvar) = &*self.release;
            let released = lock.lock().unwrap();
            let _released = condvar.wait_while(released, |released| !*released).unwrap();

            self.loaded_names.lock().unwrap().insert(name);
            Ok(())
        }
    }

    fn fixture(entries: &[(&str, &str)]) -> Vec<u8> {
        let mut modinfo = Vec::new();
        for (key, value) in entries {
            modinfo.extend_from_slice(key.as_bytes());
            modinfo.push(b'=');
            modinfo.extend_from_slice(value.as_bytes());
            modinfo.push(0);
        }
        let modinfo_offset = 64;
        let shstr = b"\0.modinfo\0.shstrtab\0";
        let shstr_offset = modinfo_offset + modinfo.len();
        let section_offset = (shstr_offset + shstr.len() + 7) & !7;
        let mut data = vec![0_u8; section_offset + 3 * 64];
        data[..4].copy_from_slice(b"\x7fELF");
        data[4] = 2;
        data[5] = 1;
        data[6] = 1;
        put_u16(&mut data, 16, 1);
        put_u16(&mut data, 18, 183);
        put_u32(&mut data, 20, 1);
        put_u64(&mut data, 40, section_offset as u64);
        put_u16(&mut data, 52, 64);
        put_u16(&mut data, 58, 64);
        put_u16(&mut data, 60, 3);
        put_u16(&mut data, 62, 2);
        data[modinfo_offset..modinfo_offset + modinfo.len()].copy_from_slice(&modinfo);
        data[shstr_offset..shstr_offset + shstr.len()].copy_from_slice(shstr);

        put_u32(&mut data, section_offset + 64, 1);
        put_u32(&mut data, section_offset + 68, 1);
        put_u64(&mut data, section_offset + 64 + 24, modinfo_offset as u64);
        put_u64(&mut data, section_offset + 64 + 32, modinfo.len() as u64);
        put_u32(&mut data, section_offset + 128, 10);
        put_u32(&mut data, section_offset + 132, 3);
        put_u64(&mut data, section_offset + 128 + 24, shstr_offset as u64);
        put_u64(&mut data, section_offset + 128 + 32, shstr.len() as u64);
        data
    }

    fn put_u16(data: &mut [u8], offset: usize, value: u16) {
        data[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn put_u32(data: &mut [u8], offset: usize, value: u32) {
        data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn put_u64(data: &mut [u8], offset: usize, value: u64) {
        data[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }

    fn fixture_entries(name: &str, version: &str) -> Vec<(&'static str, &'static str)> {
        let name: &'static str = Box::leak(name.to_owned().into_boxed_str());
        let version: &'static str = Box::leak(version.to_owned().into_boxed_str());
        vec![
            ("name", name),
            ("version", version),
            ("author", "First"),
            ("author", "Second"),
            ("description", "Fixture module"),
            ("license", "GPL"),
            ("vermagic", "test"),
            ("depends", "dep, external"),
            ("parm", "mode:load mode"),
            ("parmtype", "mode:int"),
        ]
    }

    fn install_fixture<R: Runtime>(
        store: &KernelModuleStore<R>,
        directory: &Path,
        stem: &str,
        name: &str,
        version: &str,
    ) -> Result<ActionOutput> {
        let path = directory.join(format!("{stem}.ko"));
        fs::write(&path, fixture(&fixture_entries(name, version)))?;
        store.install(&path)
    }

    #[test]
    fn parses_metadata_without_losing_duplicates() {
        let path = Path::new("fixture.ko");
        let parsed = parse_module(&fixture(&fixture_entries("demo", "1.2.3")), path).unwrap();
        assert_eq!(parsed.internal_name.as_deref(), Some("demo"));
        assert_eq!(parsed.version, "1.2.3");
        assert_eq!(parsed.authors, vec!["First", "Second"]);
        assert_eq!(parsed.depends, vec!["dep", "external"]);
        assert_eq!(parsed.parameters.len(), 1);
        assert_eq!(parsed.parameters[0].name, "mode");
        assert_eq!(parsed.parameters[0].value_type.as_deref(), Some("int"));
        assert_eq!(parsed.architecture, "aarch64");
    }

    #[test]
    fn rejects_truncated_elf() {
        let error = parse_module(b"\x7fELF\x02\x01", Path::new("bad.ko")).unwrap_err();
        assert!(format!("{error:#}").contains("truncated"));
    }

    #[test]
    fn missing_name_is_diagnostic_not_a_fake_runtime_identity() {
        let parsed = parse_module(
            &fixture(&[("license", "GPL"), ("vermagic", "test")]),
            Path::new("display-fallback.ko"),
        )
        .unwrap();
        assert!(parsed.internal_name.is_none());
        assert_eq!(parsed.display_name, "display-fallback");
        assert!(!parsed.diagnostics.is_empty());
    }

    #[test]
    fn rejects_unsupported_machine() {
        let mut data = fixture(&[("name", "demo"), ("license", "GPL")]);
        put_u16(&mut data, 18, 999);
        let error = parse_module(&data, Path::new("bad-arch.ko")).unwrap_err();
        assert!(format!("{error:#}").contains("unsupported ELF machine"));
    }

    #[test]
    fn install_update_preserves_intent_and_custom_id_identity() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), FakeRuntime::new(&[]));
        let first = install_fixture(&store, source.path(), "first", "demo", "1").unwrap();
        assert_eq!(first.id, "demo");
        store.set_autoload("demo", true).unwrap();
        store.set_enabled("demo", false).unwrap();
        fs::write(temp.path().join("lkm/demo").join(BANNER_FILE), b"banner").unwrap();
        store
            .write_result(
                "demo",
                &LastResult {
                    boot_id: "test-boot".to_owned(),
                    operation: "load".to_owned(),
                    outcome: "error".to_owned(),
                    message: "preserve me".to_owned(),
                },
            )
            .unwrap();

        let second = install_fixture(&store, source.path(), "renamed", "demo", "2").unwrap();
        assert_eq!(second.id, "demo");
        let second_module = second.module.unwrap();
        assert_eq!(second_module.version, "2");
        assert!(second_module.autoload);
        assert!(second_module.disabled);
        assert!(!second_module.effective_autoload);
        assert!(temp.path().join("lkm/demo").join(BANNER_FILE).is_file());
        assert_eq!(store.log("demo").unwrap().1.unwrap().message, "preserve me");
    }

    #[test]
    fn failed_update_keeps_the_previous_entry() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), FakeRuntime::new(&[]));
        install_fixture(&store, source.path(), "demo", "demo", "1").unwrap();
        let bad = source.path().join("demo-invalid.ko");
        fs::write(&bad, b"not elf").unwrap();
        assert!(store.install(&bad).is_err());
        let modules = store.list().unwrap();
        assert_eq!(modules.len(), 1);
        assert_eq!(modules[0].version, "1");
    }

    #[test]
    fn rejects_symlink_and_path_traversal() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), FakeRuntime::new(&[]));
        let module = source.path().join("target.ko");
        fs::write(&module, fixture(&fixture_entries("demo", "1"))).unwrap();
        let link = source.path().join("link.ko");
        std::os::unix::fs::symlink(&module, &link).unwrap();
        assert!(store.install(&link).is_err());
        assert!(store.remove("../escape").is_err());
    }

    #[test]
    fn manual_load_records_result() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime::new(&[]);
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), runtime.clone());
        let path = source.path().join("demo.ko");
        fs::write(
            &path,
            fixture(&[
                ("name", "demo"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", ""),
            ]),
        )
        .unwrap();
        store.install(&path).unwrap();
        store.set_params("demo", "mode=1").unwrap();
        let output = store.load_manual("demo").unwrap();
        assert_eq!(output.module.unwrap().loaded, LoadState::Loaded);
        assert_eq!(
            runtime.load_calls.borrow().as_slice(),
            &[("demo".to_owned(), "mode=1".to_owned())]
        );
        let (_, result) = store.log("demo").unwrap();
        assert_eq!(result.unwrap().outcome, "success");
    }

    #[test]
    fn autoload_orders_managed_dependencies_and_runs_once_per_boot() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime::new(&[]);
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), runtime.clone());
        let dep = source.path().join("dep.ko");
        fs::write(
            &dep,
            fixture(&[
                ("name", "dep"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", ""),
            ]),
        )
        .unwrap();
        store.install(&dep).unwrap();
        let target = source.path().join("target.ko");
        fs::write(
            &target,
            fixture(&[
                ("name", "target"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", "dep"),
            ]),
        )
        .unwrap();
        store.install(&target).unwrap();
        store.set_autoload("dep", true).unwrap();
        store.set_autoload("target", true).unwrap();

        let first = store.autoload().unwrap();
        assert_eq!(first.loaded, vec!["dep", "target"]);
        assert_eq!(
            runtime
                .load_calls
                .borrow()
                .iter()
                .map(|(name, _)| name.as_str())
                .collect::<Vec<_>>(),
            vec!["dep", "target"]
        );
        let second = store.autoload().unwrap();
        assert!(second.attempted.is_empty());
        assert_eq!(second.skipped, vec!["dep", "target"]);
    }

    #[test]
    fn safe_mode_disables_every_entry_without_parsing_config() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), FakeRuntime::new(&[]));
        install_fixture(&store, source.path(), "demo", "demo", "1").unwrap();
        let broken = temp.path().join("lkm").join("broken");
        fs::create_dir_all(&broken).unwrap();
        fs::write(broken.join(CONFIG_FILE), b"{not json").unwrap();

        store.disable_all_for_safe_mode().unwrap();
        assert!(temp.path().join("lkm/demo").join(DISABLE_FILE).is_file());
        assert!(broken.join(DISABLE_FILE).is_file());
    }

    #[test]
    fn safe_mode_allows_blocking_but_rejects_clearing_the_block() {
        assert!(ensure_enable_policy(false, true).is_ok());
        assert!(ensure_enable_policy(false, false).is_ok());
        assert!(ensure_enable_policy(true, false).is_ok());
        assert!(ensure_enable_policy(true, true).is_err());
    }

    #[test]
    fn failed_dependency_does_not_block_unrelated_module() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime::new(&[]);
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), runtime.clone());
        let broken = source.path().join("broken.ko");
        fs::write(
            &broken,
            fixture(&[
                ("name", "broken"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", "missing"),
            ]),
        )
        .unwrap();
        store.install(&broken).unwrap();
        store.set_autoload("broken", true).unwrap();
        let healthy = source.path().join("healthy.ko");
        fs::write(
            &healthy,
            fixture(&[
                ("name", "healthy"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", ""),
            ]),
        )
        .unwrap();
        store.install(&healthy).unwrap();
        store.set_autoload("healthy", true).unwrap();

        let report = store.autoload().unwrap();
        assert!(!report.failures.is_empty());
        assert_eq!(report.loaded, vec!["healthy"]);
        let (_, result) = store.log("broken").unwrap();
        assert_eq!(result.unwrap().outcome, "error");
        let second = store.autoload().unwrap();
        assert!(second.attempted.is_empty());
    }

    #[test]
    fn dependency_cycle_is_reported_without_loading() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime::new(&[]);
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), runtime.clone());
        for (name, dependency) in [("first", "second"), ("second", "first")] {
            let path = source.path().join(format!("{name}.ko"));
            fs::write(
                &path,
                fixture(&[
                    ("name", name),
                    ("license", "GPL"),
                    ("vermagic", "test"),
                    ("depends", dependency),
                ]),
            )
            .unwrap();
            store.install(&path).unwrap();
            store.set_autoload(name, true).unwrap();
        }
        let report = store.autoload().unwrap();
        assert!(report.loaded.is_empty());
        assert!(!report.failures.is_empty());
        assert!(runtime.load_calls.borrow().is_empty());
    }

    #[test]
    fn interrupted_or_corrupt_records_fail_closed() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime::new(&[]);
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), runtime.clone());
        let path = source.path().join("demo.ko");
        fs::write(
            &path,
            fixture(&[
                ("name", "demo"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", ""),
            ]),
        )
        .unwrap();
        store.install(&path).unwrap();
        store.set_autoload("demo", true).unwrap();
        store
            .write_result(
                "demo",
                &LastResult {
                    boot_id: "older-boot".to_owned(),
                    operation: "autoload".to_owned(),
                    outcome: "inflight".to_owned(),
                    message: String::new(),
                },
            )
            .unwrap();

        let interrupted = store.autoload().unwrap();
        assert_eq!(interrupted.skipped, vec!["demo"]);
        assert!(runtime.load_calls.borrow().is_empty());
        assert_eq!(store.log("demo").unwrap().1.unwrap().outcome, "interrupted");

        let still_interrupted = store.autoload().unwrap();
        assert_eq!(still_interrupted.skipped, vec!["demo"]);
        assert!(runtime.load_calls.borrow().is_empty());

        fs::write(
            temp.path().join("lkm/demo").join(LAST_RESULT_FILE),
            b"{broken",
        )
        .unwrap();
        let corrupt = store.autoload().unwrap();
        assert_eq!(corrupt.failures.len(), 1);
        assert!(runtime.load_calls.borrow().is_empty());
    }

    #[test]
    fn rejects_configs_larger_than_the_read_limit() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join(CONFIG_FILE);
        let value = "x".repeat(MAX_CONFIG_BYTES);
        let error = write_json_bounded(&path, &value, MAX_CONFIG_BYTES).unwrap_err();
        assert!(format!("{error:#}").contains("maximum"));
        assert!(!path.exists());
    }

    #[test]
    fn update_and_remove_rollback_namespaces_do_not_collide() {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path().join("lkm");
        let store = KernelModuleStore::with_runtime(root.clone(), FakeRuntime::new(&[]));
        let rollback = root.join(ROLLBACK_DIR);
        let update = rollback.join("update-removed-demo__1");
        let removal = rollback.join("removed-removed-demo__2");
        fs::create_dir_all(&update).unwrap();
        fs::create_dir_all(&removal).unwrap();
        fs::write(update.join("module.ko"), b"kept").unwrap();

        store.recover().unwrap();

        assert!(root.join("removed-demo/module.ko").is_file());
        assert!(!removal.exists());
    }

    #[test]
    fn corrupt_entries_can_still_be_removed() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let store = KernelModuleStore::with_runtime(temp.path().join("lkm"), FakeRuntime::new(&[]));
        install_fixture(&store, source.path(), "demo", "demo", "1").unwrap();
        fs::write(temp.path().join("lkm/demo").join(CONFIG_FILE), b"{broken").unwrap();

        let output = store.remove("demo").unwrap();

        assert!(output.module.is_none());
        assert!(!temp.path().join("lkm/demo").exists());
    }

    #[test]
    fn lock_wait_is_bounded() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("lock");
        let _held = FileLock::acquire(&path).unwrap();
        let started = Instant::now();
        let error = match FileLock::acquire(&path) {
            Ok(_) => panic!("second lock unexpectedly succeeded"),
            Err(error) => error,
        };
        assert!(format!("{error:#}").contains("lock busy"));
        assert!(started.elapsed() < Duration::from_secs(4));
    }

    #[test]
    fn blocking_autoload_does_not_hold_the_entry_lock() {
        let temp = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let runtime = BlockingRuntime::new();
        let store = Arc::new(KernelModuleStore::with_runtime(
            temp.path().join("lkm"),
            runtime.clone(),
        ));
        let module = source.path().join("demo.ko");
        fs::write(
            &module,
            fixture(&[
                ("name", "demo"),
                ("license", "GPL"),
                ("vermagic", "test"),
                ("depends", ""),
            ]),
        )
        .unwrap();
        store.install(&module).unwrap();
        store.set_autoload("demo", true).unwrap();

        let autoload_store = Arc::clone(&store);
        let worker = thread::spawn(move || autoload_store.autoload());
        runtime.wait_started();

        let started = Instant::now();
        store.set_enabled("demo", false).unwrap();
        assert!(started.elapsed() < Duration::from_secs(3));

        runtime.release();
        let report = worker.join().unwrap().unwrap();
        assert_eq!(report.loaded, vec!["demo"]);
        assert!(store.read_entry("demo").unwrap().disabled);
    }
}
