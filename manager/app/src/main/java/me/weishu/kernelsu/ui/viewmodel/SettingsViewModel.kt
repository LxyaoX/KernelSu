package me.weishu.kernelsu.ui.viewmodel

import android.system.OsConstants
import android.util.Log
import android.widget.Toast
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.R
import me.weishu.kernelsu.data.repository.SettingsRepository
import me.weishu.kernelsu.data.repository.SettingsRepositoryImpl
import me.weishu.kernelsu.data.susfs.SusfsRepository
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.ui.screen.settings.SettingsUiState
import me.weishu.kernelsu.ui.theme.ColorMode

class SettingsViewModel(
    private val repo: SettingsRepository = SettingsRepositoryImpl(),
) : ViewModel() {

    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()
    private var refreshGeneration = 0
    private var initialized = false

    fun ensureLoaded() {
        if (initialized) return
        initialized = true
        refresh()
    }

    fun refresh() {
        val generation = ++refreshGeneration
        _uiState.update {
            it.copy(
                isRekernelXLoading = true,
                isBootPropsLoading = true,
            )
        }
        viewModelScope.launch {
            val checkUpdate = repo.checkUpdate
            val checkModuleUpdate = repo.checkModuleUpdate
            val themeMode = repo.themeMode
            val keyColor = repo.keyColor
            val enablePredictiveBack = repo.enablePredictiveBack
            val enableNavigationBadge = repo.enableNavigationBadge
            val pageScale = repo.pageScale
            val moduleDescriptionMaxLines = repo.moduleDescriptionMaxLines
            val enableWebDebugging = repo.enableWebDebugging
            val listWorkingCardModeHidden = repo.listWorkingCardModeHidden
            val colorStyle = repo.colorStyle
            val isLkmMode = repo.isLkmMode()

            // Async loading for natives/features
            val isSuEnabled = repo.isSuEnabled()

            // One batched root-shell round trip replaces the per-feature exec chain: `feature
            // list` covers every status/value probe, and the config reads and the JSON
            // statuses ride the same job. A cold post-reboot open pays one command burst
            // instead of ~20 sequential exec round trips.
            val snapshot = repo.loadFeatureSnapshot()

            val suCompatStatus = snapshot.statuses["su_compat"].orEmpty()
            val suCompatPersistValue = snapshot.persists["su_compat"]
            val suCompatMode = when {
                suCompatStatus == "forced" -> if (isSuEnabled) 0 else 2
                suCompatPersistValue == 0L -> 2
                !isSuEnabled -> 1
                else -> 0
            }

            val kernelUmountStatus = snapshot.statuses["kernel_umount"].orEmpty()
            val isKernelUmountEnabled = repo.isKernelUmountEnabled()
            val selinuxHideStatus = snapshot.statuses["selinux_hide"].orEmpty()
            val isSelinuxHideEnabled = repo.isSelinuxHideEnabled()
            val mountViewFilterStatus = snapshot.statuses["mount_view_filter"].orEmpty()
            val isMountViewFilterEnabled = snapshot.values["mount_view_filter"] == 1L
            val selinuxMode = repo.getSELinuxMode()
            val sulogStatus = snapshot.statuses["sulog"].orEmpty()
            val avcSpoofStatus = snapshot.statuses["avc_spoof"].orEmpty()
            val isAvcSpoofEnabled = repo.isAvcSpoofEnabled()
            val isSulogEnabled = if (sulogStatus == "forced") {
                snapshot.values["sulog"]?.let { it != 0L } == true
            } else {
                snapshot.persists["sulog"] == 1L
            }
            val adbRootStatus = snapshot.statuses["adb_root"].orEmpty()
            val isAdbRootEnabled = if (adbRootStatus == "forced") {
                snapshot.values["adb_root"]?.let { it != 0L } == true
            } else {
                snapshot.persists["adb_root"] == 1L
            }
            val isDefaultUmountModules = repo.isDefaultUmountModules()
            val shellSuAdbOnlyStatus = snapshot.statuses["shell_su_adb_only"].orEmpty()
            val isShellSuAdbOnlyEnabled = if (shellSuAdbOnlyStatus == "forced") {
                snapshot.values["shell_su_adb_only"]?.let { it != 0L } == true
            } else {
                snapshot.persists["shell_su_adb_only"] == 1L
            }
            val autoJailbreak = repo.autoJailbreak
            val useSoftReboot = repo.useSoftReboot
            val allowAnyDynamicManager = repo.allowAnyDynamicManager
            val isDynamicManagerAvailable = Natives.isDynamicManagerEnabled()
            val isLateLoadMode = Natives.isLateLoadMode
            val kernelFullVersionAvailable = Natives.isManager && !Natives.getFullVersion().isNullOrBlank()

            val rekernelXResult = repo.getRekernelXStatus()
            val bootPropsResult = repo.getBootPropsStatus()
            // The home card's SUSFS row switch only makes sense when the kernel reports SUSFS.
            val susfsDetected = try {
                SusfsRepository().status().detected
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                false
            }

            if (generation != refreshGeneration) return@launch
            _uiState.update {
                it.copy(
                    checkUpdate = checkUpdate,
                    checkModuleUpdate = checkModuleUpdate,
                    themeMode = themeMode,
                    keyColor = keyColor,
                    enablePredictiveBack = enablePredictiveBack,
                    enableNavigationBadge = enableNavigationBadge,
                    pageScale = pageScale,
                    moduleDescriptionMaxLines = moduleDescriptionMaxLines,
                    enableWebDebugging = enableWebDebugging,
                    listWorkingCardModeHidden = listWorkingCardModeHidden,
                    hideHomeFingerprint = repo.hideHomeFingerprint,
                    hideHomeZygisk = repo.hideHomeZygisk,
                    hideHomeSusfs = repo.hideHomeSusfs,
                    hideHomeKernelFullVersion = repo.hideHomeKernelFullVersion,
                    kernelFullVersionAvailable = kernelFullVersionAvailable,
                    susfsDetected = susfsDetected,
                    colorStyle = colorStyle,
                    suCompatStatus = suCompatStatus,
                    suCompatMode = suCompatMode,
                    isSuEnabled = isSuEnabled,
                    adbRootStatus = adbRootStatus,
                    isAdbRootEnabled = isAdbRootEnabled,
                    shellSuAdbOnlyStatus = shellSuAdbOnlyStatus,
                    isShellSuAdbOnlyEnabled = isShellSuAdbOnlyEnabled,
                    kernelUmountStatus = kernelUmountStatus,
                    isKernelUmountEnabled = isKernelUmountEnabled,
                    selinuxHideStatus = selinuxHideStatus,
                    isSelinuxHideEnabled = isSelinuxHideEnabled,
                    mountViewFilterStatus = mountViewFilterStatus,
                    isMountViewFilterEnabled = isMountViewFilterEnabled,
                    selinuxMode = selinuxMode,
                    sulogStatus = sulogStatus,
                    avcSpoofStatus = avcSpoofStatus,
                    isAvcSpoofEnabled = isAvcSpoofEnabled,
                    isSulogEnabled = isSulogEnabled,
                    isDefaultUmountModules = isDefaultUmountModules,
                    isLkmMode = isLkmMode,
                    autoJailbreak = autoJailbreak,
                    useSoftReboot = useSoftReboot,
                    allowAnyDynamicManager = allowAnyDynamicManager,
                    isDynamicManagerAvailable = isDynamicManagerAvailable,
                    isLateLoadMode = isLateLoadMode,
                    rekernelXStatus = rekernelXResult.getOrNull(),
                    rekernelXReadError = rekernelXResult.isFailure,
                    isRekernelXLoading = false,
                    bootPropsStatus = bootPropsResult.getOrNull(),
                    bootPropsReadError = bootPropsResult.isFailure,
                    isBootPropsLoading = false,
                )
            }
        }
    }

    /**
     * Re-reads only the preference-backed settings and the cheap in-process kernel queries.
     * Returned-to screens call this instead of [refresh]: a pushed theme page
     * can change the stored values, but nothing it does can change the feature states, so
     * the batched root-shell probe round trip is not paid again on every resume.
     */
    fun refreshSettings() {
        viewModelScope.launch {
            _uiState.update {
                it.copy(
                    checkUpdate = repo.checkUpdate,
                    checkModuleUpdate = repo.checkModuleUpdate,
                    themeMode = repo.themeMode,
                    keyColor = repo.keyColor,
                    enablePredictiveBack = repo.enablePredictiveBack,
                    enableNavigationBadge = repo.enableNavigationBadge,
                    pageScale = repo.pageScale,
                    moduleDescriptionMaxLines = repo.moduleDescriptionMaxLines,
                    enableWebDebugging = repo.enableWebDebugging,
                    listWorkingCardModeHidden = repo.listWorkingCardModeHidden,
                    hideHomeFingerprint = repo.hideHomeFingerprint,
                    hideHomeZygisk = repo.hideHomeZygisk,
                    hideHomeSusfs = repo.hideHomeSusfs,
                    colorStyle = repo.colorStyle,
                    autoJailbreak = repo.autoJailbreak,
                    useSoftReboot = repo.useSoftReboot,
                    allowAnyDynamicManager = repo.allowAnyDynamicManager,
                    isDynamicManagerAvailable = Natives.isDynamicManagerEnabled(),
                    isLateLoadMode = Natives.isLateLoadMode,
                    isLkmMode = repo.isLkmMode(),
                )
            }
        }
    }

    fun setRekernelXEnabled(enabled: Boolean) {
        if (_uiState.value.isRekernelXWriting || _uiState.value.isRekernelXLoading) return
        ++refreshGeneration
        _uiState.update { it.copy(isRekernelXWriting = true) }
        viewModelScope.launch {
            val result = repo.setRekernelXEnabled(enabled)
            val status = repo.getRekernelXStatus()
            _uiState.update {
                it.copy(
                    isRekernelXWriting = false,
                    isRekernelXLoading = false,
                    rekernelXStatus = status.getOrNull(),
                    rekernelXReadError = status.isFailure,
                )
            }
            val message = when {
                result.isFailure -> R.string.rekernel_x_write_failed
                !enabled -> R.string.rekernel_x_disabled_toast
                // The driver only loads now when the hot insmod was accepted; otherwise the
                // module's post-fs-data script loads it at the next boot.
                status.getOrNull()?.loaded == true -> R.string.rekernel_x_loaded_toast
                else -> R.string.rekernel_x_reboot_required
            }
            Toast.makeText(ksuApp, message, Toast.LENGTH_LONG).show()
        }
    }

    fun setBootPropsEnabled(enabled: Boolean) {
        if (_uiState.value.isBootPropsWriting || _uiState.value.isBootPropsLoading) return
        ++refreshGeneration
        _uiState.update { it.copy(isBootPropsWriting = true) }
        viewModelScope.launch {
            val result = repo.setBootPropsEnabled(enabled)
            val status = repo.getBootPropsStatus()
            _uiState.update {
                it.copy(
                    isBootPropsWriting = false,
                    isBootPropsLoading = false,
                    bootPropsStatus = status.getOrNull(),
                    bootPropsReadError = status.isFailure,
                )
            }
            val message = when {
                result.isFailure -> R.string.boot_props_write_failed
                !enabled -> R.string.boot_props_disabled_toast
                // resetprop rewrites the shared property area, so a device that took the values
                // needs no reboot; anything left is a property this build could not write.
                status.getOrNull()?.allApplied == true -> R.string.boot_props_enabled_toast
                else -> R.string.boot_props_enabled_pending_toast
            }
            Toast.makeText(ksuApp, message, Toast.LENGTH_LONG).show()
        }
    }

    fun setCheckUpdate(enabled: Boolean) {
        repo.checkUpdate = enabled
        _uiState.update { it.copy(checkUpdate = enabled) }
    }

    fun setCheckModuleUpdate(enabled: Boolean) {
        repo.checkModuleUpdate = enabled
        _uiState.update { it.copy(checkModuleUpdate = enabled) }
    }

    fun setThemeMode(mode: Int) {
        repo.themeMode = mode
        _uiState.update { it.copy(themeMode = mode) }
    }

    fun setColorMode(mode: ColorMode) {
        repo.themeMode = mode.value
        _uiState.update { it.copy(themeMode = mode.value) }
    }

    fun setKeyColor(color: Int) {
        repo.keyColor = color
        repo.useSystemDynamicColor = color == 0
        _uiState.update { it.copy(keyColor = color) }
    }

    fun setColorStyle(style: String) {
        repo.colorStyle = style
        repo.colorGenerationMode = "custom"
        _uiState.update { it.copy(colorStyle = style) }
    }

    fun setEnablePredictiveBack(enabled: Boolean) {
        repo.enablePredictiveBack = enabled
        _uiState.update { it.copy(enablePredictiveBack = enabled) }
    }

    fun setEnableNavigationBadge(enabled: Boolean) {
        repo.enableNavigationBadge = enabled
        _uiState.update { it.copy(enableNavigationBadge = enabled) }
    }

    fun setPageScale(scale: Float) {
        repo.pageScale = scale
        _uiState.update { it.copy(pageScale = scale) }
    }

    fun setModuleDescriptionMaxLines(lines: Int) {
        repo.moduleDescriptionMaxLines = lines
        _uiState.update { it.copy(moduleDescriptionMaxLines = lines) }
    }

    fun setEnableWebDebugging(enabled: Boolean) {
        repo.enableWebDebugging = enabled
        _uiState.update { it.copy(enableWebDebugging = enabled) }
    }

    fun setListWorkingCardModeHidden(hidden: Boolean) {
        repo.listWorkingCardModeHidden = hidden
        _uiState.update { it.copy(listWorkingCardModeHidden = hidden) }
    }

    fun setHideHomeFingerprint(hidden: Boolean) {
        repo.hideHomeFingerprint = hidden
        _uiState.update { it.copy(hideHomeFingerprint = hidden) }
    }

    fun setHideHomeZygisk(hidden: Boolean) {
        repo.hideHomeZygisk = hidden
        _uiState.update { it.copy(hideHomeZygisk = hidden) }
    }

    fun setHideHomeSusfs(hidden: Boolean) {
        repo.hideHomeSusfs = hidden
        _uiState.update { it.copy(hideHomeSusfs = hidden) }
    }

    fun setHideHomeKernelFullVersion(hidden: Boolean) {
        repo.hideHomeKernelFullVersion = hidden
        _uiState.update { it.copy(hideHomeKernelFullVersion = hidden) }
    }

    fun setSuCompatMode(mode: Int) {
        viewModelScope.launch(Dispatchers.IO) {
            when (mode) {
                0 -> if (repo.setSuEnabled(true)) {
                    repo.execKsudFeatureSave()
                    repo.setSuCompatModePref(0)
                    _uiState.update { it.copy(suCompatMode = 0, isSuEnabled = true) }
                }

                1 -> if (repo.setSuEnabled(true)) {
                    repo.execKsudFeatureSave()
                    if (repo.setSuEnabled(false)) {
                        // "Disable until reboot" implies it should be enabled on next boot.
                        // We set the preference to 0 (Enabled) to match the persistent state.
                        repo.setSuCompatModePref(0)
                        _uiState.update { it.copy(suCompatMode = 1, isSuEnabled = false) }
                    }
                }

                2 -> if (repo.setSuEnabled(false)) {
                    repo.execKsudFeatureSave()
                    repo.setSuCompatModePref(2)
                    _uiState.update { it.copy(suCompatMode = 2, isSuEnabled = false) }
                }
            }
        }
    }

    fun setKernelUmountEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setKernelUmountEnabled(enabled)) {
                repo.execKsudFeatureSave()
                _uiState.update { it.copy(isKernelUmountEnabled = enabled) }
            }
        }
    }

    fun setSelinuxHideEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            val status = repo.setSelinuxHideEnabled(enabled)
            repo.execKsudFeatureSave()
            _uiState.update { it.copy(isSelinuxHideEnabled = enabled) }
            when (status) {
                0 -> {}
                -OsConstants.EAGAIN -> {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(ksuApp, R.string.settings_selinux_hide_reboot_required,
                            Toast.LENGTH_LONG).show()
                    }
                }
                else -> {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(ksuApp, ksuApp.getString(R.string.settings_selinux_hide_failed, status),
                            Toast.LENGTH_LONG).show()
                    }
                }
            }
        }
    }

    fun setMountViewFilterEnabled(enabled: Boolean) {
        if (_uiState.value.isMountViewFilterWriting) return
        _uiState.update { it.copy(isMountViewFilterWriting = true) }
        viewModelScope.launch(Dispatchers.IO) {
            val result = repo.setMountViewFilterEnabled(enabled)
            val status = if (result.isFailure) "unavailable" else repo.getMountViewFilterStatus()
            val actualEnabled = repo.isMountViewFilterEnabled()
            val saved = result.isSuccess && repo.execKsudFeatureSave()
            _uiState.update {
                it.copy(
                    mountViewFilterStatus = status,
                    isMountViewFilterEnabled = actualEnabled,
                    isMountViewFilterWriting = false,
                )
            }
            val message = when {
                result.isFailure -> R.string.settings_mount_view_filter_failed
                !saved -> R.string.settings_mount_view_filter_save_failed
                else -> null
            }
            if (message != null) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(ksuApp, message, Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    fun setSELinuxMode(enforcing: Boolean) {
        if (_uiState.value.isSELinuxModeWriting) return
        _uiState.update { it.copy(isSELinuxModeWriting = true) }
        viewModelScope.launch(Dispatchers.IO) {
            val success = repo.setSELinuxMode(enforcing)
            val mode = repo.getSELinuxMode()
            _uiState.update { it.copy(selinuxMode = mode, isSELinuxModeWriting = false) }
            if (!success) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(ksuApp, R.string.settings_selinux_mode_failed, Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    fun setAutoJailbreak(enabled: Boolean) {
        repo.autoJailbreak = enabled
        _uiState.update { it.copy(autoJailbreak = enabled) }
    }

    fun setUseSoftReboot(enabled: Boolean) {
        repo.useSoftReboot = enabled
        _uiState.update { it.copy(useSoftReboot = enabled) }
    }

    fun setAllowAnyDynamicManager(enabled: Boolean) {
        repo.allowAnyDynamicManager = enabled
        _uiState.update { it.copy(allowAnyDynamicManager = enabled) }
    }

    fun setAvcSpoofEnabled(enabled: Boolean) {
        if (_uiState.value.isAvcSpoofWriting) return
        _uiState.update { it.copy(isAvcSpoofWriting = true) }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (repo.setAvcSpoofEnabled(enabled)) {
                    val saved = repo.execKsudFeatureSave()
                    _uiState.update { it.copy(isAvcSpoofEnabled = repo.isAvcSpoofEnabled()) }
                    if (!saved) {
                        withContext(Dispatchers.Main) {
                            Toast.makeText(ksuApp, R.string.settings_avc_spoof_save_failed, Toast.LENGTH_LONG).show()
                        }
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(ksuApp, R.string.settings_avc_spoof_failed, Toast.LENGTH_LONG).show()
                    }
                }
            } finally {
                _uiState.update { it.copy(isAvcSpoofWriting = false) }
            }
        }
    }

    fun setSulogEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setSulogEnabled(enabled)) {
                repo.execKsudFeatureSave()
                _uiState.update { it.copy(isSulogEnabled = enabled) }
            }
        }
    }

    fun setAdbRootEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setAdbRootEnabled(enabled)) {
                repo.execKsudFeatureSave()
                _uiState.update { it.copy(isAdbRootEnabled = enabled) }
            }
        }
    }

    fun setShellSuAdbOnlyEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setShellSuAdbOnlyEnabled(enabled)) {
                repo.execKsudFeatureSave()
                _uiState.update { it.copy(isShellSuAdbOnlyEnabled = enabled) }
            }
        }
    }

    fun setDefaultUmountModules(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setDefaultUmountModules(enabled)) {
                _uiState.update { it.copy(isDefaultUmountModules = enabled) }
            }
        }
    }
}
