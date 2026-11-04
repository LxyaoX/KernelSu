//! Writable feature IDs shared by Android feature management and host boot patching.
#![cfg_attr(not(target_os = "android"), allow(dead_code))]

use anyhow::Result;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum FeatureId {
    SuCompat = 0,
    KernelUmount = 1,
    Sulog = 2,
    AdbRoot = 3,
    SelinuxHide = 4,
    // IDs 5 and 8 belonged to the removed dynamic-manager feature.
    AvcSpoof = 6,
    ShellSuAdbOnly = 7,
    MountViewFilter = 10,
    /// YukiZygisk's injection runtime. Not a user toggle, so it stays out of `ALL`.
    YukiZygisk = 11,
}

impl FeatureId {
    pub const ALL: [Self; 8] = [
        Self::SuCompat,
        Self::KernelUmount,
        Self::Sulog,
        Self::AdbRoot,
        Self::SelinuxHide,
        Self::AvcSpoof,
        Self::ShellSuAdbOnly,
        Self::MountViewFilter,
    ];

    pub const fn from_u32(id: u32) -> Option<Self> {
        match id {
            0 => Some(Self::SuCompat),
            1 => Some(Self::KernelUmount),
            2 => Some(Self::Sulog),
            3 => Some(Self::AdbRoot),
            4 => Some(Self::SelinuxHide),
            6 => Some(Self::AvcSpoof),
            7 => Some(Self::ShellSuAdbOnly),
            10 => Some(Self::MountViewFilter),
            11 => Some(Self::YukiZygisk),
            _ => None,
        }
    }

    pub const fn name(self) -> &'static str {
        match self {
            Self::SuCompat => "su_compat",
            Self::KernelUmount => "kernel_umount",
            Self::Sulog => "sulog",
            Self::AdbRoot => "adb_root",
            Self::SelinuxHide => "selinux_hide",
            Self::AvcSpoof => "avc_spoof",
            Self::ShellSuAdbOnly => "shell_su_adb_only",
            Self::MountViewFilter => "mount_view_filter",
            Self::YukiZygisk => "yukizygisk",
        }
    }

    pub const fn description(self) -> &'static str {
        match self {
            Self::SuCompat => {
                "SU Compatibility Mode - allows authorized apps to gain root via traditional 'su' command"
            }
            Self::KernelUmount => {
                "Kernel Umount - controls whether kernel automatically unmounts modules when not needed"
            }
            Self::Sulog => {
                "SU Log - streams kernel sulog events to userspace and persists them to disk"
            }
            Self::AdbRoot => "ADB Root - Enable adbd root",
            Self::SelinuxHide => {
                "SELinux Hide - sanitize /sys/fs/selinux access results for app UIDs"
            }
            Self::AvcSpoof => {
                "AVC log spoofing - mask the KSU target context in SELinux audit logs"
            }
            Self::ShellSuAdbOnly => {
                "Shell SU ADB Only - when shell is granted root, additionally require the process to be spawned by adbd or a KernelSU root process (ksu domain)"
            }
            Self::YukiZygisk => {
                "YukiZygisk - kernel-assisted Zygisk injection runtime (not a user toggle)"
            }
            Self::MountViewFilter => {
                "Mount View Filter - hide root and module mounts from app and isolated-process proc mount views"
            }
        }
    }
}

pub fn parse_feature_id(name: &str) -> Result<FeatureId> {
    FeatureId::ALL
        .into_iter()
        .find(|id| id.name() == name || (*id as u32).to_string() == name)
        .ok_or_else(|| anyhow::anyhow!("Unknown feature: {name}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writable_ids_match_kernel_contract() {
        for id in FeatureId::ALL {
            assert_eq!(parse_feature_id(id.name()).unwrap(), id);
            assert_eq!(parse_feature_id(&(id as u32).to_string()).unwrap(), id);
        }
        assert_eq!(FeatureId::AvcSpoof as u32, 6);
        assert_eq!(FeatureId::ShellSuAdbOnly as u32, 7);
        assert_eq!(FeatureId::MountViewFilter as u32, 10);
        assert_eq!(FeatureId::from_u32(6), Some(FeatureId::AvcSpoof));
        assert_eq!(FeatureId::from_u32(7), Some(FeatureId::ShellSuAdbOnly));
        assert_eq!(FeatureId::from_u32(10), Some(FeatureId::MountViewFilter));
        assert!(!FeatureId::KernelUmount.has_dedicated_manager());
        assert!(parse_feature_id("5").is_err());
        assert!(parse_feature_id("unknown_feature").is_err());
    }
}
