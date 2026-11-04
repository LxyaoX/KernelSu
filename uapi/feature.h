#ifndef __KSU_UAPI_FEATURE_H
#define __KSU_UAPI_FEATURE_H

enum ksu_feature_id {
    KSU_FEATURE_SU_COMPAT = 0,
    KSU_FEATURE_KERNEL_UMOUNT = 1,
    KSU_FEATURE_SULOG = 2,
    KSU_FEATURE_ADB_ROOT = 3,
    KSU_FEATURE_SELINUX_HIDE = 4,
    // 5 and 8 belonged to the removed dynamic-manager feature; the gaps stay so a
    // persisted feature value keeps its meaning.
    KSU_FEATURE_AVC_SPOOF = 6,
    KSU_FEATURE_SHELL_SU_ADB_ONLY = 7,
    KSU_FEATURE_MOUNT_VIEW_FILTER = 10,
    /* Additive: YukiZygisk injection runtime. */
    KSU_FEATURE_YUKIZYGISK = 11,

    KSU_FEATURE_MAX
};

#endif
