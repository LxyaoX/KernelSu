package me.weishu.kernelsu.data.repository

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.util.Log
import androidx.core.content.edit
import com.materialkolor.PaletteStyle
import com.topjohnwu.superuser.ShellUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.data.model.HomeLayoutStyle
import me.weishu.kernelsu.data.bootprops.BootPropsInstaller
import me.weishu.kernelsu.data.model.BootPropsStatus
import me.weishu.kernelsu.data.model.YukiZygiskStatus
import me.weishu.kernelsu.data.model.RekernelXStatus
import me.weishu.kernelsu.data.rekernelx.RekernelXInstaller
import me.weishu.kernelsu.data.pif.PifInstaller
import me.weishu.kernelsu.data.model.PifOption
import me.weishu.kernelsu.data.model.PifStatus
import me.weishu.kernelsu.data.teesim.TeesimInstaller
import me.weishu.kernelsu.data.model.TeesimStatus
import me.weishu.kernelsu.data.model.ModuleCustomOrderStore
import me.weishu.kernelsu.data.model.ModuleSortGroup
import me.weishu.kernelsu.data.model.ModuleSortPriorityStore
import me.weishu.kernelsu.data.modulestore.StoreSourceKind
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.magica.BootCompletedReceiver
import me.weishu.kernelsu.ui.util.execKsud
import me.weishu.kernelsu.ui.util.execKsudBatch
import me.weishu.kernelsu.ui.util.execKsudResult
import me.weishu.kernelsu.ui.util.getFeaturePersistValue
import me.weishu.kernelsu.ui.util.getFeatureValue
import me.weishu.kernelsu.ui.util.getFeatureStatus
import me.weishu.kernelsu.ui.util.getSELinuxStatusRaw
import me.weishu.kernelsu.ui.util.setSELinuxEnforcing
import org.json.JSONObject
import java.security.SecureRandom

private const val SETTINGS_PREFS = "settings"
private const val KEY_USE_SOFT_REBOOT = "soft_reboot"

/** Partitions backed up by default; the runtime list is filtered by what the device actually has. */
val DEFAULT_BACKUP_PARTITIONS = setOf("boot", "init_boot", "vendor_boot")

/** Prefer soft reboot: always in jailbreak mode, or when the setting is enabled. */
/**
 * Single-quote a value for the root shell.
 *
 * Module ids are already validated by ksud; this keeps the command line
 * unambiguous even if a caller passes something unexpected.
 */
private fun shellQuote(value: String): String = "'" + value.replace("'", "'\"'\"'") + "'"

private fun ksudFailure(result: com.topjohnwu.superuser.Shell.Result, fallback: String): String {
    return (result.err + result.out)
        .asSequence()
        .map(String::trim)
        .filter(String::isNotEmpty)
        .lastOrNull()
        ?.removePrefix("Error: ")
        ?.takeIf(String::isNotEmpty)
        ?: "$fallback (exit ${result.code})"
}

private fun requireKsud(result: com.topjohnwu.superuser.Shell.Result, fallback: String) {
    check(result.isSuccess) { ksudFailure(result, fallback) }
}

fun isSoftRebootPreferred(): Boolean =
    Natives.isLateLoadMode || ksuApp.getSharedPreferences(SETTINGS_PREFS, Context.MODE_PRIVATE)
        .getBoolean(KEY_USE_SOFT_REBOOT, false)

class SettingsRepositoryImpl : SettingsRepository {

    private companion object {
        private const val INTENT_TOKEN_KEY = "intent_token"
        private val secureRandom = SecureRandom()
    }

    private val prefs by lazy {
        ksuApp.getSharedPreferences(SETTINGS_PREFS, Context.MODE_PRIVATE)
    }

    override var checkUpdate: Boolean
        get() = prefs.getBoolean("check_update", true)
        set(value) = prefs.edit { putBoolean("check_update", value) }

    override var checkModuleUpdate: Boolean
        get() = prefs.getBoolean("module_check_update", true)
        set(value) = prefs.edit { putBoolean("module_check_update", value) }

    override var themeMode: Int
        get() = prefs.getInt("color_mode", 0)
        set(value) = prefs.edit { putInt("color_mode", value) }

    override var keyColor: Int
        get() = prefs.getInt("key_color", 0)
        set(value) = prefs.edit { putInt("key_color", value) }

    override var colorStyle: String
        get() = prefs.getString("color_style", PaletteStyle.TonalSpot.name) ?: PaletteStyle.TonalSpot.name
        set(value) = prefs.edit { putString("color_style", value) }

    override var useSystemDynamicColor: Boolean
        get() = prefs.getBoolean("use_system_dynamic_color", keyColor == 0)
        set(value) = prefs.edit { putBoolean("use_system_dynamic_color", value) }

    override var colorGenerationMode: String
        get() = prefs.getString("color_generation_mode", "custom") ?: "custom"
        set(value) = prefs.edit { putString("color_generation_mode", value) }

    override var colorContrast: String
        get() = prefs.getString("color_contrast", "STANDARD") ?: "STANDARD"
        set(value) = prefs.edit { putString("color_contrast", value) }

    override var statsTopLayout: String
        get() = prefs.getString("stats_top_layout", "list") ?: "list"
        set(value) = prefs.edit { putString("stats_top_layout", value) }

    override var listWorkingCardModeHidden: Boolean
        get() = prefs.getBoolean("list_working_card_mode_hidden", false)
        set(value) = prefs.edit { putBoolean("list_working_card_mode_hidden", value) }

    override var hideHomeFingerprint: Boolean
        get() = prefs.getBoolean("hide_home_fingerprint", false)
        set(value) = prefs.edit { putBoolean("hide_home_fingerprint", value) }

    override var hideHomeZygisk: Boolean
        get() = prefs.getBoolean("hide_home_zygisk", false)
        set(value) = prefs.edit { putBoolean("hide_home_zygisk", value) }

    override var hideHomeSusfs: Boolean
        get() = prefs.getBoolean("hide_home_susfs", false)
        set(value) = prefs.edit { putBoolean("hide_home_susfs", value) }

    override var hideHomeKernelFullVersion: Boolean
        get() = prefs.getBoolean("hide_home_kernel_full_version", false)
        set(value) = prefs.edit { putBoolean("hide_home_kernel_full_version", value) }

    override var enablePredictiveBack: Boolean
        get() = prefs.getBoolean("enable_predictive_back", false)
        set(value) = prefs.edit { putBoolean("enable_predictive_back", value) }

    override var enableNavigationBadge: Boolean
        get() = prefs.getBoolean("enable_navigation_badge", true)
        set(value) = prefs.edit { putBoolean("enable_navigation_badge", value) }

    override var pageScale: Float
        get() = prefs.getFloat("page_scale", 1.0f)
        set(value) = prefs.edit { putFloat("page_scale", value) }

    override var moduleDescriptionMaxLines: Int
        get() = prefs.getInt("module_description_max_lines", 4)
        set(value) = prefs.edit { putInt("module_description_max_lines", value) }

    override var enableWebDebugging: Boolean
        get() = prefs.getBoolean("enable_web_debugging", false)
        set(value) = prefs.edit { putBoolean("enable_web_debugging", value) }

    override var homeLayoutStyle: String
        get() = HomeLayoutStyle.fromValue(
            prefs.getString("home_layout_style", HomeLayoutStyle.DEFAULT)
        )
        set(value) = prefs.edit { putString("home_layout_style", HomeLayoutStyle.fromValue(value)) }

    override var moduleSortGroups: Set<ModuleSortGroup>
        get() = ModuleSortPriorityStore.decode(prefs.getString(ModuleSortPriorityStore.Key, null))
        set(value) = prefs.edit { putString(ModuleSortPriorityStore.Key, ModuleSortPriorityStore.encode(value)) }

    override var moduleSortEnabledFirst: Boolean
        get() = prefs.getBoolean("module_sort_enabled_first", false)
        set(value) = prefs.edit { putBoolean("module_sort_enabled_first", value) }

    override var moduleSortCustomOrder: List<String>
        get() = ModuleCustomOrderStore.decode(prefs.getString(ModuleCustomOrderStore.Key, null))
        set(value) = prefs.edit {
            if (value.isEmpty()) remove(ModuleCustomOrderStore.Key)
            else putString(ModuleCustomOrderStore.Key, ModuleCustomOrderStore.encode(value))
        }

    override var superuserShowSystemApps: Boolean
        get() = prefs.getBoolean("show_system_apps", false)
        set(value) = prefs.edit { putBoolean("show_system_apps", value) }

    override var superuserShowOnlyPrimaryUserApps: Boolean
        get() = prefs.getBoolean("show_only_primary_user_apps", false)
        set(value) = prefs.edit { putBoolean("show_only_primary_user_apps", value) }

    override var superuserSortOption: Int
        get() = prefs.getInt("superuser_sort_option", 0)
        set(value) = prefs.edit { putInt("superuser_sort_option", value) }

    override var suLogFilters: Set<String>?
        get() = prefs.getStringSet("sulog_filters", null)?.toSet()
        set(filters) = prefs.edit { putStringSet("sulog_filters", filters) }

    override var autoJailbreak: Boolean
        get() = prefs.getBoolean("auto_jailbreak", false)
        set(value) {
            runCatching {
                ksuApp.packageManager.setComponentEnabledSetting(
                    ComponentName(ksuApp, BootCompletedReceiver::class.java),
                    if (value) PackageManager.COMPONENT_ENABLED_STATE_ENABLED else PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                    PackageManager.DONT_KILL_APP
                )
            }.onFailure {
                Log.e("Settings", "failed to change boot receiver state to $value", it)
            }
            prefs.edit {
                putBoolean("auto_jailbreak", value)
            }
        }

    override var useSoftReboot: Boolean
        get() = prefs.getBoolean(KEY_USE_SOFT_REBOOT, false)
        set(value) = prefs.edit { putBoolean(KEY_USE_SOFT_REBOOT, value) }

    override var repoSourceKind: StoreSourceKind
        get() = StoreSourceKind.fromValue(prefs.getString("repo_source", null))
        set(value) = prefs.edit { putString("repo_source", value.value) }

    override var repoCustomUrl: String
        get() = prefs.getString("repo_custom_url", "").orEmpty()
        set(value) = prefs.edit { putString("repo_custom_url", value) }

    override var repoSelectedRepositoryUrl: String
        get() = prefs.getString("repo_cluster_url", "").orEmpty()
        set(value) = prefs.edit { putString("repo_cluster_url", value) }

    override var backupLocalEnabled: Boolean
        get() = prefs.getBoolean("backup_local_enabled", true)
        set(value) = prefs.edit { putBoolean("backup_local_enabled", value) }

    override var backupWebDavEnabled: Boolean
        get() = prefs.getBoolean("backup_webdav_enabled", false)
        set(value) = prefs.edit { putBoolean("backup_webdav_enabled", value) }

    override var webDavUrl: String
        get() = prefs.getString("backup_webdav_url", "").orEmpty()
        set(value) = prefs.edit { putString("backup_webdav_url", value) }

    override var webDavUsername: String
        get() = prefs.getString("backup_webdav_username", "").orEmpty()
        set(value) = prefs.edit { putString("backup_webdav_username", value) }

    override var webDavPath: String
        get() = prefs.getString("backup_webdav_path", "").orEmpty()
        set(value) = prefs.edit { putString("backup_webdav_path", value) }

    override var backupModulesEnabled: Boolean
        get() = prefs.getBoolean("backup_modules_enabled", true)
        set(value) = prefs.edit { putBoolean("backup_modules_enabled", value) }

    override var backupAllowlistEnabled: Boolean
        get() = prefs.getBoolean("backup_allowlist_enabled", true)
        set(value) = prefs.edit { putBoolean("backup_allowlist_enabled", value) }

    override var backupPartitions: Set<String>
        get() = prefs.getStringSet("backup_partitions", DEFAULT_BACKUP_PARTITIONS)?.toSet()
            ?: DEFAULT_BACKUP_PARTITIONS
        set(value) = prefs.edit { putStringSet("backup_partitions", value) }

    override var backupBeforeFlash: Boolean
        get() = prefs.getBoolean("backup_before_flash", false)
        set(value) = prefs.edit { putBoolean("backup_before_flash", value) }

    override val intentToken: String
        get() {
        val existing = prefs.getString(INTENT_TOKEN_KEY, null)
        if (!existing.isNullOrBlank()) return existing
        val token = ByteArray(32).also(secureRandom::nextBytes)
            .joinToString(separator = "") { "%02x".format(it) }
        prefs.edit { putString(INTENT_TOKEN_KEY, token) }
        return token
    }

    override suspend fun getSuCompatStatus(): String = getFeatureStatus("su_compat")

    override suspend fun getSuCompatPersistValue(): Long? = getFeaturePersistValue("su_compat")

    override fun isSuEnabled(): Boolean = Natives.isSuEnabled()

    override fun setSuEnabled(enabled: Boolean): Boolean = Natives.setSuEnabled(enabled)

    override fun setSuCompatModePref(mode: Int) = prefs.edit { putInt("su_compat_mode", mode) }

    override fun getSuCompatModePref(): Int = prefs.getInt("su_compat_mode", 0)

    override suspend fun getKernelUmountStatus(): String = getFeatureStatus("kernel_umount")

    override fun isKernelUmountEnabled(): Boolean = Natives.isKernelUmountEnabled()

    override fun setKernelUmountEnabled(enabled: Boolean): Boolean = Natives.setKernelUmountEnabled(enabled)

    override suspend fun getSelinuxHideStatus(): String = getFeatureStatus("selinux_hide")

    override fun isSelinuxHideEnabled(): Boolean = Natives.isSelinuxHideEnabled()

    override fun setSelinuxHideEnabled(enabled: Boolean): Int = Natives.setSelinuxHideEnabled(enabled)

    override suspend fun getMountViewFilterStatus(): String = getFeatureStatus("mount_view_filter")

    override suspend fun isMountViewFilterEnabled(): Boolean = getFeatureValue("mount_view_filter") == 1L

    override suspend fun setMountViewFilterEnabled(enabled: Boolean): Result<Unit> = runCatching {
        requireKsud(
            execKsudResult("feature set mount_view_filter ${if (enabled) 1 else 0}"),
            "Mount view filter configuration failed",
        )
    }

    override suspend fun getSELinuxMode(): String = withContext(Dispatchers.IO) { getSELinuxStatusRaw() }

    override suspend fun setSELinuxMode(enforcing: Boolean): Boolean =
        withContext(Dispatchers.IO) { setSELinuxEnforcing(enforcing) }

    override suspend fun getSulogStatus(): String = getFeatureStatus("sulog")

    override suspend fun getAvcSpoofStatus(): String = getFeatureStatus("avc_spoof")

    override fun isAvcSpoofEnabled(): Boolean = Natives.isAvcSpoofEnabled()

    override fun setAvcSpoofEnabled(enabled: Boolean): Boolean = Natives.setAvcSpoofEnabled(enabled)

    override suspend fun getSulogPersistValue(): Long? = getFeaturePersistValue("sulog")
    override suspend fun getYukiZygiskStatus(): Result<YukiZygiskStatus> =
        withContext(Dispatchers.IO) {
            runCatching {
                val result = execKsudResult("yukizygisk status")
                check(result.isSuccess) { "ksud yukizygisk status failed (exit ${result.code})" }
                YukiZygiskStatus.fromJson(result.out.joinToString("\n"))
                    ?: error("Invalid ksud yukizygisk status output")
            }
        }

    override suspend fun getSulogValue(): Long? = getFeatureValue("sulog")

    override fun setSulogEnabled(enabled: Boolean): Boolean = execKsud("feature set sulog ${if (enabled) 1 else 0}", true)

    override suspend fun getRekernelXStatus(): Result<RekernelXStatus> = withContext(Dispatchers.IO) {
        runCatching { RekernelXInstaller.status() }
    }

    override suspend fun setRekernelXEnabled(enabled: Boolean): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val before = RekernelXInstaller.status()
            if (enabled) {
                RekernelXInstaller.enable(before)
            } else {
                RekernelXInstaller.disable()
            }
            // The boot script is the persisted decision, so read it back instead of trusting the
            // individual commands: that is what the next boot will act on.
            val after = RekernelXInstaller.status()
            check(after.enabled == enabled) { "ReKernel-X boot state did not change" }
        }
    }

    override suspend fun getBootPropsStatus(): Result<BootPropsStatus> = withContext(Dispatchers.IO) {
        runCatching { BootPropsInstaller.status() }
    }

    override suspend fun setBootPropsEnabled(enabled: Boolean): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val before = BootPropsInstaller.status()
            if (enabled) BootPropsInstaller.enable(before) else BootPropsInstaller.disable()
            // The scripts on disk are the persisted decision, so read them back; the individual
            // resetprop calls can each succeed while the file never landed.
            val after = BootPropsInstaller.status()
            check(after.enabled == enabled) { "boot property scripts did not change" }
        }
    }

    override suspend fun getTeesimStatus(): Result<TeesimStatus> = withContext(Dispatchers.IO) {
        runCatching { TeesimInstaller.status() }
    }

    override suspend fun installTeesim(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            TeesimInstaller.install()
            check(TeesimInstaller.status().installed) { "TEESimulator module is not on disk" }
        }
    }

    override suspend fun setTeesimEnabled(enabled: Boolean): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                TeesimInstaller.setEnabled(enabled)
                // The marker is the persisted decision, so read it back instead of trusting the
                // write: that is what the next boot acts on.
                val after = TeesimInstaller.status()
                check(after.enabled == enabled) { "TEESimulator module state did not change" }
            }
        }

    override suspend fun uninstallTeesim(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            TeesimInstaller.uninstall()
            check(!TeesimInstaller.status().installed) { "TEESimulator module is still on disk" }
        }
    }

    override suspend fun importTeesimKeybox(base64: String): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching { TeesimInstaller.importKeybox(base64) }
        }

    override suspend fun removeTeesimKeybox(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            TeesimInstaller.removeKeybox()
            check(!TeesimInstaller.status().keyboxPresent) { "TEESimulator keybox is still present" }
        }
    }

    override suspend fun setTeesimTargetApps(
        packages: List<String>,
        autoInclude: Boolean,
    ): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                TeesimInstaller.setTargetApps(packages, autoInclude)
                val after = TeesimInstaller.status().config
                check(after.defaultApps.toSet() == packages.toSet()) {
                    "TEESimulator target apps did not change"
                }
                check(after.autoInclude == autoInclude) {
                    "TEESimulator auto-include did not change"
                }
            }
        }

    override suspend fun rescanTeesimTargets(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching { TeesimInstaller.rescan() }
    }

    override suspend fun getPifStatus(): Result<PifStatus> = withContext(Dispatchers.IO) {
        runCatching { PifInstaller.status() }
    }

    override suspend fun reinstallPif(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            PifInstaller.reinstall()
            val status = PifInstaller.status()
            check(status.installed && status.linked) { "PlayIntegrityFix is not linked" }
        }
    }

    override suspend fun setPifEnabled(enabled: Boolean): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                PifInstaller.setEnabled(enabled)
                // ksud acts on the marker at boot, so what the page must show is the marker's own
                // state rather than the write that produced it.
                check(PifInstaller.status().enabled == enabled) { "PlayIntegrityFix state did not change" }
            }
        }

    override suspend fun adoptPif(): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            PifInstaller.adopt()
            val status = PifInstaller.status()
            check(status.installed && status.linked) { "PlayIntegrityFix is not linked" }
        }
    }

    override suspend fun importPifProp(base64: String): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            PifInstaller.importProp(base64)
            // Read it back: the write is only as good as the file the library will parse.
            check(PifInstaller.config().fingerprint.isNotBlank()) { "pif.prop did not take" }
        }
    }

    override suspend fun setPifOption(option: PifOption, value: Boolean): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                PifInstaller.setOption(option, value)
                check(PifInstaller.config().isOn(option) == value) { "pif.prop did not change" }
            }
        }

    override suspend fun loadFeatureSnapshot(): FeatureSnapshot = withContext(Dispatchers.IO) {
        // One batched round trip: `feature list` reports every status and current value in a
        // single ksud run, and the four config reads and the JSON statuses ride the same job.
        // A failing command degrades to the same empty status / failed Result its individual
        // reader produces, so a partial batch is reported per feature instead of as a whole.
        val segments = execKsudBatch(
            listOf(
                "feature list",
                "feature get --config su_compat",
                "feature get --config sulog",
                "feature get --config adb_root",
                "feature get --config shell_su_adb_only",
            )
        )
        fun segment(index: Int) = segments.getOrNull(index)

        // `[ENABLED (1)] su_compat (ID=0) [FORCED]` / `[DISABLED] kernel_umount (ID=1)` /
        // `[NOT_SUPPORTED] avc_spoof (ID=6) [MODULE_MANAGED]`, one line per feature.
        val statuses = mutableMapOf<String, String>()
        val values = mutableMapOf<String, Long?>()
        val statusLine =
            Regex("\\[(NOT_SUPPORTED|ENABLED \\((\\d+)\\)|DISABLED)\\] (\\w+) \\(ID=\\d+\\)(.*)")
        segment(0)?.out?.forEach { line ->
            val match = statusLine.matchEntire(line.trim()) ?: return@forEach
            val state = match.groupValues[1]
            val marks = match.groupValues[4]
            statuses[match.groupValues[3]] = when {
                marks.contains("[FORCED]") -> "forced"
                marks.contains("[MODULE_MANAGED]") -> "managed"
                state == "NOT_SUPPORTED" -> "unsupported"
                else -> "supported"
            }
            values[match.groupValues[3]] = when (state) {
                "NOT_SUPPORTED" -> null
                "DISABLED" -> 0L
                else -> match.groupValues[2].toLongOrNull()
            }
        }

        fun persisted(index: Int): Long? = segment(index)?.out
            ?.firstOrNull { it.trim().startsWith("Value:") }
            ?.substringAfter("Value:")
            ?.trim()
            ?.toLongOrNull()

        FeatureSnapshot(
            statuses = statuses,
            values = values,
            persists = mapOf(
                "su_compat" to persisted(1),
                "sulog" to persisted(2),
                "adb_root" to persisted(3),
                "shell_su_adb_only" to persisted(4),
            ),
        )
    }

    override suspend fun setModuleSkip(id: String, skip: Boolean): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val flag = if (skip) "" else " --off"
            check(execKsud("module skip ${shellQuote(id)}$flag", true)) {
                "ksud module skip failed"
            }
        }
    }

    override suspend fun getAdbRootStatus(): String = getFeatureStatus("adb_root")

    override suspend fun getShellSuAdbOnlyStatus(): String = getFeatureStatus("shell_su_adb_only")
    override suspend fun getShellSuAdbOnlyPersistValue(): Long? = getFeaturePersistValue("shell_su_adb_only")
    override suspend fun getShellSuAdbOnlyValue(): Long? = getFeatureValue("shell_su_adb_only")
    override fun setShellSuAdbOnlyEnabled(enabled: Boolean): Boolean =
        execKsud("feature set shell_su_adb_only ${if (enabled) 1 else 0}", true)

    override suspend fun getAdbRootPersistValue(): Long? = getFeaturePersistValue("adb_root")
    override suspend fun getAdbRootValue(): Long? = getFeatureValue("adb_root")

    override fun setAdbRootEnabled(enabled: Boolean): Boolean =
        if (execKsud("feature set adb_root ${if (enabled) 1 else 0}", true)) {
            ShellUtils.fastCmd("setprop ctl.restart adbd")
            true
        } else {
            false
        }

    override fun isDefaultUmountModules(): Boolean = Natives.isDefaultUmountModules()

    override fun setDefaultUmountModules(enabled: Boolean): Boolean = Natives.setDefaultUmountModules(enabled)

    override fun isLkmMode(): Boolean = Natives.isLkmMode

    override fun execKsudFeatureSave(): Boolean = execKsud("feature save", true)
}
