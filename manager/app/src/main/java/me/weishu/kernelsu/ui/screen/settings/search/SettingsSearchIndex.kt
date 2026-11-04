package me.weishu.kernelsu.ui.screen.settings.search

import androidx.annotation.StringRes
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.automirrored.filled.Rule
import androidx.compose.material.icons.automirrored.rounded.MenuOpen
import androidx.compose.material.icons.filled.AdminPanelSettings
import androidx.compose.material.icons.filled.Adb
import androidx.compose.material.icons.filled.Backup
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.DeveloperMode
import androidx.compose.material.icons.filled.Extension
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.LayersClear
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.SystemUpdateAlt
import androidx.compose.material.icons.filled.Translate
import androidx.compose.material.icons.filled.Vaccines
import androidx.compose.material.icons.filled.VerifiedUser
import androidx.compose.material.icons.outlined.Cloud
import androidx.compose.material.icons.rounded.AspectRatio
import androidx.compose.material.icons.rounded.Bedtime
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Pin
import androidx.compose.material.icons.rounded.RoundedCorner
import androidx.compose.material.icons.automirrored.rounded.LabelOff
import androidx.compose.material.icons.rounded.SpaceDashboard
import androidx.compose.material.icons.rounded.Style
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material.icons.rounded.SwipeUp
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalResources
import me.weishu.kernelsu.R
import me.weishu.kernelsu.media.VisualMediaConfig
import me.weishu.kernelsu.ui.component.settings.SettingsCategory
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.screen.settings.SettingsUiState

/** Where a search result lands when its row is tapped. */
sealed interface SettingsSearchTarget {
    /** A control that lives on one of the category screens. */
    data class Category(val key: String) : SettingsSearchTarget

    /** A page of its own, opened directly. */
    data class Page(val route: Route) : SettingsSearchTarget

    /** An action the settings hub owns: a dialog, a sheet or an external link. */
    data class Hub(val action: SettingsHubAction) : SettingsSearchTarget
}

/** The rows the hub itself owns, which have no page of their own to push. */
enum class SettingsHubAction { BUG_FEEDBACK, SEND_LOG, UNINSTALL }

/**
 * One searchable settings row.
 *
 * The index is the single source of truth for what the search can find, so a row added to a
 * category screen has to be registered here too. [titleRes] and [summaryRes] point at the very
 * strings the category screen draws, [icon] is the same icon, and [available] mirrors the
 * visibility condition the screen applies, so the search never offers a row the device cannot show.
 */
data class SettingsSearchEntry(
    val id: String,
    @StringRes val titleRes: Int,
    val icon: ImageVector,
    val target: SettingsSearchTarget,
    val category: SettingsCategory? = null,
    @StringRes val summaryRes: Int = 0,
    @StringRes val parentTitleRes: Int = category?.titleRes ?: R.string.settings,
    val aliases: List<String> = emptyList(),
    val available: (SettingsUiState) -> Boolean = { true },
    /**
     * A row that only exists while the floating navigation bar is on screen. The form lives
     * outside the settings state, so it is declared here and resolved by the caller rather than
     * read from inside the predicate.
     */
    val requiresFloatingBar: Boolean = false,
)

/** A row on a category screen: tapping it opens that category. */
private fun SettingsCategory.row(
    id: String,
    @StringRes titleRes: Int,
    icon: ImageVector,
    @StringRes summaryRes: Int = 0,
    aliases: List<String> = emptyList(),
    available: (SettingsUiState) -> Boolean = { true },
    requiresFloatingBar: Boolean = false,
) = SettingsSearchEntry(
    id = id,
    titleRes = titleRes,
    icon = icon,
    target = SettingsSearchTarget.Category(key),
    category = this,
    summaryRes = summaryRes,
    aliases = aliases,
    available = available,
    requiresFloatingBar = requiresFloatingBar,
)

/** A row that opens a page of its own, filed under the category that links to it. */
private fun SettingsCategory.page(
    id: String,
    @StringRes titleRes: Int,
    icon: ImageVector,
    route: Route,
    @StringRes summaryRes: Int = 0,
    aliases: List<String> = emptyList(),
    available: (SettingsUiState) -> Boolean = { true },
) = SettingsSearchEntry(
    id = id,
    titleRes = titleRes,
    icon = icon,
    target = SettingsSearchTarget.Page(route),
    category = this,
    summaryRes = summaryRes,
    aliases = aliases,
    available = available,
)

/** A row the hub draws itself, so it is filed under the settings root. */
private fun hubRow(
    id: String,
    @StringRes titleRes: Int,
    icon: ImageVector,
    target: SettingsSearchTarget,
    @StringRes summaryRes: Int = 0,
    aliases: List<String> = emptyList(),
    available: (SettingsUiState) -> Boolean = { true },
) = SettingsSearchEntry(
    id = id,
    titleRes = titleRes,
    icon = icon,
    target = target,
    summaryRes = summaryRes,
    aliases = aliases,
    available = available,
)

/**
 * Every row the settings search can find, in the order the pages present them.
 *
 * Ordered by category so the result groups read the same way as the hub, and within a category so
 * equally scored rows keep the page's own order.
 */
val settingsSearchIndex: List<SettingsSearchEntry> = buildList {
    // General
    add(
        SettingsCategory.GENERAL.row(
            id = "general.check_update",
            titleRes = R.string.settings_check_update,
            summaryRes = R.string.settings_check_update_summary,
            icon = Icons.Filled.SystemUpdate,
            aliases = listOf("update", "ota", "release"),
        )
    )
    add(
        SettingsCategory.GENERAL.row(
            id = "general.check_module_update",
            titleRes = R.string.settings_module_check_update,
            summaryRes = R.string.settings_check_update_summary,
            icon = Icons.Filled.SystemUpdateAlt,
            aliases = listOf("update", "module", "repo"),
        )
    )
    add(
        SettingsCategory.GENERAL.page(
            id = "general.profile_template",
            titleRes = R.string.settings_profile_template,
            summaryRes = R.string.settings_profile_template_summary,
            icon = Icons.Filled.Description,
            route = Route.AppProfileTemplate,
            aliases = listOf("profile", "template", "per app"),
        )
    )
    add(
        SettingsCategory.GENERAL.page(
            id = "general.language",
            titleRes = R.string.settings_app_language,
            summaryRes = R.string.settings_app_language_summary,
            icon = Icons.Filled.Translate,
            route = Route.Language,
            aliases = listOf("language", "locale", "english", "chinese"),
        )
    )

    // Appearance
    add(
        SettingsCategory.APPEARANCE.page(
            id = "appearance.theme",
            titleRes = R.string.settings_theme,
            summaryRes = R.string.settings_theme_summary,
            icon = Icons.Filled.Palette,
            route = Route.ColorPalette,
            aliases = listOf("color", "colour", "palette", "monet", "dynamic color", "amoled", "dark"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.hide_status_tag",
            titleRes = R.string.settings_home_hide_status_tag,
            summaryRes = R.string.settings_home_hide_status_tag_summary,
            icon = Icons.AutoMirrored.Rounded.LabelOff,
            aliases = listOf("status", "tag", "badge", "mode", "hide"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.title_image",
            titleRes = R.string.media_title_image,
            summaryRes = R.string.media_title_image_summary,
            icon = Icons.Rounded.Image,
            aliases = listOf("title", "image", "header", "photo"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.title_offset",
            titleRes = R.string.media_title_offset,
            icon = Icons.Rounded.SwapHoriz,
            aliases = listOf("title", "offset", "position"),
            available = { VisualMediaConfig.titleEnabled },
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.hide_home_fingerprint",
            titleRes = R.string.settings_hide_home_fingerprint,
            summaryRes = R.string.settings_hide_home_fingerprint_summary,
            icon = Icons.Filled.Fingerprint,
            aliases = listOf("home", "fingerprint", "hide", "card"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.hide_home_zygisk",
            titleRes = R.string.settings_hide_home_zygisk,
            summaryRes = R.string.settings_hide_home_zygisk_summary,
            icon = Icons.Filled.Vaccines,
            aliases = listOf("home", "zygisk", "hide", "card"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.hide_home_susfs",
            titleRes = R.string.settings_hide_home_susfs,
            summaryRes = R.string.settings_hide_home_susfs_summary,
            icon = Icons.Rounded.VisibilityOff,
            aliases = listOf("home", "susfs", "hide", "card"),
            available = { it.susfsDetected },
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.hide_home_kernel_full_version",
            titleRes = R.string.settings_hide_home_kernel_full_version,
            summaryRes = R.string.settings_hide_home_kernel_full_version_summary,
            icon = Icons.Filled.DeveloperBoard,
            aliases = listOf("home", "kernel", "build", "version", "full", "hide", "card"),
            available = { it.kernelFullVersionAvailable },
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.page_scale",
            titleRes = R.string.settings_page_scale,
            summaryRes = R.string.settings_page_scale_summary,
            icon = Icons.Rounded.AspectRatio,
            aliases = listOf("scale", "density", "size", "zoom"),
        )
    )
    add(
        SettingsCategory.APPEARANCE.row(
            id = "appearance.module_description_lines",
            titleRes = R.string.settings_module_description_max_lines,
            summaryRes = R.string.settings_module_description_max_lines_summary,
            icon = Icons.Rounded.Description,
            aliases = listOf("module", "description", "lines", "collapse"),
        )
    )

    // Behavior
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.navigation_badge",
            titleRes = R.string.settings_navigation_badge,
            summaryRes = R.string.settings_navigation_badge_summary,
            icon = Icons.Rounded.Pin,
            aliases = listOf("navigation", "badge", "dot"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.predictive_back",
            titleRes = R.string.settings_enable_predictive_back,
            summaryRes = R.string.settings_enable_predictive_back_summary,
            icon = Icons.AutoMirrored.Rounded.MenuOpen,
            aliases = listOf("predictive back", "gesture", "back"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.nav_mode",
            titleRes = R.string.settings_nav_mode,
            summaryRes = R.string.settings_nav_mode_summary,
            icon = Icons.Rounded.SpaceDashboard,
            aliases = listOf("navigation", "bar", "rail", "bottom", "floating"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.nav_show_superuser",
            titleRes = R.string.settings_show_superuser,
            summaryRes = R.string.settings_nav_layout_title,
            icon = Icons.Filled.Shield,
            aliases = listOf("navigation", "show", "hide", "superuser", "root"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.nav_show_module",
            titleRes = R.string.settings_show_module,
            summaryRes = R.string.settings_nav_layout_title,
            icon = Icons.Filled.Extension,
            aliases = listOf("navigation", "show", "hide", "module"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.floating_bar_style",
            titleRes = R.string.settings_floating_bar_style,
            icon = Icons.Rounded.Style,
            aliases = listOf("floating", "bar", "style", "drawer"),
            requiresFloatingBar = true,
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.floating_bar_compact",
            titleRes = R.string.settings_floating_bar_compact,
            icon = Icons.Rounded.RoundedCorner,
            aliases = listOf("floating", "compact", "compact bar"),
            requiresFloatingBar = true,
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.floating_auto_hide",
            titleRes = R.string.settings_floating_auto_hide,
            summaryRes = R.string.settings_floating_auto_hide_summary,
            icon = Icons.Rounded.VisibilityOff,
            aliases = listOf("floating", "auto hide", "hide"),
            requiresFloatingBar = true,
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.floating_swipe_hide",
            titleRes = R.string.settings_floating_swipe_hide,
            summaryRes = R.string.settings_floating_swipe_hide_summary,
            icon = Icons.Rounded.SwipeUp,
            aliases = listOf("floating", "swipe", "hide"),
            requiresFloatingBar = true,
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.sucompat",
            titleRes = R.string.settings_sucompat,
            summaryRes = R.string.settings_sucompat_summary,
            icon = Icons.Filled.AdminPanelSettings,
            aliases = listOf("su", "compat", "compatibility", "legacy"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.soft_reboot",
            titleRes = R.string.settings_soft_reboot,
            summaryRes = R.string.settings_soft_reboot_summary,
            icon = Icons.Filled.RestartAlt,
            aliases = listOf("reboot", "soft", "restart"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.web_debugging",
            titleRes = R.string.enable_web_debugging,
            summaryRes = R.string.enable_web_debugging_summary,
            icon = Icons.Filled.DeveloperMode,
            aliases = listOf("webui", "debug", "webview", "devtools"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.auto_jailbreak",
            titleRes = R.string.settings_auto_jailbreak,
            summaryRes = R.string.settings_auto_jailbreak_summary,
            icon = Icons.Filled.FlashOn,
            aliases = listOf("jailbreak", "auto", "late load"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.module_card_show_more",
            titleRes = R.string.module_card_show_more_info,
            summaryRes = R.string.module_card_show_more_info_summary,
            icon = Icons.Filled.Description,
            aliases = listOf("module", "card", "info"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.module_card_fold_system",
            titleRes = R.string.module_card_fold_system,
            summaryRes = R.string.module_card_fold_system_summary,
            icon = Icons.Filled.Layers,
            aliases = listOf("module", "card", "system", "fold"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.module_card_spliced",
            titleRes = R.string.module_card_spliced,
            summaryRes = R.string.module_card_spliced_summary,
            icon = Icons.Filled.GridView,
            aliases = listOf("module", "card", "spliced", "group"),
        )
    )
    add(
        SettingsCategory.BEHAVIOR.row(
            id = "behavior.module_card_button_style",
            titleRes = R.string.module_card_button_style,
            summaryRes = R.string.module_card_button_style_summary,
            icon = Icons.Filled.Palette,
            aliases = listOf("module", "card", "button", "style"),
        )
    )
    // Function
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.kernel_umount",
            titleRes = R.string.settings_kernel_umount,
            summaryRes = R.string.settings_kernel_umount_summary,
            icon = Icons.Filled.LayersClear,
            aliases = listOf("umount", "unmount", "module", "hide"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.umount_modules_default",
            titleRes = R.string.settings_umount_modules_default,
            summaryRes = R.string.settings_umount_modules_default_summary,
            icon = Icons.AutoMirrored.Filled.Rule,
            aliases = listOf("umount", "unmount", "default", "app"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.avc_spoof",
            titleRes = R.string.settings_avc_spoof,
            summaryRes = R.string.settings_avc_spoof_summary,
            icon = Icons.Filled.Security,
            aliases = listOf("avc", "selinux", "spoof", "denial"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.sulog",
            titleRes = R.string.settings_sulog,
            summaryRes = R.string.settings_sulog_summary,
            icon = Icons.AutoMirrored.Filled.Article,
            aliases = listOf("su", "log", "logging", "history"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.dynamic_manager",
            titleRes = R.string.allow_any_dynamic_manager,
            summaryRes = R.string.allow_any_dynamic_manager_summary,
            icon = Icons.Filled.AdminPanelSettings,
            aliases = listOf("dynamic manager", "manager", "superkey"),
            available = { it.isDynamicManagerAvailable },
        )
    )
    add(
        SettingsCategory.FUNCTION.page(
            id = "function.yukizygisk",
            titleRes = R.string.yukizygisk_title,
            summaryRes = R.string.yukizygisk_summary,
            icon = Icons.Filled.Vaccines,
            route = Route.YukiZygisk,
            aliases = listOf("yukizygisk", "zygisk", "injection", "注入"),
        )
    )
    add(
        SettingsCategory.FUNCTION.page(
            id = "function.teesim",
            titleRes = R.string.settings_teesim,
            icon = Icons.Filled.VerifiedUser,
            route = Route.Teesim,
            aliases = listOf("teesim", "attestation", "keybox", "keymint", "tee", "integrity"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.boot_props",
            titleRes = R.string.settings_boot_props,
            summaryRes = R.string.boot_props_search_summary,
            icon = Icons.Rounded.Lock,
            aliases = listOf(
                "bootloader",
                "verifiedboot",
                "flash.locked",
                "vbmeta",
                "orange",
                "integrity",
                "attestation",
            ),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.rekernel_x",
            titleRes = R.string.settings_rekernel_x,
            summaryRes = R.string.rekernel_x_search_summary,
            icon = Icons.Rounded.Bedtime,
            aliases = listOf("rekernel", "tombstone", "freeze", "frozen", "binder"),
        )
    )

    // Security
    add(
        SettingsCategory.SECURITY.row(
            id = "security.selinux_hide",
            titleRes = R.string.settings_selinux_hide,
            summaryRes = R.string.settings_selinux_hide_summary,
            icon = Icons.Filled.Security,
            aliases = listOf("selinux", "hide", "spoof"),
        )
    )
    add(
        SettingsCategory.FUNCTION.row(
            id = "function.mount_view_filter",
            titleRes = R.string.settings_mount_view_filter,
            summaryRes = R.string.settings_mount_view_filter_summary,
            icon = Icons.Rounded.VisibilityOff,
            aliases = listOf("mount", "hide", "proc", "mountinfo", "mounts", "挂载"),
        )
    )
    add(
        SettingsCategory.SECURITY.row(
            id = "security.selinux_mode",
            titleRes = R.string.settings_selinux_mode,
            icon = Icons.Filled.Lock,
            aliases = listOf("selinux", "enforcing", "permissive", "mode"),
        )
    )
    add(
        SettingsCategory.SECURITY.row(
            id = "security.adb_root",
            titleRes = R.string.settings_adb_root,
            summaryRes = R.string.settings_adb_root_summary,
            icon = Icons.Filled.Adb,
            aliases = listOf("adb", "root", "debug"),
        )
    )
    add(
        SettingsCategory.SECURITY.row(
            id = "security.shell_su_adb_only",
            titleRes = R.string.settings_shell_su_adb_only,
            summaryRes = R.string.settings_shell_su_adb_only_summary,
            icon = Icons.Filled.VerifiedUser,
            aliases = listOf("shell", "su", "adb", "terminal"),
        )
    )

    // Backup
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.local_enabled",
            titleRes = R.string.backup_local_enabled,
            summaryRes = R.string.backup_local_summary,
            icon = Icons.Filled.Folder,
            aliases = listOf("backup", "local"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.webdav_enabled",
            titleRes = R.string.backup_webdav_enabled,
            icon = Icons.Filled.Cloud,
            aliases = listOf("backup", "webdav", "cloud", "server"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.open_folder",
            titleRes = R.string.backup_open_folder,
            icon = Icons.Filled.FolderOpen,
            aliases = listOf("backup", "folder", "downloads"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.before_flash",
            titleRes = R.string.backup_before_flash,
            summaryRes = R.string.backup_before_flash_summary,
            icon = Icons.Filled.FlashOn,
            aliases = listOf("backup", "flash", "zip"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.modules",
            titleRes = R.string.backup_modules,
            summaryRes = R.string.backup_modules_summary,
            icon = Icons.Filled.Extension,
            aliases = listOf("backup", "module"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.allowlist",
            titleRes = R.string.backup_allowlist,
            icon = Icons.AutoMirrored.Filled.Rule,
            aliases = listOf("backup", "allowlist", "superuser"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.now",
            titleRes = R.string.backup_now,
            icon = Icons.Filled.Backup,
            aliases = listOf("backup", "now", "run"),
        )
    )
    add(
        SettingsCategory.BACKUP.page(
            id = "backup.list",
            titleRes = R.string.backup_open_list,
            icon = Icons.AutoMirrored.Filled.List,
            route = Route.BackupList,
            aliases = listOf("backup", "list", "history", "restore"),
        )
    )
    add(
        SettingsCategory.BACKUP.row(
            id = "backup.webdav_test",
            titleRes = R.string.backup_webdav_test,
            icon = Icons.Filled.Cloud,
            aliases = listOf("webdav", "test", "connection"),
        )
    )
    add(
        SettingsCategory.BACKUP.page(
            id = "backup.webdav_browse",
            titleRes = R.string.backup_webdav_browse,
            icon = Icons.Filled.FolderOpen,
            route = Route.WebDavBrowser(),
            aliases = listOf("webdav", "browse", "folder", "remote"),
        )
    )

    // The rows the hub itself owns.
    add(
        hubRow(
            id = "hub.bug_feedback",
            titleRes = R.string.settings_bug_feedback,
            icon = Icons.Filled.BugReport,
            target = SettingsSearchTarget.Hub(SettingsHubAction.BUG_FEEDBACK),
            aliases = listOf("bug", "feedback", "issue", "report"),
        )
    )
    add(
        hubRow(
            id = "hub.send_log",
            titleRes = R.string.send_log,
            icon = Icons.Filled.Description,
            target = SettingsSearchTarget.Hub(SettingsHubAction.SEND_LOG),
            aliases = listOf("log", "send", "debug", "report"),
        )
    )
    add(
        hubRow(
            id = "hub.about",
            titleRes = R.string.about,
            icon = Icons.Filled.Info,
            target = SettingsSearchTarget.Page(Route.About),
            aliases = listOf("about", "version", "credits", "license"),
        )
    )
    add(
        hubRow(
            id = "hub.uninstall",
            titleRes = R.string.settings_uninstall,
            icon = Icons.Filled.Delete,
            target = SettingsSearchTarget.Hub(SettingsHubAction.UNINSTALL),
            aliases = listOf("uninstall", "remove"),
            available = { it.isLkmMode },
        )
    )
}

/**
 * Resolves the index in the current locale.
 *
 * The folded copies the matcher works on are built here, so typing only ever folds the query.
 */
@Composable
fun rememberSettingsSearchEntries(): List<ResolvedSettingsEntry> {
    val resources = LocalResources.current
    return remember(resources) {
        settingsSearchIndex.map { entry ->
            resolveEntry(
                entry = entry,
                title = resources.getString(entry.titleRes),
                summary = entry.summaryRes.takeIf { it != 0 }?.let(resources::getString).orEmpty(),
                parentTitle = resources.getString(entry.parentTitleRes),
            )
        }
    }
}

