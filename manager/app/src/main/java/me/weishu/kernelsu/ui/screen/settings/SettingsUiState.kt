package me.weishu.kernelsu.ui.screen.settings

import androidx.compose.runtime.Immutable
import com.materialkolor.PaletteStyle
import me.weishu.kernelsu.data.model.BootPropsStatus
import me.weishu.kernelsu.data.model.RekernelXStatus
@Immutable
data class SettingsUiState(
    val checkUpdate: Boolean = true,
    val checkModuleUpdate: Boolean = true,
    val themeMode: Int = 0,
    val keyColor: Int = 0,
    val colorStyle: String = PaletteStyle.TonalSpot.name,
    val enablePredictiveBack: Boolean = false,
    val enableNavigationBadge: Boolean = true,
    val pageScale: Float = 1.0f,
    val moduleDescriptionMaxLines: Int = 4,
    val enableWebDebugging: Boolean = false,
    val listWorkingCardModeHidden: Boolean = false,
    val hideHomeFingerprint: Boolean = false,
    val hideHomeZygisk: Boolean = false,
    val hideHomeSusfs: Boolean = false,
    val hideHomeKernelFullVersion: Boolean = false,
    val kernelFullVersionAvailable: Boolean = false,

    // Su Compat
    val suCompatStatus: String = "",
    val suCompatMode: Int = 0, // 0: enable default, 1: disable until reboot, 2: disable always
    val isSuEnabled: Boolean = false,

    // Kernel Umount
    val kernelUmountStatus: String = "",
    val isKernelUmountEnabled: Boolean = false,

    // SELinux Hide
    val selinuxHideStatus: String = "",
    val isSelinuxHideEnabled: Boolean = false,

    // Mount View Filter
    val mountViewFilterStatus: String = "",
    val isMountViewFilterEnabled: Boolean = false,
    val isMountViewFilterWriting: Boolean = false,

    // SELinux Mode
    val selinuxMode: String = "",
    val isSELinuxModeWriting: Boolean = false,

    // SU Log
    val sulogStatus: String = "",
    val avcSpoofStatus: String = "",
    val isAvcSpoofEnabled: Boolean = false,
    val isAvcSpoofWriting: Boolean = false,
    val isSulogEnabled: Boolean = false,

    /** Whether the kernel reports a usable SUSFS: gates the home card's SUSFS row switch. */
    val susfsDetected: Boolean = false,

    // ReKernel-X tombstone kernel support (external LKM, only its module state is managed here)
    val rekernelXStatus: RekernelXStatus? = null,
    val isRekernelXLoading: Boolean = true,
    val rekernelXReadError: Boolean = false,
    val isRekernelXWriting: Boolean = false,

    // Boot-property rewrite. Only the value read back from the device is kept: the scripts that
    // carry the decision live in /data/adb, not in the settings snapshot.
    val bootPropsStatus: BootPropsStatus? = null,
    val isBootPropsLoading: Boolean = true,
    val bootPropsReadError: Boolean = false,
    val isBootPropsWriting: Boolean = false,

    // Umount Modules
    val isDefaultUmountModules: Boolean = false,

    // ADB Root
    val adbRootStatus: String = "",
    val isAdbRootEnabled: Boolean = false,
    val shellSuAdbOnlyStatus: String = "",
    val isShellSuAdbOnlyEnabled: Boolean = false,

    val isLkmMode: Boolean = false,
    val isLateLoadMode: Boolean = false,

    // Auto Jailbreak
    val autoJailbreak: Boolean = false,

    // Soft Reboot
    val useSoftReboot: Boolean = false,

    // Dynamic Manager
    val allowAnyDynamicManager: Boolean = false,
    val isDynamicManagerAvailable: Boolean = true
)

@Immutable
data class SettingsScreenActions(
    val onSetCheckUpdate: (Boolean) -> Unit,
    val onSetCheckModuleUpdate: (Boolean) -> Unit,
    val onOpenTheme: () -> Unit,
    val onOpenLanguage: () -> Unit,
    val onOpenProfileTemplate: () -> Unit,
    val onSetSuCompatMode: (Int) -> Unit,
    val onSetKernelUmountEnabled: (Boolean) -> Unit,
    val onSetSelinuxHideEnabled: (Boolean) -> Unit,
    val onSetMountViewFilterEnabled: (Boolean) -> Unit,
    val onSetSELinuxMode: (Boolean) -> Unit,
    val onSetSulogEnabled: (Boolean) -> Unit,
    val onSetAvcSpoofEnabled: (Boolean) -> Unit,
    val onOpenTeesim: () -> Unit,
    val onSetRekernelXEnabled: (Boolean) -> Unit,
    val onSetBootPropsEnabled: (Boolean) -> Unit,
    val onSetAdbRootEnabled: (Boolean) -> Unit,
    val onSetShellSuAdbOnlyEnabled: (Boolean) -> Unit,
    val onSetDefaultUmountModules: (Boolean) -> Unit,
    val onSetEnableWebDebugging: (Boolean) -> Unit,
    val onSetListWorkingCardModeHidden: (Boolean) -> Unit,
    val onSetHideHomeFingerprint: (Boolean) -> Unit,
    val onSetHideHomeZygisk: (Boolean) -> Unit,
    val onSetHideHomeSusfs: (Boolean) -> Unit,
    val onSetHideHomeKernelFullVersion: (Boolean) -> Unit,
    val onSetEnableNavigationBadge: (Boolean) -> Unit,
    val onSetEnablePredictiveBack: (Boolean) -> Unit,
    val onSetPageScale: (Float) -> Unit,
    val onSetModuleDescriptionMaxLines: (Int) -> Unit,
    val onSetAutoJailbreak: (Boolean) -> Unit,
    val onSetUseSoftReboot: (Boolean) -> Unit,
    val onSetAllowAnyDynamicManager: (Boolean) -> Unit,
    val onOpenCategory: (String) -> Unit,
    val onOpenAbout: () -> Unit,
)
