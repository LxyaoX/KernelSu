package me.weishu.kernelsu.ui.viewmodel

import android.os.Build
import android.system.Os
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.BuildConfig
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.R
import me.weishu.kernelsu.data.repository.SettingsRepository
import me.weishu.kernelsu.data.repository.SettingsRepositoryImpl
import me.weishu.kernelsu.getKernelVersion
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.ui.screen.home.HomeMount
import me.weishu.kernelsu.ui.screen.home.HomeUiState
import me.weishu.kernelsu.ui.screen.home.SystemInfo
import me.weishu.kernelsu.ui.screen.home.getManagerVersion
import me.weishu.kernelsu.ui.screen.home.resolveHomeMountProvider
import me.weishu.kernelsu.ui.util.CapabilityRepository
import me.weishu.kernelsu.ui.util.RootShellStatus
import me.weishu.kernelsu.ui.util.checkNewVersion
import me.weishu.kernelsu.ui.util.getModuleDisplayName
import me.weishu.kernelsu.ui.util.getSELinuxStatusRaw
import me.weishu.kernelsu.ui.util.module.LatestVersionInfo
import me.weishu.kernelsu.ui.util.resolveDeviceName

class HomeViewModel(
    private val settingsRepo: SettingsRepository = SettingsRepositoryImpl()
) : ViewModel() {

    private val _uiState = MutableStateFlow(buildState())
    val uiState: StateFlow<HomeUiState> = _uiState.asStateFlow()

    /**
     * The home screen must follow the same capability snapshot as the layout and navigation, so a
     * root probe that finishes, fails or recovers updates the warnings and counts without a manual
     * refresh. Identity changes rebuild the whole state (version/UAPI/lkm fields depend on it); a
     * mere root-state change only patches the root fields, keeping the layout stable.
     */
    private var lastIsManager: Boolean? = null
    private var lastUapiCompatible: Boolean? = null

    /** Monotonic guard so an older refresh cannot overwrite a newer one. */
    private var refreshToken = 0L
    private val susfsRepository = me.weishu.kernelsu.data.susfs.SusfsRepository()
    private var susfsToken = 0L
    private var zygiskToken = 0L
    private var mountToken = 0L

    init {
        viewModelScope.launch {
            kotlinx.coroutines.flow.combine(
                me.weishu.kernelsu.ui.util.RootShell.status,
                me.weishu.kernelsu.ui.util.RootShell.generation,
            ) { status, generation -> status to generation }.collect { (status, _) ->
                if (status == RootShellStatus.Ready) {
                    refreshSusfs()
                    refreshZygisk()
                    refreshMount()
                } else {
                    susfsToken++
                    zygiskToken++
                    mountToken++
                    _uiState.update {
                        it.copy(
                            susfs = me.weishu.kernelsu.data.susfs.SusfsStatus(protocol = "root_unavailable"),
                            zygiskProvider = null,
                            mount = HomeMount(),
                        )
                    }
                }
            }
        }
        viewModelScope.launch {
            CapabilityRepository.state.collect { capability ->
                val identityChanged =
                    capability.isManager != lastIsManager || capability.uapiCompatible != lastUapiCompatible
                lastIsManager = capability.isManager
                lastUapiCompatible = capability.uapiCompatible
                if (identityChanged) {
                    refresh()
                } else {
                    _uiState.update {
                        it.copy(
                            isRootAvailable = capability.rootStatus == RootShellStatus.Ready,
                            rootStatus = capability.rootStatus,
                        )
                    }
                }
            }
        }
    }

    fun refresh() {
        refreshSusfs()
        refreshZygisk()
        refreshMount()
        // Guard against overlapping refreshes: a slower, older build must not overwrite a newer
        // snapshot (or clobber the capability collector's root update).
        val token = ++refreshToken
        viewModelScope.launch {
            val baseState = withContext(Dispatchers.IO) { buildState() }
            if (token != refreshToken) return@launch
            // Re-read the root fields from the latest capability snapshot at commit time, so a
            // completion of this refresh cannot clobber a root update the collector already applied.
            val capability = CapabilityRepository.current()
            _uiState.update {
                baseState.copy(
                    susfs = it.susfs,
                    zygiskProvider = it.zygiskProvider,
                    mount = it.mount,
                    isRootAvailable = capability.rootStatus == RootShellStatus.Ready,
                    rootStatus = capability.rootStatus,
                )
            }
            if (baseState.checkUpdateEnabled) {
                val latestVersionInfo = withContext(Dispatchers.IO) { checkNewVersion() }
                if (token != refreshToken) return@launch
                _uiState.update { it.copy(latestVersionInfo = latestVersionInfo) }
            }
        }
    }

    fun refreshSusfs() {
        val request = ++susfsToken
        val generation = me.weishu.kernelsu.ui.util.RootShell.generation.value
        viewModelScope.launch {
            val status = try { susfsRepository.status() }
            catch (e: kotlinx.coroutines.CancellationException) { throw e }
            catch (e: Exception) { me.weishu.kernelsu.data.susfs.SusfsStatus(protocol = "root_unavailable", error = e.message) }
            if (request == susfsToken && generation == me.weishu.kernelsu.ui.util.RootShell.generation.value) {
                _uiState.update { it.copy(susfs = status) }
            }
        }
    }

    /**
     * Resolves the active Zygisk implementation for the home card: any active external provider
     * wins and is named by its module name; otherwise the built-in engine when it is enabled and
     * installed. Null hides the row.
     */
    fun refreshZygisk() {
        val request = ++zygiskToken
        viewModelScope.launch {
            val provider = try {
                resolveZygiskProvider()
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                null
            }
            if (request == zygiskToken) _uiState.update { it.copy(zygiskProvider = provider) }
        }
    }

    /**
     * Resolves the provider that actually mounted modules this boot. A read failure, or a record
     * that cannot name one, hides the row rather than guessing.
     */
    fun refreshMount() {
        val request = ++mountToken
        val generation = me.weishu.kernelsu.ui.util.RootShell.generation.value
        viewModelScope.launch {
            val status = settingsRepo.getFolkMountStatus().getOrNull()
            if (request == mountToken &&
                generation == me.weishu.kernelsu.ui.util.RootShell.generation.value
            ) {
                _uiState.update {
                    it.copy(
                        mount = HomeMount(
                            provider = resolveHomeMountProvider(status),
                            metamoduleId = status?.metamoduleId,
                        )
                    )
                }
            }
        }
    }

    private suspend fun resolveZygiskProvider(): String? {
        val status = settingsRepo.getYukiZygiskStatus().getOrNull() ?: return null
        // No installed payload means the home card has no Zygisk row to name.
        if (!status.installed) return null
        // YukiZygisk is the only built-in engine this build ships: it is either running or it is
        // not, and the page behind the settings row is where a failure is explained.
        return ksuApp.getString(R.string.yukizygisk_builtin_active)
    }

    private fun buildState(): HomeUiState {
        val kernelVersion = getKernelVersion()
        val isManager = Natives.isManager
        val ksuVersion = if (isManager) Natives.version else null
        val kernelUAPIVersion = if (isManager) Natives.kernelUAPIVersion else null
        val managerUAPIVersion = Natives.managerUAPIVersion
        val lkmMode = ksuVersion?.let { if (kernelVersion.isSupportedSeries()) Natives.isLkmMode else null }
        val rootStatus = CapabilityRepository.current().rootStatus
        val managerVersion = getManagerVersion(ksuApp)

        return HomeUiState(
            kernelVersion = kernelVersion,
            ksuVersion = ksuVersion,
            lkmMode = lkmMode,
            isLkmBundled = lkmMode == true && Natives.isLkmBundled,
            isManager = isManager,
            isManagerPrBuild = BuildConfig.IS_PR_BUILD,
            isKernelPrBuild = Natives.isPrBuild,
            requiresNewKernel = isManager && Natives.managerUAPIVersion > Natives.kernelUAPIVersion,
            requiresNewManager = isManager && Natives.managerUAPIVersion < Natives.kernelUAPIVersion,
            kernelUAPIVersion = kernelUAPIVersion,
            managerUAPIVersion = managerUAPIVersion,
            isRootAvailable = rootStatus == RootShellStatus.Ready,
            rootStatus = rootStatus,
            isSafeMode = Natives.isSafeMode,
            isLateLoadMode = Natives.isLateLoadMode,
            checkUpdateEnabled = settingsRepo.checkUpdate,
            latestVersionInfo = LatestVersionInfo(),
            currentManagerVersionCode = managerVersion.versionCode,
            systemInfo = SystemInfo(
                kernelVersion = Os.uname().release,
                kernelFullVersion = if (isManager) Natives.getFullVersion() else null,
                managerVersion = "${managerVersion.versionName} (${managerVersion.versionCode}-${managerUAPIVersion})",
                deviceModel = resolveDeviceName(),
                fingerprint = Build.FINGERPRINT,
                selinuxStatus = getSELinuxStatusRaw(),
                seccompStatus = runCatching {
                    Os.prctl(21 /* PR_GET_SECCOMP */, 0, 0, 0, 0)
                }.getOrDefault(-1),
            ),
        )
    }
}

