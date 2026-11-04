#![deny(clippy::all, clippy::pedantic)]
#![warn(clippy::nursery)]
#![allow(
    clippy::module_name_repetitions,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    clippy::cast_precision_loss,
    clippy::doc_markdown,
    clippy::too_many_lines,
    clippy::cast_possible_wrap,
    clippy::redundant_field_names
)]

#[cfg(any(target_os = "android", test))]
mod anykernel3;
mod apk_sign;
mod assets;
mod boot_patch;
#[cfg(target_os = "android")]
mod cli;
#[cfg(not(target_os = "android"))]
mod cli_non_android;
#[cfg(target_os = "android")]
mod debug;
mod defs;
#[cfg(target_os = "android")]
mod dynamic_manager;
#[cfg(target_os = "android")]
mod feature;
mod feature_id;
#[cfg(target_os = "android")]
mod hot_install;
#[cfg(target_os = "android")]
mod init_event;
#[cfg(any(target_os = "android", test))]
mod kernel_module;
#[cfg(target_os = "android")]
mod ksucalls;
#[cfg(target_os = "android")]
mod late_load;
mod lkm_image;
mod lkm_image_btf;
#[cfg(target_os = "android")]
mod lua_probe;
#[cfg(target_os = "android")]
mod magica;
#[cfg(target_os = "android")]
mod metamodule;
#[cfg(target_os = "android")]
mod module;
#[cfg(target_os = "android")]
mod module_config;
#[cfg(target_os = "android")]
mod module_mount;
#[cfg(target_os = "android")]
mod module_loader;
#[cfg(any(target_os = "android", test))]
#[cfg_attr(not(target_os = "android"), allow(dead_code))]
mod module_markers;
#[cfg(target_os = "android")]
mod pif;
#[cfg(any(target_os = "android", test))]
mod plugin;
#[cfg(any(target_os = "android", test))]
#[cfg_attr(not(target_os = "android"), allow(dead_code))]
mod preinit;
#[cfg(target_os = "android")]
mod profile;
#[cfg(target_os = "android")]
mod resetprop;
#[cfg(target_os = "android")]
mod restorecon;
#[cfg(target_os = "android")]
mod sepolicy;
#[cfg(target_os = "android")]
mod soft_reboot;
#[cfg(any(target_os = "android", test))]
mod startup_epoch;
#[cfg(target_os = "android")]
mod su;
#[cfg(target_os = "android")]
mod sulog;
#[cfg(any(target_os = "android", test))]
#[cfg_attr(not(target_os = "android"), allow(dead_code))]
mod yukizygisk;
// SUSFS is an Android control interface; Linux hosts only compile it for tests.
#[cfg(target_os = "android")]
#[cfg(any(target_os = "android", test))]
#[cfg(target_os = "android")]
#[allow(nonstandard_style, unused, unsafe_op_in_unsafe_fn)]
mod ksu_uapi;
#[cfg(any(target_os = "android", all(test, target_os = "linux")))]
mod susfs;
#[cfg(target_os = "android")]
mod unload;
#[cfg(target_os = "android")]
mod utils;

fn main() -> anyhow::Result<()> {
    #[cfg(target_os = "android")]
    {
        cli::run()
    }
    #[cfg(not(target_os = "android"))]
    {
        cli_non_android::run()
    }
}
