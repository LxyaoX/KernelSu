#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/ratelimit.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/thread_info.h>
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs.h>
#endif
#include "uapi/supercall.h"
#include "supercall/internal.h"
#include "arch.h" // IWYU pragma: keep
#include "policy/allowlist.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "runtime/ksud_boot.h"
#include "feature/kernel_umount.h"
#include "manager/manager_identity.h"
#include "manager/throne_tracker.h"
#include "selinux/selinux.h"
#include "infra/file_wrapper.h"
#include "hook/tp_marker.h"
#include "policy/app_profile.h"
#include "sulog/event.h"
#include "sulog/fd.h"
#include "supercall/supercall.h"
#include "infra/symbol_resolver.h"
#include "preinit/preinit.h"
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/pid.h>
#include <linux/ptrace.h>
#include <linux/sched/task.h>
#include "infra/task_work.h"
#include "uapi/yukizygisk.h"
#include "feature/yukizygisk/api.h"
#include "feature/yukizygisk/patch_transaction.h"

static int do_grant_root(void __user *arg)
{
    int ret;
    __u32 audit_uid = current_uid().val;
    __u32 audit_euid = current_euid().val;

    // we already check uid above on allowed_for_su()

    pr_info("allow root for: %d\n", audit_uid);
    ret = escape_with_root_profile();
    ksu_sulog_emit_grant_root(ret, audit_uid, audit_euid, GFP_KERNEL);

    return ret;
}

static int do_get_info(void __user *arg)
{
    struct ksu_get_info_cmd cmd = { .version = KERNEL_SU_VERSION, .flags = 0 };

#ifdef MODULE
    cmd.flags |= KSU_GET_INFO_FLAG_LKM;
    if (ksu_bundled) {
        cmd.flags |= KSU_GET_INFO_FLAG_BUNDLED;
    }
#endif

    if (is_manager()) {
        cmd.flags |= KSU_GET_INFO_FLAG_MANAGER;
    }
    if (ksu_late_loaded) {
        cmd.flags |= KSU_GET_INFO_FLAG_LATE_LOAD;
    }
    if (ksu_preinit_enabled_param() && !ksu_late_loaded)
        cmd.flags |= KSU_GET_INFO_FLAG_PREINIT;
#if defined(EXPECTED_SIZE2) && !defined(SECOND_SIGNATURE_RELEASE)
    cmd.flags |= KSU_GET_INFO_FLAG_PR_BUILD;
#endif
    cmd.features = KSU_FEATURE_MAX;
    cmd.uapi_version = KERNEL_SU_UAPI_VERSION;

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("get_version: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_get_info_legacy(void __user *arg)
{
    struct ksu_get_info_legacy_cmd cmd = { .version = KERNEL_SU_VERSION, .flags = 0 };

#ifdef MODULE
    cmd.flags |= KSU_GET_INFO_FLAG_LKM;
    if (ksu_bundled) {
        cmd.flags |= KSU_GET_INFO_FLAG_BUNDLED;
    }
#endif

    if (is_manager()) {
        cmd.flags |= KSU_GET_INFO_FLAG_MANAGER;
    }
    if (ksu_late_loaded) {
        cmd.flags |= KSU_GET_INFO_FLAG_LATE_LOAD;
    }
#if defined(EXPECTED_SIZE2) && !defined(SECOND_SIGNATURE_RELEASE)
    cmd.flags |= KSU_GET_INFO_FLAG_PR_BUILD;
#endif
    cmd.features = KSU_FEATURE_MAX;

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("get_version: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_get_full_version(void __user *arg)
{
    struct ksu_get_full_version_cmd cmd = { 0 };

    strscpy(cmd.version_full, KSU_VERSION_FULL, sizeof(cmd.version_full));

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("get_full_version: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_report_event(void __user *arg)
{
    static bool services_started = false;
    struct ksu_report_event_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        return -EFAULT;
    }

    switch (cmd.event) {
    case EVENT_POST_FS_DATA: {
        static bool post_fs_data_lock = false;

        // Reset for emulated soft reboot
        services_started = false;
        if (!post_fs_data_lock) {
            post_fs_data_lock = true;
            if (ksu_late_loaded) {
                pr_info("post-fs-data skipped (late load)\n");
            } else {
                pr_info("post-fs-data triggered\n");
                on_post_fs_data();
            }
        }
        break;
    }
    case EVENT_BOOT_COMPLETED: {
        static bool boot_complete_lock = false;
        if (!boot_complete_lock) {
            boot_complete_lock = true;
            if (ksu_late_loaded) {
                pr_info("boot_complete skipped (late load)\n");
            } else {
                pr_info("boot_complete triggered\n");
                on_boot_completed();
#ifdef CONFIG_KSU_SUSFS
                susfs_start_sdcard_monitor_fn();
#endif
            }
        }
        break;
    }
    case EVENT_MODULE_MOUNTED: {
        pr_info("module mounted!\n");
        on_module_mounted();
        break;
    }
    case EVENT_SERVICES: {
        if (services_started) {
            pr_info("services already started, skipping\n");
            return 0;
        }
        services_started = true;
        pr_info("services triggered\n");
        return 1;
    }
    default:
        break;
    }

    return 0;
}

static int do_set_sepolicy(void __user *arg)
{
    struct ksu_set_sepolicy_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        return -EFAULT;
    }

    return handle_sepolicy((void __user *)cmd.data, cmd.data_len);
}

static int do_check_safemode(void __user *arg)
{
    struct ksu_check_safemode_cmd cmd;

    cmd.in_safe_mode = ksu_is_safe_mode();

    if (cmd.in_safe_mode) {
        pr_warn("safemode enabled!\n");
    }

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("check_safemode: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_new_get_allow_list_common(void __user *arg, bool allow)
{
    struct ksu_new_get_allow_list_cmd cmd;
    int *arr = NULL;
    int err = 0;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        return -EFAULT;
    }

    if (cmd.count) {
        arr = kmalloc(sizeof(int) * cmd.count, GFP_KERNEL);
        if (!arr) {
            return -ENOMEM;
        }
    }

    bool success = ksu_get_allow_list(arr, cmd.count, &cmd.count, &cmd.total_count, allow);

    if (!success) {
        err = -EFAULT;
        goto out;
    }

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("new_get_allow_list: copy_to_user count failed\n");
        err = -EFAULT;
        goto out;
    }

    if (cmd.count && copy_to_user(&((struct ksu_new_get_allow_list_cmd *)arg)->uids, arr, sizeof(int) * cmd.count)) {
        pr_err("new_get_allow_list: copy_to_user uids failed\n");
        err = -EFAULT;
    }

out:
    if (arr) {
        kfree(arr);
    }
    return err;
}

static int do_new_get_deny_list(void __user *arg)
{
    return do_new_get_allow_list_common(arg, false);
}

static int do_new_get_allow_list(void __user *arg)
{
    return do_new_get_allow_list_common(arg, true);
}

static int do_get_allow_list_common(void __user *arg, bool allow)
{
    int *arr = NULL;
    int err = 0;
    u16 count;
    u32 out_count;
    static const u16 kSize = 128;

    arr = kmalloc(sizeof(int) * kSize, GFP_KERNEL);
    if (!arr) {
        return -ENOMEM;
    }

    bool success = ksu_get_allow_list(arr, kSize, &count, NULL, allow);

    if (!success) {
        err = -EFAULT;
        goto out;
    }

    out_count = count;

    if (copy_to_user(arg + offsetof(struct ksu_get_allow_list_cmd, count), &out_count, sizeof(u32))) {
        pr_err("get_allow_list: copy_to_user count failed\n");
        err = -EFAULT;
        goto out;
    }

    if (copy_to_user(arg, arr, sizeof(u32) * count)) {
        pr_err("get_allow_list: copy_to_user uids failed\n");
        err = -EFAULT;
    }

out:
    if (arr) {
        kfree(arr);
    }
    return err;
}

static int do_get_deny_list(void __user *arg)
{
    return do_get_allow_list_common(arg, false);
}

static int do_get_allow_list(void __user *arg)
{
    return do_get_allow_list_common(arg, true);
}

static int do_uid_granted_root(void __user *arg)
{
    struct ksu_uid_granted_root_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        return -EFAULT;
    }

    cmd.granted = ksu_is_uid_granted_root(cmd.uid);

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("uid_granted_root: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_uid_should_umount(void __user *arg)
{
    struct ksu_uid_should_umount_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        return -EFAULT;
    }

    cmd.should_umount = ksu_uid_should_umount(cmd.uid);

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("uid_should_umount: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_get_manager_appid(void __user *arg)
{
    struct ksu_get_manager_appid_cmd cmd;

    cmd.appid = ksu_get_manager_appid();

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("get_manager_appid: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_get_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
    return -EOPNOTSUPP;
#endif
    uid_t uid;
    struct app_profile *profile;
    int ret = 0;

    if (copy_from_user(&uid, (char __user *)arg + offsetof(struct ksu_get_app_profile_cmd, profile.curr_uid),
                       sizeof(uid_t))) {
        pr_err("get_app_profile: copy_from_user failed\n");
        return -EFAULT;
    }

    rcu_read_lock();
    profile = ksu_get_app_profile(uid);
    rcu_read_unlock();
    if (!profile) {
        ret = -ENOENT;
    } else {
        if (copy_to_user((char __user *)arg + offsetof(struct ksu_get_app_profile_cmd, profile), profile,
                         sizeof(struct app_profile))) {
            pr_err("get_app_profile: copy_to_user failed\n");
            ret = -EFAULT;
        }
        ksu_put_app_profile(profile);
    }
    return ret;
}

static int do_set_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
    return -EOPNOTSUPP;
#endif

    struct ksu_set_app_profile_cmd cmd;
    int ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("set_app_profile: copy_from_user failed\n");
        return -EFAULT;
    }

    ret = ksu_set_app_profile(&cmd.profile);
    if (!ret) {
        ksu_persistent_allow_list();
#ifndef CONFIG_KSU_SUSFS
        ksu_mark_running_process();
#endif
    }
    return ret;
}

static int do_get_feature(void __user *arg)
{
    struct ksu_get_feature_cmd cmd;
    bool supported = false;
    int ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("get_feature: copy_from_user failed\n");
        return -EFAULT;
    }

    ret = ksu_get_feature(cmd.feature_id, &cmd.value, &supported);
    cmd.supported = supported ? KSU_FEATURE_FLAG_SUPPORTED : 0;
    if (supported && ksu_feature_is_forced(cmd.feature_id))
        cmd.supported |= KSU_FEATURE_FLAG_FORCED;

    if (ret && supported) {
        pr_err("get_feature: failed for feature %u: %d\n", cmd.feature_id, ret);
        return ret;
    }

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("get_feature: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_set_feature(void __user *arg)
{
    struct ksu_set_feature_cmd cmd;
    int ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("set_feature: copy_from_user failed\n");
        return -EFAULT;
    }

    ret = ksu_set_feature(cmd.feature_id, cmd.value);
    if (ret) {
        pr_err("set_feature: failed for feature %u: %d\n", cmd.feature_id, ret);
        return ret;
    }

    return 0;
}

static int do_get_wrapper_fd(void __user *arg)
{
    if (!ksu_file_sid) {
        return -EINVAL;
    }

    struct ksu_get_wrapper_fd_cmd cmd;
    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("get_wrapper_fd: copy_from_user failed\n");
        return -EFAULT;
    }

    return ksu_install_file_wrapper(cmd.fd);
}

static int do_manage_mark(void __user *arg)
{
    struct ksu_manage_mark_cmd cmd;
#ifndef CONFIG_KSU_SUSFS
    int ret = 0;
#endif

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("manage_mark: copy_from_user failed\n");
        return -EFAULT;
    }

    switch (cmd.operation) {
#ifdef CONFIG_KSU_SUSFS
    case KSU_MARK_GET:
        cmd.result = susfs_is_current_proc_no_su() ? 0 : 1;
        break;
    case KSU_MARK_MARK:
    case KSU_MARK_UNMARK:
    case KSU_MARK_REFRESH:
        break;
#else
    case KSU_MARK_GET: {
        // Get task mark status
        ret = ksu_get_task_mark(cmd.pid);
        if (ret < 0) {
            pr_err("manage_mark: get failed for pid %d: %d\n", cmd.pid, ret);
            return ret;
        }
        cmd.result = (u32)ret;
        break;
    }
    case KSU_MARK_MARK: {
        if (cmd.pid == 0) {
            ksu_mark_all_process();
        } else {
            ret = ksu_set_task_mark(cmd.pid, true);
            if (ret < 0) {
                pr_err("manage_mark: set_mark failed for pid %d: %d\n", cmd.pid, ret);
                return ret;
            }
        }
        break;
    }
    case KSU_MARK_UNMARK: {
        if (cmd.pid == 0) {
            ksu_unmark_all_process();
        } else {
            ret = ksu_set_task_mark(cmd.pid, false);
            if (ret < 0) {
                pr_err("manage_mark: set_unmark failed for pid %d: %d\n", cmd.pid, ret);
                return ret;
            }
        }
        break;
    }
    case KSU_MARK_REFRESH: {
        ksu_mark_running_process();
        pr_info("manage_mark: refreshed running processes\n");
        break;
    }
#endif
    default: {
        pr_err("manage_mark: invalid operation %u\n", cmd.operation);
        return -EINVAL;
    }
    }
    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("manage_mark: copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int do_nuke_ext4_sysfs(void __user *arg)
{
    struct ksu_nuke_ext4_sysfs_cmd cmd;
    char mnt[256];
    long ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;

    if (!cmd.arg)
        return -EINVAL;

    memset(mnt, 0, sizeof(mnt));

    ret = strncpy_from_user(mnt, cmd.arg, sizeof(mnt));
    if (ret < 0) {
        pr_err("nuke ext4 copy mnt failed: %ld\n", ret);
        return -EFAULT;
    }

    if (ret == sizeof(mnt)) {
        pr_err("nuke ext4 mnt path too long\n");
        return -ENAMETOOLONG;
    }

    pr_info("do_nuke_ext4_sysfs: %s\n", mnt);

    return nuke_ext4_sysfs(mnt);
}

struct list_head mount_list = LIST_HEAD_INIT(mount_list);
DECLARE_RWSEM(mount_list_lock);

static int add_try_umount(void __user *arg)
{
    struct mount_entry *new_entry, *entry, *tmp;
    struct ksu_add_try_umount_cmd cmd;
    char buf[256] = { 0 };

    if (copy_from_user(&cmd, arg, sizeof cmd))
        return -EFAULT;

    switch (cmd.mode) {
    case KSU_UMOUNT_WIPE: {
        struct mount_entry *entry, *tmp;
        down_write(&mount_list_lock);
        list_for_each_entry_safe (entry, tmp, &mount_list, list) {
            pr_info("wipe_umount_list: removing entry: %s\n", entry->umountable);
            list_del(&entry->list);
            kfree(entry->umountable);
            kfree(entry);
        }
        up_write(&mount_list_lock);

        return 0;
    }

    case KSU_UMOUNT_ADD: {
        long len = strncpy_from_user(buf, (const char __user *)cmd.arg, 256);
        if (len <= 0)
            return -EFAULT;

        buf[sizeof(buf) - 1] = '\0';

        new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
        if (!new_entry)
            return -ENOMEM;

        new_entry->umountable = kstrdup(buf, GFP_KERNEL);
        if (!new_entry->umountable) {
            kfree(new_entry);
            return -ENOMEM;
        }

        down_write(&mount_list_lock);

        // disallow dupes
        // if this gets too many, we can consider moving this whole task to a kthread
        list_for_each_entry (entry, &mount_list, list) {
            if (!strcmp(entry->umountable, buf)) {
                pr_info("cmd_add_try_umount: %s is already here!\n", buf);
                up_write(&mount_list_lock);
                kfree(new_entry->umountable);
                kfree(new_entry);
                return -EEXIST;
            }
        }

        // now check flags and add
        // this also serves as a null check
        if (cmd.flags)
            new_entry->flags = cmd.flags;
        else
            new_entry->flags = 0;

        // debug
        list_add(&new_entry->list, &mount_list);
        up_write(&mount_list_lock);
        pr_info("cmd_add_try_umount: %s added!\n", buf);

        return 0;
    }

    // this is just strcmp'd wipe anyway
    case KSU_UMOUNT_DEL: {
        long len = strncpy_from_user(buf, (const char __user *)cmd.arg, sizeof(buf) - 1);
        if (len <= 0)
            return -EFAULT;

        buf[sizeof(buf) - 1] = '\0';

        down_write(&mount_list_lock);
        list_for_each_entry_safe (entry, tmp, &mount_list, list) {
            if (!strcmp(entry->umountable, buf)) {
                pr_info("cmd_add_try_umount: entry removed: %s\n", entry->umountable);
                list_del(&entry->list);
                kfree(entry->umountable);
                kfree(entry);
            }
        }
        up_write(&mount_list_lock);

        return 0;
    }

    default: {
        pr_err("cmd_add_try_umount: invalid operation %u\n", cmd.mode);
        return -EINVAL;
    }

    } // switch(cmd.mode)

    return 0;
}

static int do_set_init_pgrp(void __user *arg)
{
    int err;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
    struct pid *pids[PIDTYPE_MAX] = { 0 };
#endif

    write_lock_irq(&tasklist_lock);
    struct task_struct *p = current->group_leader;
    struct pid *init_group = task_pgrp(&init_task);

    err = -EPERM;
    if (task_session(p) != task_session(&init_task))
        goto out;

    err = 0;
    if (task_pgrp(p) != init_group) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
        change_pid(pids, p, PIDTYPE_PGID, init_group);
#else
        change_pid(p, PIDTYPE_PGID, init_group);
#endif
    }

out:
    write_unlock_irq(&tasklist_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
    free_pids(pids);
#endif

    return err;
}

static int do_get_sulog_fd(void __user *arg)
{
    struct ksu_get_sulog_fd_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("get_sulog_fd: copy_from_user failed\n");
        return -EFAULT;
    }

    if (cmd.flags) {
        pr_err("get_sulog_fd: unsupported flags 0x%x\n", cmd.flags);
        return -EINVAL;
    }

    return ksu_install_sulog_fd();
}

static int do_disable_escape_to_root(void __user *arg)
{
    set_thread_flag(TIF_KSU_DISABLE_ESCAPE_WITH_ROOT);
    return 0;
}

static int do_lookup_symbols(void __user *arg)
{
    struct ksu_lookup_symbols_cmd cmd;
    const u64 __user *unames;
    u64 __user *uaddrs;
    char **names;
    unsigned long *addrs;
    int ret = 0;
    u32 i;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (cmd.flags || !cmd.count || cmd.count > KSU_LOOKUP_SYMBOLS_MAX)
        return -EINVAL;

    names = kvcalloc(cmd.count, sizeof(*names), GFP_KERNEL);
    addrs = kvcalloc(cmd.count, sizeof(*addrs), GFP_KERNEL);
    if (!names || !addrs) {
        ret = -ENOMEM;
        goto out;
    }

    unames = (const u64 __user *)(uintptr_t)cmd.names;
    uaddrs = (u64 __user *)(uintptr_t)cmd.addrs;
    for (i = 0; i < cmd.count; i++) {
        u64 uname;
        char *name;

        if (get_user(uname, unames + i)) {
            ret = -EFAULT;
            goto out;
        }
        name = strndup_user((const char __user *)(uintptr_t)uname, KSYM_NAME_LEN);
        if (IS_ERR(name)) {
            if (PTR_ERR(name) == -EINVAL)
                continue;
            ret = PTR_ERR(name);
            goto out;
        }
        if (!*name) {
            kfree(name);
            continue;
        }
        names[i] = name;
    }

    ret = ksu_lookup_kernel_symbols((const char *const *)names, addrs, cmd.count);
    if (ret < 0)
        goto out;
    for (i = 0; i < cmd.count; i++) {
        if (put_user((u64)addrs[i], uaddrs + i)) {
            ret = -EFAULT;
            goto out;
        }
    }

out:
    if (names) {
        for (i = 0; i < cmd.count; i++)
            kfree(names[i]);
    }
    kvfree(names);
    kvfree(addrs);
    return ret;
}

#ifdef CONFIG_KSU_YUKIZYGISK
static int do_yz_handoff(void __user *arg)
{
    return ksu_yukizygisk_handoff_module_fds(arg);
}

static int do_yz_set_dlopen(void __user *arg)
{
    struct yz_dlopen_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    ksu_yukizygisk_set_linker_offsets(cmd.dlopen_offset, cmd.dlsym_offset);
    return 0;
}

static int do_yz_set_dlopen32(void __user *arg)
{
    struct yz_dlopen_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    ksu_yukizygisk_set_compat_linker_offsets(cmd.dlopen_offset, cmd.dlsym_offset);
    return 0;
}

static int do_yz_reload(void __user *arg)
{
    (void)arg;
    return ksu_yukizygisk_emit_reload();
}

static int do_yz_set_yukilinker(void __user *arg)
{
    struct yz_yukilinker_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    ksu_yukizygisk_set_first_stage_loader(cmd.enabled != 0);
    return 0;
}

static int do_yz_set_native_targets(void __user *arg)
{
    struct yz_native_targets_cmd *cmd;
    int ret;

    cmd = memdup_user(arg, sizeof(*cmd));
    if (IS_ERR(cmd))
        return PTR_ERR(cmd);
    ret = ksu_yukizygisk_set_native_targets(cmd);
    kfree(cmd);
    return ret;
}

static int do_yz_restore_native_load_policy(void __user *arg)
{
    struct yz_native_load_policy_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    return ksu_yukizygisk_restore_native_load_policy((pid_t)cmd.pid);
}

static int do_yz_get_safemode(void __user *arg)
{
    struct yz_safemode_status_cmd cmd;
    int ret;

    ret = ksu_yukizygisk_get_safemode(&cmd);
    if (ret)
        return ret;
    if (copy_to_user(arg, &cmd, sizeof(cmd)))
        return -EFAULT;
    return 0;
}

static int do_yz_get_runtime(void __user *arg)
{
    struct yz_runtime_query_cmd cmd;
    struct yz_runtime_record *entries = NULL;
    void __user *user_entries;
    int ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (cmd.capacity > YZ_RUNTIME_RECORD_MAX || (cmd.capacity && !cmd.entries))
        return -EINVAL;

    user_entries = (void __user *)(uintptr_t)cmd.entries;
    if (cmd.capacity) {
        entries = kvcalloc(cmd.capacity, sizeof(*entries), GFP_KERNEL);
        if (!entries)
            return -ENOMEM;
    }
    ret = ksu_yukizygisk_get_runtime(entries, cmd.capacity, &cmd);
    if (ret)
        goto out;
    if (cmd.count && copy_to_user(user_entries, entries, sizeof(*entries) * cmd.count)) {
        ret = -EFAULT;
        goto out;
    }
    if (copy_to_user(arg, &cmd, sizeof(cmd)))
        ret = -EFAULT;
out:
    kvfree(entries);
    return ret;
}

static int do_yz_get_exit_history_fd(void __user *arg)
{
    return ksu_yukizygisk_get_exit_history_fd(arg);
}

static int do_yz_get_health(void __user *arg)
{
    return ksu_yukizygisk_get_health(arg);
}

static int do_yz_report_runtime(void __user *arg)
{
    struct yz_runtime_report_cmd cmd;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    cmd.module_id[sizeof(cmd.module_id) - 1] = '\0';
    return ksu_yukizygisk_report_runtime(&cmd);
}

static int do_yz_allow_module_load_policy(void __user *arg)
{
    struct yz_module_load_policy_cmd cmd;
    struct task_struct *task;
    const struct cred *cred;
    struct file *file;
    bool directory;
    bool source_image;
    int ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (!cmd.pid || cmd.dirfd < 0)
        return -EINVAL;
    rcu_read_lock();
    task = get_pid_task(find_vpid(cmd.pid), PIDTYPE_PID);
    rcu_read_unlock();
    if (!task)
        return -ESRCH;
    cred = get_task_cred(task);
    /* fget rather than fdget: `struct fd` is a tagged pointer from 6.13 on, so neither `.file`
     * nor `fd_file()` compiles across the whole range this module is built for. */
    file = fget(cmd.dirfd);
    if (!file) {
        ret = -EBADF;
        goto out_cred;
    }
    directory = S_ISDIR(file_inode(file)->i_mode);
    source_image = S_ISREG(file_inode(file)->i_mode) && file_inode(file)->i_sb->s_magic == TMPFS_MAGIC &&
                   (file->f_mode & FMODE_READ) && !(file->f_mode & FMODE_WRITE);
    if (!directory && !source_image) {
        ret = -EINVAL;
        goto out_fd;
    }
    if (!is_zygote(cred) &&
        !(source_image && ksu_yukizygisk_is_native_runtime(task->tgid, READ_ONCE(task->start_boottime)))) {
        ret = -EPERM;
        goto out_fd;
    }
    ret = ksu_yukizygisk_allow_module_load_policy(task, file, cred);
out_fd:
    fput(file);
out_cred:
    put_cred(cred);
    put_task_struct(task);
    pr_info("yukizygisk: module load policy request pid=%u fd=%d err=%d\n", cmd.pid, cmd.dirfd, ret);
    return ret;
}

struct yz_unmap_tw {
    struct callback_head cb;
    unsigned long addr[YZ_MAX_UNMAP_SEGS];
    unsigned long size[YZ_MAX_UNMAP_SEGS];
    unsigned int n;
    unsigned int retry;
};

/* Retry until the program counter leaves all requested ranges. */
static void yz_unmap_tw_func(struct callback_head *cb)
{
    struct yz_unmap_tw *tw = container_of(cb, struct yz_unmap_tw, cb);
    struct pt_regs *regs = task_pt_regs(current);
    unsigned long pc = regs ? instruction_pointer(regs) : 0;
    unsigned int i;

    for (i = 0; i < tw->n; i++) {
        if (pc >= tw->addr[i] && pc < tw->addr[i] + tw->size[i]) {
            if (++tw->retry < 16) {
                init_task_work(&tw->cb, yz_unmap_tw_func);
                if (!task_work_add(current, &tw->cb, TWA_RESUME))
                    return; /* re-queued; keep tw */
            }
            pr_warn("yukizygisk: core unmap skipped pid=%d "
                    "pc=0x%lx retries=%u\n",
                    current->pid, pc, tw->retry);
            kfree(tw);
            module_put(THIS_MODULE);
            return;
        }
    }

    for (i = 0; i < tw->n; i++) {
        pr_info("yukizygisk: core segment unmap pid=%d address=0x%lx "
                "size=0x%lx\n",
                current->pid, tw->addr[i], tw->size[i]);
        vm_munmap(tw->addr[i], tw->size[i]);
    }
    kfree(tw);
    module_put(THIS_MODULE);
}

static int do_yz_unmap_pid(void __user *arg)
{
    struct yz_unmap_pid_cmd cmd;
    struct task_struct *task;
    struct yz_unmap_tw *tw;
    unsigned int i;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (cmd.n_segs == 0 || cmd.n_segs > YZ_MAX_UNMAP_SEGS)
        return -EINVAL;

    rcu_read_lock();
    task = get_pid_task(find_vpid(cmd.pid), PIDTYPE_PID);
    rcu_read_unlock();
    if (!task)
        return -ESRCH;

    if (!is_appuid(task_uid(task).val)) {
        pr_info("yukizygisk: core unmap rejected pid=%u uid=%u\n", cmd.pid, task_uid(task).val);
        put_task_struct(task);
        return -EPERM;
    }

    tw = kzalloc(sizeof(*tw), GFP_KERNEL);
    if (!tw) {
        put_task_struct(task);
        return -ENOMEM;
    }
    init_task_work(&tw->cb, yz_unmap_tw_func);
    tw->n = cmd.n_segs;
    for (i = 0; i < cmd.n_segs; i++) {
        tw->addr[i] = (unsigned long)cmd.addr[i];
        tw->size[i] = (unsigned long)cmd.size[i];
    }
    if (ksu_task_work_add(task, &tw->cb)) {
        kfree(tw);
        put_task_struct(task);
        return -ESRCH;
    }
    put_task_struct(task);
    pr_info("yukizygisk: core unmap scheduled pid=%u segments=%u\n", cmd.pid, cmd.n_segs);
    return 0;
}

static int do_yz_unmap_self(void __user *arg)
{
    struct yz_unmap_self_cmd cmd;
    struct yz_unmap_tw *tw;
    unsigned int i;

    if (!current->mm)
        return -EINVAL;
    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (cmd.n_segs == 0 || cmd.n_segs > YZ_MAX_UNMAP_SEGS)
        return -EINVAL;
    for (i = 0; i < cmd.n_segs; i++) {
        unsigned long a = (unsigned long)cmd.addr[i];
        unsigned long s = (unsigned long)cmd.size[i];

        if (a == 0 || s == 0 || a >= TASK_SIZE || s > TASK_SIZE || a + s < a || a + s > TASK_SIZE) {
            pr_warn("yukizygisk: invalid core segment pid=%d "
                    "address=0x%lx size=0x%lx\n",
                    current->pid, a, s);
            return -EINVAL;
        }
    }

    tw = kzalloc(sizeof(*tw), GFP_KERNEL);
    if (!tw)
        return -ENOMEM;
    init_task_work(&tw->cb, yz_unmap_tw_func);
    tw->n = cmd.n_segs;
    for (i = 0; i < cmd.n_segs; i++) {
        tw->addr[i] = (unsigned long)cmd.addr[i];
        tw->size[i] = (unsigned long)cmd.size[i];
    }
    if (ksu_task_work_add(current, &tw->cb)) {
        kfree(tw);
        return -ESRCH;
    }
    pr_info("yukizygisk: self core unmap scheduled pid=%d segments=%u\n", current->pid, cmd.n_segs);
    return 0;
}

/* COW-patch target text via access_process_vm(FOLL_FORCE|FOLL_WRITE). */
static int do_yz_patch_text(void __user *arg)
{
    struct yz_patch_text_cmd cmd;
    struct task_struct *task;
    int n;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (cmd.len == 0 || cmd.len > YZ_PATCH_TEXT_MAX)
        return -EINVAL;
    if (cmd.addr == 0 || cmd.addr >= TASK_SIZE || cmd.addr + cmd.len < cmd.addr || cmd.addr + cmd.len > TASK_SIZE)
        return -EINVAL;

    rcu_read_lock();
    task = get_pid_task(find_vpid(cmd.pid), PIDTYPE_PID);
    rcu_read_unlock();
    if (!task)
        return -ESRCH;

    /* zygiskd validates cmd.pid against the SO_PEERCRED peer PID. */

    n = access_process_vm(task, (unsigned long)cmd.addr, cmd.bytes, cmd.len, FOLL_FORCE | FOLL_WRITE);
    put_task_struct(task);
    if (n != (int)cmd.len) {
        pr_warn("yukizygisk: text patch incomplete pid=%u "
                "address=0x%llx written=%d requested=%u\n",
                cmd.pid, cmd.addr, n, cmd.len);
        return -EFAULT;
    }
    pr_info("yukizygisk: text patched pid=%u address=0x%llx bytes=%u\n", cmd.pid, cmd.addr, cmd.len);
    return 0;
}

static int yz_patch_v2_read(void *context, unsigned long address, void *buffer, unsigned int length)
{
    return access_process_vm(context, address, buffer, length, FOLL_FORCE);
}

static int yz_patch_v2_write(void *context, unsigned long address, void *buffer, unsigned int length)
{
    return access_process_vm(context, address, buffer, length, FOLL_FORCE | FOLL_WRITE);
}

static int do_yz_patch_text_v2(void __user *arg)
{
    struct yz_patch_text_v2_cmd cmd;
    struct task_struct *task;
    u8 before[YZ_PATCH_TEXT_MAX];
    u8 after[YZ_PATCH_TEXT_MAX];
    struct yz_patch_io io;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;
    if (!cmd.len || cmd.len > YZ_PATCH_TEXT_MAX || !cmd.addr || cmd.addr >= TASK_SIZE ||
        cmd.addr + cmd.len < cmd.addr || cmd.addr + cmd.len > TASK_SIZE ||
        (cmd.addr & (PAGE_SIZE - 1)) + cmd.len > PAGE_SIZE)
        return -EINVAL;

    cmd.result = YZ_PATCH_V2_REJECTED;
    rcu_read_lock();
    task = get_pid_task(find_vpid(cmd.pid), PIDTYPE_PID);
    rcu_read_unlock();
    if (!task)
        return -ESRCH;

    io.context = task;
    io.read = yz_patch_v2_read;
    io.write = yz_patch_v2_write;
    cmd.result =
        yz_patch_transaction(&io, (unsigned long)cmd.addr, cmd.len, cmd.expected, cmd.replacement, before, after);
    put_task_struct(task);
    return copy_to_user(arg, &cmd, sizeof(cmd)) ? -EFAULT : 0;
}
#endif // #ifdef CONFIG_KSU_YUKIZYGISK

// IOCTL handlers mapping table
// clang-format off
static const struct ksu_ioctl_cmd_map ksu_ioctl_handlers[] = {
    { 
        .cmd = KSU_IOCTL_GRANT_ROOT,
        .name = "GRANT_ROOT",
        .handler = do_grant_root,
        .perm_check = allowed_for_su 
    },
    {
        .cmd = KSU_IOCTL_GET_INFO,
        .name = "GET_INFO",
        .handler = do_get_info,
        .perm_check = always_allow
    },
    {
        .cmd = KSU_IOCTL_GET_INFO_LEGACY,
        .name = "GET_INFO_LEGACY",
        .handler = do_get_info_legacy,
        .perm_check = always_allow
    },
    {
        .cmd = KSU_IOCTL_GET_FULL_VERSION,
        .name = "GET_FULL_VERSION",
        .handler = do_get_full_version,
        .perm_check = always_allow
    },
    {
        .cmd = KSU_IOCTL_REPORT_EVENT,
        .name = "REPORT_EVENT",
        .handler = do_report_event,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_SET_SEPOLICY,
        .name = "SET_SEPOLICY",
        .handler = do_set_sepolicy,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_CHECK_SAFEMODE,
        .name = "CHECK_SAFEMODE",
        .handler = do_check_safemode,
        .perm_check = always_allow
    },
    {
        .cmd = KSU_IOCTL_GET_ALLOW_LIST,
        .name = "GET_ALLOW_LIST",
        .handler = do_get_allow_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_DENY_LIST,
        .name = "GET_DENY_LIST",
        .handler = do_get_deny_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NEW_GET_ALLOW_LIST,
        .name = "NEW_GET_ALLOW_LIST",
        .handler = do_new_get_allow_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NEW_GET_DENY_LIST,
        .name = "NEW_GET_DENY_LIST",
        .handler = do_new_get_deny_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_UID_GRANTED_ROOT,
        .name = "UID_GRANTED_ROOT",
        .handler = do_uid_granted_root,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_UID_SHOULD_UMOUNT,
        .name = "UID_SHOULD_UMOUNT",
        .handler = do_uid_should_umount,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_MANAGER_APPID,
        .name = "GET_MANAGER_APPID",
        .handler = do_get_manager_appid,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_APP_PROFILE,
        .name = "GET_APP_PROFILE",
        .handler = do_get_app_profile,
        .perm_check = only_manager
    },
    {
        .cmd = KSU_IOCTL_SET_APP_PROFILE,
        .name = "SET_APP_PROFILE",
        .handler = do_set_app_profile,
        .perm_check = only_manager
    },
    {
        .cmd = KSU_IOCTL_GET_FEATURE,
        .name = "GET_FEATURE",
        .handler = do_get_feature,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_SET_FEATURE,
        .name = "SET_FEATURE",
        .handler = do_set_feature,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_WRAPPER_FD,
        .name = "GET_WRAPPER_FD",
        .handler = do_get_wrapper_fd,
        .perm_check = manager_or_root,
        .allow_su_session = true
    },
    {
        .cmd = KSU_IOCTL_MANAGE_MARK,
        .name = "MANAGE_MARK",
        .handler = do_manage_mark,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NUKE_EXT4_SYSFS,
        .name = "NUKE_EXT4_SYSFS",
        .handler = do_nuke_ext4_sysfs,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_ADD_TRY_UMOUNT,
        .name = "ADD_TRY_UMOUNT",
        .handler = add_try_umount,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_SET_INIT_PGRP,
        .name = "SET_INIT_PGRP",
        .handler = do_set_init_pgrp,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_GET_SULOG_FD,
        .name = "GET_SULOG_FD",
        .handler = do_get_sulog_fd,
        .perm_check = only_root
    },
    { 
        .cmd = KSU_IOCTL_DISABLE_ESCAPE_TO_ROOT, 
        .name = "DISABLE_ESCAPE_TO_ROOT", 
        .handler = do_disable_escape_to_root, 
        .perm_check = only_root,
        .allow_su_session = true
    },
    {
        .cmd = KSU_IOCTL_LOOKUP_SYMBOLS,
        .name = "LOOKUP_SYMBOLS",
        .handler = do_lookup_symbols,
        .perm_check = only_root
    },
#ifdef CONFIG_KSU_YUKIZYGISK
    {.cmd = KSU_IOCTL_YZ_HANDOFF,
     .name = "YZ_HANDOFF",
     .handler = do_yz_handoff,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_SET_DLOPEN,
     .name = "YZ_SET_DLOPEN",
     .handler = do_yz_set_dlopen,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_SET_DLOPEN32,
     .name = "YZ_SET_DLOPEN32",
     .handler = do_yz_set_dlopen32,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_SET_YUKILINKER,
     .name = "YZ_SET_YUKILINKER",
     .handler = do_yz_set_yukilinker,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_SET_NATIVE_TARGETS,
     .name = "YZ_SET_NATIVE_TARGETS",
     .handler = do_yz_set_native_targets,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_RESTORE_NATIVE_LOAD_POLICY,
     .name = "YZ_RESTORE_NATIVE_LOAD_POLICY",
     .handler = do_yz_restore_native_load_policy,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_SAFEMODE,
     .name = "YZ_GET_SAFEMODE",
     .handler = do_yz_get_safemode,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_RUNTIME,
     .name = "YZ_GET_RUNTIME",
     .handler = do_yz_get_runtime,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_EXIT_HISTORY_FD,
     .name = "YZ_GET_EXIT_HISTORY_FD",
     .handler = do_yz_get_exit_history_fd,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_REGISTER_ENDPOINT,
     .name = "YZ_REGISTER_ENDPOINT",
     .handler = yz_endpoint_register,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_UNREGISTER_ENDPOINT,
     .name = "YZ_UNREGISTER_ENDPOINT",
     .handler = yz_endpoint_unregister,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_ENDPOINT_STATE_FD,
     .name = "YZ_GET_ENDPOINT_STATE_FD",
     .handler = yz_endpoint_get_fd,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_HEALTH,
     .name = "YZ_GET_HEALTH",
     .handler = do_yz_get_health,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_REPORT_RUNTIME,
     .name = "YZ_REPORT_RUNTIME",
     .handler = do_yz_report_runtime,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_ALLOW_MODULE_LOAD_POLICY,
     .name = "YZ_ALLOW_MODULE_LOAD_POLICY",
     .handler = do_yz_allow_module_load_policy,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_UNMAP_PID,
     .name = "YZ_UNMAP_PID",
     .handler = do_yz_unmap_pid,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_UNMAP_SELF,
     .name = "YZ_UNMAP_SELF",
     .handler = do_yz_unmap_self,
     .perm_check = injected_app},
    {.cmd = KSU_IOCTL_YZ_PATCH_TEXT,
     .name = "YZ_PATCH_TEXT",
     .handler = do_yz_patch_text,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_PATCH_TEXT_V2,
     .name = "YZ_PATCH_TEXT_V2",
     .handler = do_yz_patch_text_v2,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_GET_EVENT_CHANNEL,
     .name = "YZ_GET_EVENT_CHANNEL",
     .handler = yz_events_get_channel,
     .perm_check = only_root},
    {.cmd = KSU_IOCTL_YZ_RELOAD,
     .name = "YZ_RELOAD",
     .handler = do_yz_reload,
     .perm_check = manager_or_root},
#endif // #ifdef CONFIG_KSU_YUKIZYGISK
    {
        .cmd = 0,
        .name = NULL,
        .handler = NULL,
        .perm_check = NULL
    } // Sentinel
};
// clang-format on

long ksu_supercall_handle_ioctl(const struct file *filp, unsigned int cmd, void __user *argp)
{
    int i;

#ifdef CONFIG_KSU_DEBUG
    pr_info_ratelimited("ksu ioctl: cmd=0x%x from uid=%d\n", cmd, current_uid().val);
#endif

    for (i = 0; ksu_ioctl_handlers[i].handler; i++) {
        if (cmd == ksu_ioctl_handlers[i].cmd) {
            // Check permission first
            if (ksu_ioctl_handlers[i].perm_check && !ksu_ioctl_handlers[i].perm_check() &&
                !(ksu_ioctl_handlers[i].allow_su_session && ksu_is_su_session_fd(filp))) {
                pr_warn_ratelimited("ksu ioctl: permission denied for cmd=0x%x uid=%d\n", cmd, current_uid().val);
                return -EPERM;
            }
            // Execute handler
            return ksu_ioctl_handlers[i].handler(argp);
        }
    }

    pr_warn_ratelimited("ksu ioctl: unsupported command 0x%x\n", cmd);
    return -ENOTTY;
}

void __init ksu_supercall_dump_commands(void)
{
    int i;

    pr_info("KernelSU IOCTL Commands:\n");
    for (i = 0; ksu_ioctl_handlers[i].handler; i++) {
        pr_info("  %-18s = 0x%08x\n", ksu_ioctl_handlers[i].name, ksu_ioctl_handlers[i].cmd);
    }
}

void ksu_supercall_cleanup_state(void)
{
    struct mount_entry *entry, *tmp;

    down_write(&mount_list_lock);
    list_for_each_entry_safe (entry, tmp, &mount_list, list) {
        list_del(&entry->list);
        kfree(entry->umountable);
        kfree(entry);
    }
    up_write(&mount_list_lock);
}
