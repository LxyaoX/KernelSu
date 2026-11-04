package me.weishu.kernelsu.ui.screen.settings

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.filled.Rule
import androidx.compose.material.icons.filled.Adb
import androidx.compose.material.icons.filled.AdminPanelSettings
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.DeveloperMode
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.LayersClear
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.SystemUpdateAlt
import androidx.compose.material.icons.filled.Translate
import androidx.compose.material.icons.filled.Vaccines
import androidx.compose.material.icons.filled.VerifiedUser
import androidx.compose.material.icons.automirrored.rounded.LabelOff
import androidx.compose.material.icons.rounded.SpaceDashboard
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.component.KsuIsValid
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedDropdownItem
import me.weishu.kernelsu.ui.component.material.SegmentedListItem
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference
import top.yukonga.miuix.kmp.preference.ArrowPreference

/**
 * Contents of each settings category, split out of the old flat hub. Every function emits its own
 * [SegmentedColumn] groups; the hosting screen owns the scroll container, so these stay layout-free
 * and mirror each other's spacing.
 */

private val GroupModifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp)

/** Which text of the custom home card the shared text dialog is currently editing. */
@Composable
fun GeneralCategoryContent(uiState: SettingsUiState, actions: SettingsScreenActions) {
    KsuIsValid {
        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf(
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_check_update),
                        summary = stringResource(id = R.string.settings_check_update_summary),
                        startAction = { Icon(Icons.Filled.SystemUpdate, contentDescription = null) },
                        checked = uiState.checkUpdate,
                        onCheckedChange = actions.onSetCheckUpdate,
                    )
                },
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_module_check_update),
                        summary = stringResource(id = R.string.settings_check_update_summary),
                        startAction = { Icon(Icons.Filled.SystemUpdateAlt, contentDescription = null) },
                        checked = uiState.checkModuleUpdate,
                        onCheckedChange = actions.onSetCheckModuleUpdate,
                    )
                }
            )
        )
    }

    val profileTemplate = stringResource(id = R.string.settings_profile_template)
    KsuIsValid {
        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf {
                ArrowPreference(
                    title = profileTemplate,
                    summary = stringResource(id = R.string.settings_profile_template_summary),
                    startAction = { Icon(Icons.Filled.Description, profileTemplate) },
                    onClick = actions.onOpenProfileTemplate,
                )
            }
        )
    }

    val appLanguage = stringResource(id = R.string.settings_app_language)
    SegmentedColumn(
        modifier = GroupModifier,
        content = listOf {
            ArrowPreference(
                title = appLanguage,
                summary = stringResource(id = R.string.settings_app_language_summary),
                startAction = { Icon(Icons.Filled.Translate, appLanguage) },
                onClick = actions.onOpenLanguage,
            )
        }
    )
}

@Composable
fun AppearanceCategoryContent(uiState: SettingsUiState, actions: SettingsScreenActions) {
    SegmentedColumn(
        modifier = GroupModifier,
        content = buildList {
            add {
                ArrowPreference(
                    title = stringResource(id = R.string.settings_theme),
                    summary = stringResource(id = R.string.settings_theme_summary),
                    startAction = { Icon(Icons.Filled.Palette, stringResource(id = R.string.settings_theme)) },
                    onClick = actions.onOpenTheme,
                )
            }
        }
    )

    // Layout gathers the home screen's shape and the search pages' title treatment.
    SegmentedColumn(
        modifier = GroupModifier,
        title = stringResource(R.string.settings_section_layout),
    ) {
        item {
            SwitchPreference(
                title = stringResource(id = R.string.settings_home_hide_status_tag),
                summary = stringResource(id = R.string.settings_home_hide_status_tag_summary),
                startAction = { Icon(Icons.AutoMirrored.Rounded.LabelOff, contentDescription = null) },
                checked = uiState.listWorkingCardModeHidden,
                onCheckedChange = actions.onSetListWorkingCardModeHidden,
            )
        }
    }

    AdvancedTitleSettings(modifier = GroupModifier)

    // The identifying rows only, mirroring FP: the base facts of the card stay visible everywhere.
    // The Zygisk row is always there now - the engine ships with the build, so there is no
    // configuration in which the home card cannot show it.
    SegmentedColumn(
        modifier = GroupModifier,
        title = stringResource(R.string.settings_home_info),
    ) {
        item {
            SwitchPreference(
                title = stringResource(R.string.settings_hide_home_fingerprint),
                summary = stringResource(R.string.settings_hide_home_fingerprint_summary),
                startAction = { Icon(Icons.Filled.Fingerprint, contentDescription = null) },
                checked = uiState.hideHomeFingerprint,
                onCheckedChange = actions.onSetHideHomeFingerprint,
            )
        }
        item {
            SwitchPreference(
                title = stringResource(R.string.settings_hide_home_zygisk),
                summary = stringResource(R.string.settings_hide_home_zygisk_summary),
                startAction = { Icon(Icons.Filled.Vaccines, contentDescription = null) },
                checked = uiState.hideHomeZygisk,
                onCheckedChange = actions.onSetHideHomeZygisk,
            )
        }
        item(visible = uiState.susfsDetected) {
            SwitchPreference(
                title = stringResource(R.string.settings_hide_home_susfs),
                summary = stringResource(R.string.settings_hide_home_susfs_summary),
                startAction = { Icon(Icons.Rounded.VisibilityOff, contentDescription = null) },
                checked = uiState.hideHomeSusfs,
                onCheckedChange = actions.onSetHideHomeSusfs,
            )
        }
        item(visible = uiState.kernelFullVersionAvailable) {
            SwitchPreference(
                title = stringResource(R.string.settings_hide_home_kernel_full_version),
                summary = stringResource(R.string.settings_hide_home_kernel_full_version_summary),
                startAction = { Icon(Icons.Filled.DeveloperBoard, contentDescription = null) },
                checked = uiState.hideHomeKernelFullVersion,
                onCheckedChange = actions.onSetHideHomeKernelFullVersion,
            )
        }
    }

    DisplayDensitySection(uiState, actions)
}

@Composable
fun BehaviorCategoryContent(uiState: SettingsUiState, actions: SettingsScreenActions) {
    NavigationSettingsSection(uiState, actions)

    KsuIsValid {
        val suCompatModeItems = listOf(
            stringResource(id = R.string.settings_mode_enable_by_default),
            stringResource(id = R.string.settings_mode_disable_until_reboot),
            stringResource(id = R.string.settings_mode_disable_always),
        )

        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf(
                {
                    val suSummary = when (uiState.suCompatStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_sucompat_summary)
                    }
                    OverlayDropdownPreference(
                        title = stringResource(id = R.string.settings_sucompat),
                        summary = suSummary,
                        items = suCompatModeItems,
                        startAction = { Icon(Icons.Filled.AdminPanelSettings, contentDescription = null) },
                        selectedIndex = uiState.suCompatMode,
                        onSelectedIndexChange = actions.onSetSuCompatMode,
                        enabled = uiState.suCompatStatus == "supported",
                    )
                },
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_soft_reboot),
                        summary = stringResource(id = R.string.settings_soft_reboot_summary),
                        startAction = { Icon(Icons.Filled.RestartAlt, contentDescription = null) },
                        checked = uiState.isLateLoadMode || uiState.useSoftReboot,
                        onCheckedChange = actions.onSetUseSoftReboot,
                        enabled = !uiState.isLateLoadMode,
                    )
                },
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.enable_web_debugging),
                        summary = stringResource(id = R.string.enable_web_debugging_summary),
                        startAction = { Icon(Icons.Filled.DeveloperMode, contentDescription = null) },
                        checked = uiState.enableWebDebugging,
                        onCheckedChange = actions.onSetEnableWebDebugging,
                    )
                },
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_auto_jailbreak),
                        summary = stringResource(id = R.string.settings_auto_jailbreak_summary),
                        startAction = { Icon(Icons.Filled.FlashOn, contentDescription = null) },
                        checked = uiState.autoJailbreak,
                        onCheckedChange = actions.onSetAutoJailbreak,
                        enabled = uiState.isLateLoadMode,
                    )
                }
            )
        )
    }

    ModuleCardSettingsSection()
}

@Composable
fun FunctionCategoryContent(uiState: SettingsUiState, actions: SettingsScreenActions) {
    val navigator = LocalNavigator.current
    KsuIsValid {
        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf(
                {
                    val umountSummary = when (uiState.kernelUmountStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_kernel_umount_summary)
                    }
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_kernel_umount),
                        summary = umountSummary,
                        startAction = { Icon(Icons.Filled.LayersClear, contentDescription = null) },
                        checked = uiState.isKernelUmountEnabled,
                        onCheckedChange = actions.onSetKernelUmountEnabled,
                        enabled = uiState.kernelUmountStatus == "supported",
                    )
                },
                {
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_umount_modules_default),
                        summary = stringResource(id = R.string.settings_umount_modules_default_summary),
                        startAction = { Icon(Icons.AutoMirrored.Filled.Rule, contentDescription = null) },
                        checked = uiState.isDefaultUmountModules,
                        onCheckedChange = actions.onSetDefaultUmountModules,
                    )
                },
                {
                    val mountViewFilterSummary = when (uiState.mountViewFilterStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(R.string.feature_status_managed_summary)
                        "unavailable" -> stringResource(R.string.settings_mount_view_filter_unavailable)
                        else -> stringResource(R.string.settings_mount_view_filter_summary)
                    }
                    SwitchPreference(
                        title = stringResource(R.string.settings_mount_view_filter),
                        summary = mountViewFilterSummary,
                        startAction = { Icon(Icons.Rounded.VisibilityOff, contentDescription = null) },
                        checked = uiState.isMountViewFilterEnabled,
                        onCheckedChange = actions.onSetMountViewFilterEnabled,
                        enabled = uiState.mountViewFilterStatus == "supported" &&
                                  !uiState.isMountViewFilterWriting,
                    )
                },
                {
                    val avcSpoofSummary = when (uiState.avcSpoofStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_avc_spoof_summary)
                    }
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_avc_spoof),
                        summary = avcSpoofSummary,
                        startAction = { Icon(Icons.Filled.Security, contentDescription = null) },
                        checked = uiState.isAvcSpoofEnabled,
                        onCheckedChange = actions.onSetAvcSpoofEnabled,
                        enabled = uiState.avcSpoofStatus == "supported" && !uiState.isAvcSpoofWriting,
                    )
                },
                {
                    val sulogSummary = when (uiState.sulogStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_sulog_summary)
                    }
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_sulog),
                        summary = sulogSummary,
                        startAction = { Icon(Icons.AutoMirrored.Filled.Article, contentDescription = null) },
                        checked = uiState.isSulogEnabled,
                        onCheckedChange = actions.onSetSulogEnabled,
                        enabled = uiState.sulogStatus == "supported",
                    )
                }
            )
        )

        // §6.1: hide the dynamic-manager toggle entirely when the kernel cannot support it, instead
        // of offering a switch that does nothing.
        if (uiState.isDynamicManagerAvailable) {
            SegmentedColumn(
                modifier = GroupModifier,
                content = listOf {
                    SwitchPreference(
                        title = stringResource(id = R.string.allow_any_dynamic_manager),
                        summary = stringResource(id = R.string.allow_any_dynamic_manager_summary),
                        startAction = { Icon(Icons.Filled.AdminPanelSettings, contentDescription = null) },
                        checked = uiState.allowAnyDynamicManager,
                        onCheckedChange = actions.onSetAllowAnyDynamicManager,
                    )
                }
            )
        }

        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf(
                {
                    ArrowPreference(
                        title = stringResource(id = R.string.yukizygisk_title),
                        summary = stringResource(id = R.string.yukizygisk_summary),
                        startAction = { Icon(Icons.Filled.Vaccines, stringResource(id = R.string.yukizygisk_title)) },
                        onClick = { navigator.push(Route.YukiZygisk) },
                    )
                },
                {
                    // ReKernel-X is an external LKM: the row owns its module markers only, so
                    // tombstone tooling keeps working across manager restarts and reboots.
                    RekernelXStatusItem(uiState, actions)
                },
                {
                    // The boot state the bootloader reported is the cheapest tell a detector
                    // has; ksud's built-in resetprop lets the manager correct it without a module.
                    BootPropsItem(uiState, actions)
                },
                {
                    // TEESimulator keeps its state on disk, not in the settings snapshot: this row
                    // is a door, and the page reads the module itself when it opens.
                    ArrowPreference(
                        title = stringResource(id = R.string.settings_teesim),
                        summary = stringResource(id = R.string.teesim_search_summary),
                        startAction = { Icon(Icons.Filled.VerifiedUser, null) },
                        onClick = actions.onOpenTeesim,
                    )
                },
            )
        )
    }
}

@Composable
fun SecurityCategoryContent(uiState: SettingsUiState, actions: SettingsScreenActions) {
    var showSELinuxModeDialog by remember { mutableStateOf(false) }
    KsuIsValid {
        SegmentedColumn(
            modifier = GroupModifier,
            content = listOf(
                {
                    val selinuxHideSummary = when (uiState.selinuxHideStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_selinux_hide_summary)
                    }
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_selinux_hide),
                        summary = selinuxHideSummary,
                        startAction = { Icon(Icons.Filled.Security, contentDescription = null) },
                        checked = uiState.isSelinuxHideEnabled,
                        onCheckedChange = actions.onSetSelinuxHideEnabled,
                        enabled = uiState.selinuxHideStatus == "supported",
                    )
                },
                {
                    val modeDisplay = when (uiState.selinuxMode) {
                        "Enforcing" -> stringResource(R.string.settings_selinux_mode_enforcing)
                        "Permissive" -> stringResource(R.string.settings_selinux_mode_permissive)
                        "Disabled" -> stringResource(R.string.selinux_status_disabled)
                        else -> stringResource(R.string.selinux_status_unknown)
                    }
                    val modeTitle = stringResource(R.string.settings_selinux_mode)
                    ArrowPreference(
                        title = modeTitle,
                        summary = stringResource(R.string.settings_selinux_current_mode, modeDisplay),
                        startAction = { Icon(Icons.Filled.Lock, modeTitle) },
                        onClick = { showSELinuxModeDialog = true },
                        enabled = !uiState.isSELinuxModeWriting,
                    )
                },
                {
                    val adbRootSummary = when (uiState.adbRootStatus) {
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        "unsupported" -> stringResource(id = R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(id = R.string.feature_status_managed_summary)
                        else -> stringResource(id = R.string.settings_adb_root_summary)
                    }
                    SwitchPreference(
                        title = stringResource(id = R.string.settings_adb_root),
                        summary = adbRootSummary,
                        startAction = { Icon(Icons.Filled.Adb, contentDescription = null) },
                        checked = uiState.isAdbRootEnabled,
                        onCheckedChange = actions.onSetAdbRootEnabled,
                        enabled = uiState.adbRootStatus == "supported",
                    )
                },
                {
                    val summary = when (uiState.shellSuAdbOnlyStatus) {
                        "unsupported" -> stringResource(R.string.feature_status_unsupported_summary)
                        "managed" -> stringResource(R.string.feature_status_managed_summary)
                        "forced" -> stringResource(R.string.feature_status_forced_summary)
                        else -> stringResource(R.string.settings_shell_su_adb_only_summary)
                    }
                    SwitchPreference(
                        title = stringResource(R.string.settings_shell_su_adb_only),
                        summary = summary,
                        startAction = { Icon(Icons.Filled.VerifiedUser, contentDescription = null) },
                        checked = uiState.isShellSuAdbOnlyEnabled,
                        onCheckedChange = actions.onSetShellSuAdbOnlyEnabled,
                        enabled = uiState.shellSuAdbOnlyStatus == "supported",
                    )
                }
            )
        )
    }

    if (showSELinuxModeDialog) {
        SELinuxModeDialog(
            currentMode = uiState.selinuxMode,
            onDismiss = { showSELinuxModeDialog = false },
            onConfirm = actions.onSetSELinuxMode,
        )
    }
}
