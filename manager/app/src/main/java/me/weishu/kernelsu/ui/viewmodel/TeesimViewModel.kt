package me.weishu.kernelsu.ui.viewmodel

import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.net.Uri
import android.os.Process
import android.util.Base64
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
import me.weishu.kernelsu.R
import me.weishu.kernelsu.data.model.PifOption
import me.weishu.kernelsu.data.model.PifProp
import me.weishu.kernelsu.data.model.PifStatus
import me.weishu.kernelsu.data.model.WEBVIEW_ZYGOTE_UID
import me.weishu.kernelsu.data.model.TeesimStatus
import me.weishu.kernelsu.data.repository.SettingsRepository
import me.weishu.kernelsu.data.repository.SuperUserRepository
import me.weishu.kernelsu.data.repository.SuperUserRepositoryImpl
import me.weishu.kernelsu.data.teesim.TeesimKeybox
import me.weishu.kernelsu.data.repository.SettingsRepositoryImpl
import me.weishu.kernelsu.ksuApp

/**
 * One row of the target picker, carrying what it needs to draw itself.
 *
 * [userId] is the Android user the install belongs to: 0 for the owner, 999 for an app clone, 10
 * for a work profile. An app can be installed for several of them, so the picker lists it once per
 * user and the row says which one it is.
 */
data class TeesimApp(
    val packageName: String,
    val label: String,
    val system: Boolean,
    val packageInfo: PackageInfo,
    val userId: Int = 0,
) {
    /** Rows are keyed by package *and* user; the same package in two users is two rows. */
    val key: String get() = "$packageName@$userId"

    val isOtherUser: Boolean get() = userId != TeesimViewModel.PRIMARY_USER
}

data class TeesimUiState(
    val status: TeesimStatus? = null,
    val loading: Boolean = true,
    val readError: Boolean = false,
    val writing: Boolean = false,
    val apps: List<TeesimApp> = emptyList(),
    val appsLoading: Boolean = false,
    val showSystemApps: Boolean = false,
    /** Rows for installs in other users: app clones (999) and work profiles. */
    val showOtherUsers: Boolean = true,
    /** The bundled PlayIntegrityFix module, or null while it has not been read yet. */
    val pif: PifStatus? = null,
)

/**
 * Drives the TEESimulator page.
 *
 * Every write re-reads the module afterwards: the switch reflects the marker ksud will act on, the
 * keybox row reflects the file the daemon will read, and a failed command must not leave the page
 * claiming success.
 */
class TeesimViewModel(
    private val repo: SettingsRepository = SettingsRepositoryImpl(),
    private val appRepo: SuperUserRepository = SuperUserRepositoryImpl(),
) : ViewModel() {

    private val _uiState = MutableStateFlow(TeesimUiState())
    val uiState: StateFlow<TeesimUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            val result = repo.getTeesimStatus()
            val pif = repo.getPifStatus()
            _uiState.update {
                it.copy(
                    status = result.getOrNull() ?: it.status,
                    readError = result.isFailure,
                    loading = false,
                    pif = pif.getOrNull() ?: it.pif,
                )
            }
            // Opening the page is also the moment a profile that auto-includes new apps catches up
            // with anything installed since it was last written.
            syncNewTargets()
        }
    }

    fun setEnabled(enabled: Boolean) {
        if (_uiState.value.writing) return
        _uiState.update { it.copy(writing = true) }
        viewModelScope.launch {
            val result = repo.setTeesimEnabled(enabled)
            val status = repo.getTeesimStatus()
            _uiState.update {
                it.copy(
                    writing = false,
                    loading = false,
                    status = status.getOrNull() ?: it.status,
                    readError = status.isFailure,
                )
            }
            toast(
                when {
                    result.isFailure -> R.string.teesim_write_failed
                    // The hook is installed by the daemon at boot under a policy rule, so a fresh
                    // enable cannot be reported as live just because the marker is gone.
                    !enabled -> R.string.teesim_disabled_toast
                    status.getOrNull()?.daemonRunning == true -> R.string.teesim_enabled_toast
                    // The rule is applied live, so a daemon that did not come up is a real fault,
                    // not something a reboot fixes.
                    else -> R.string.teesim_state_pending
                }
            )
        }
    }

    fun install() = write(R.string.teesim_installed, R.string.teesim_install_failed) {
        repo.installTeesim()
    }

    fun uninstall() = write(R.string.teesim_uninstall_done, R.string.teesim_uninstall_failed) {
        repo.uninstallTeesim()
    }

    fun removeKeybox() = write(R.string.teesim_keybox_removed, R.string.teesim_keybox_write_failed) {
        repo.removeTeesimKeybox()
    }

    // PlayIntegrityFix is a Zygisk module, so every one of these only takes effect at the next
    // boot; the page says so instead of claiming the new identity is live.

    fun reinstallPif() = write(R.string.pif_reinstalled, R.string.pif_write_failed) {
        repo.reinstallPif()
    }

    fun adoptPif() = write(R.string.pif_adopted, R.string.pif_write_failed) {
        repo.adoptPif()
    }

    fun setPifEnabled(enabled: Boolean) =
        write(R.string.pif_saved, R.string.pif_write_failed) {
            repo.setPifEnabled(enabled)
        }

    fun setPifOption(option: PifOption, value: Boolean) =
        write(R.string.pif_saved, R.string.pif_write_failed) {
            repo.setPifOption(option, value)
        }

    /**
     * Validates the picked file before it reaches the payload.
     *
     * A configuration without a fingerprint leaves the module loaded and doing nothing, which reads
     * as a broken module rather than as a bad file - so the check happens here.
     */
    fun importPifProp(uri: Uri) {
        if (_uiState.value.writing) return
        _uiState.update { it.copy(writing = true) }
        viewModelScope.launch {
            val message = withContext(Dispatchers.IO) {
                val encoded = runCatching {
                    ksuApp.contentResolver.openInputStream(uri)?.use { stream ->
                        val bytes = stream.readBytes()
                        check(bytes.size in 1..MAX_PIF_PROP_BYTES) { "unexpected pif.prop size" }
                        val text = String(bytes, Charsets.UTF_8)
                        check(PifProp.isUsable(text)) { "not a PlayIntegrityFix configuration" }
                        Base64.encodeToString(bytes, Base64.NO_WRAP)
                    }
                }.getOrNull()
                when {
                    encoded == null -> R.string.pif_import_invalid
                    repo.importPifProp(encoded).isSuccess -> R.string.pif_imported
                    else -> R.string.pif_write_failed
                }
            }
            val status = repo.getPifStatus()
            _uiState.update { it.copy(writing = false, pif = status.getOrNull() ?: it.pif) }
            toast(message)
        }
    }

    fun setTargets(packages: List<String>, autoInclude: Boolean) =
        write(R.string.teesim_targets_saved, R.string.teesim_write_failed) {
            repo.setTeesimTargetApps(packages, autoInclude).onSuccess {
                // The list now describes these apps, so anything on the device is no longer "new";
                // without this the next open would add the apps the user just unticked.
                runCatching { thirdPartyPackages() }.onSuccess { installed ->
                    repo.teesimSyncedPackages = installed.joinToString("\n")
                }
            }
        }

    /**
     * Re-reads the installed packages on the daemon's side without changing anything.
     *
     * Only a configuration change makes it re-resolve a profile, so this is what puts an app
     * installed a minute ago into a profile that auto-includes new apps - no reboot, and no need to
     * toggle the switch off and on again.
     */
    fun rescan() =
        write(R.string.teesim_rescan_done, R.string.teesim_write_failed) {
            repo.rescanTeesimTargets()
        }

    /**
     * Validates the picked file before it reaches the daemon: a keybox that misses one of the two
     * keys or a certificate chain would leave the module installed but silently inert.
     */
    fun importKeybox(uri: Uri) {
        if (_uiState.value.writing) return
        _uiState.update { it.copy(writing = true) }
        viewModelScope.launch {
            val message = withContext(Dispatchers.IO) {
                val encoded = runCatching {
                    ksuApp.contentResolver.openInputStream(uri)?.use { stream ->
                        val bytes = stream.readBytes()
                        check(bytes.size in 1..MAX_KEYBOX_BYTES) { "unexpected keybox size" }
                        val text = String(bytes, Charsets.UTF_8).trim()
                        check(TeesimKeybox.isUsable(text)) { "not a keybox" }
                        Base64.encodeToString(text.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)
                    }
                }.getOrNull()
                when {
                    encoded == null -> R.string.teesim_keybox_invalid
                    repo.importTeesimKeybox(encoded).isSuccess -> R.string.teesim_keybox_imported
                    else -> R.string.teesim_keybox_write_failed
                }
            }
            val status = repo.getTeesimStatus()
            _uiState.update {
                it.copy(
                    writing = false,
                    loading = false,
                    status = status.getOrNull() ?: it.status,
                    readError = status.isFailure,
                )
            }
            toast(message)
        }
    }

    /**
     * Reads the installed packages, once per change of the App Store.
     *
     * Every user is walked, so a cloned app (user 999) or one installed in a work profile shows up
     * as its own row instead of being folded into the owner's copy. No profile is read per package:
     * the picker only needs names, labels and the owning user, and asking `AppProfile` for a few
     * hundred packages is a binder round trip each.
     *
     * [force] re-reads after a package was installed or removed; otherwise the list is cached for
     * the life of the screen.
     */
    fun loadApps(force: Boolean = false) {
        val state = _uiState.value
        if (!force && (state.apps.isNotEmpty() || state.appsLoading)) return
        _uiState.update { it.copy(appsLoading = true) }
        viewModelScope.launch {
            val apps = withContext(Dispatchers.IO) {
                runCatching {
                    val pm = ksuApp.packageManager
                    appRepo.getInstalledPackages().getOrThrow()
                        .filter { info ->
                            val ai = info.applicationInfo ?: return@filter false
                            // WebView Zygote is a bare UID with no package, and an app without code
                            // cannot be a target.
                            ai.uid != WEBVIEW_ZYGOTE_UID &&
                                (ai.flags and ApplicationInfo.FLAG_HAS_CODE) != 0
                        }
                        .map { info ->
                            val ai = info.applicationInfo!!
                            TeesimApp(
                                packageName = info.packageName,
                                label = runCatching { ai.loadLabel(pm).toString() }
                                    .getOrDefault(info.packageName),
                                system = (ai.flags and ApplicationInfo.FLAG_SYSTEM) != 0,
                                packageInfo = info,
                                userId = ai.uid / PER_USER_RANGE,
                            )
                        }
                        // One row per (package, user): the same app in two users is two installs.
                        .distinctBy { it.key }
                        // The owner's installs first, then the clones, each group by label.
                        .sortedWith(compareBy({ it.isOtherUser }, { it.label.lowercase() }))
                }.getOrDefault(emptyList())
            }
            _uiState.update { it.copy(apps = apps, appsLoading = false) }
        }
    }

    fun setShowOtherUsers(show: Boolean) {
        _uiState.update { it.copy(showOtherUsers = show) }
    }

    /**
     * Called when a package was installed or removed while the picker is open.
     *
     * Re-reads the list and, for a profile that auto-includes new apps, adds what appeared - so a
     * fresh install becomes a target without the user reopening the screen or pressing rescan.
     */
    fun onPackagesChanged() {
        loadApps(force = true)
        syncNewTargets()
    }

    /**
     * Adds the packages installed since the list was last written to a profile that auto-includes.
     *
     * The daemon takes new apps in by itself, but only while its configuration is reloaded, and the
     * manager is the thing that reloads it. Two facts are needed to do that without guessing: the
     * packages that existed when the profile was last written (persisted, see
     * [SettingsRepository.teesimSyncedPackages]) and the ones installed now. Their difference is
     * what gets appended, which is why an app the user unticked is not put back.
     *
     * The first run records the baseline and adds nothing: without one, every app already on the
     * device would look new and the profile would become "everything".
     */
    fun syncNewTargets() {
        val state = _uiState.value
        val status = state.status ?: return
        val config = status.config
        if (!status.installed || config.broken || !config.autoInclude || state.writing) return
        viewModelScope.launch {
            val installed = withContext(Dispatchers.IO) {
                runCatching { thirdPartyPackages() }.getOrNull()
            } ?: return@launch
            val known = repo.teesimSyncedPackages
                .lineSequence()
                .map { it.trim() }
                .filter { it.isNotEmpty() }
                .toSet()
            if (known.isEmpty()) {
                repo.teesimSyncedPackages = installed.sorted().joinToString("\n")
                return@launch
            }
            val added = installed.filterNot { it in known || it in config.defaultApps }
            if (added.isNotEmpty()) {
                _uiState.update { it.copy(writing = true) }
                val result = repo.setTeesimTargetApps(config.defaultApps + added, autoInclude = true)
                val refreshed = repo.getTeesimStatus()
                _uiState.update {
                    it.copy(
                        writing = false,
                        status = refreshed.getOrNull() ?: it.status,
                        readError = refreshed.isFailure,
                    )
                }
                toast(
                    if (result.isSuccess) R.string.teesim_auto_added else R.string.teesim_write_failed
                )
            }
            repo.teesimSyncedPackages = installed.sorted().joinToString("\n")
        }
    }

    /**
     * Package names of everything the user installed, across every Android user.
     *
     * System apps are left out on purpose: an OTA adds and replaces plenty of them, and none of
     * them is what "a new app" means to the person who turned auto-include on.
     */
    private suspend fun thirdPartyPackages(): List<String> =
        appRepo.getInstalledPackages().getOrThrow()
            .filter { info ->
                val ai = info.applicationInfo ?: return@filter false
                ai.uid != WEBVIEW_ZYGOTE_UID &&
                    (ai.flags and (ApplicationInfo.FLAG_SYSTEM or ApplicationInfo.FLAG_UPDATED_SYSTEM_APP)) == 0
            }
            .map { it.packageName }
            .distinct()
            .sorted()

    fun setShowSystemApps(show: Boolean) {
        _uiState.update { it.copy(showSystemApps = show) }
    }

    private fun write(
        successMessage: Int,
        failureMessage: Int,
        block: suspend () -> Result<Unit>,
    ) {
        if (_uiState.value.writing) return
        _uiState.update { it.copy(writing = true) }
        viewModelScope.launch {
            val result = block()
            val status = repo.getTeesimStatus()
            val pif = repo.getPifStatus()
            _uiState.update {
                it.copy(
                    writing = false,
                    loading = false,
                    status = status.getOrNull() ?: it.status,
                    readError = status.isFailure,
                    pif = pif.getOrNull() ?: it.pif,
                )
            }
            toast(if (result.isSuccess) successMessage else failureMessage)
        }
    }

    private fun toast(messageRes: Int) {
        Toast.makeText(ksuApp, messageRes, Toast.LENGTH_LONG).show()
    }

    companion object {
        /** The user the manager itself runs as; every other id is a clone or a profile. */
        val PRIMARY_USER: Int = Process.myUid() / PER_USER_RANGE

        /** Android's fixed stride between one user's app ids and the next. */
        private const val PER_USER_RANGE = 100_000

        /** A real keybox is a few dozen kilobytes; anything past this is not one. */
        const val MAX_KEYBOX_BYTES = 1_048_576

        /** `pif.prop` is a dozen lines; a megabyte of it is a file picker mistake. */
        const val MAX_PIF_PROP_BYTES = 65_536
    }
}
