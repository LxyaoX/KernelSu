package me.weishu.kernelsu.data.model

import androidx.compose.runtime.Immutable

/**
 * State of the ReKernel-X tombstone driver NeoSU ships inside the manager.
 *
 * The `.ko` files live in `assets/rekernel_x` (ReKernel-X 1.6, GPL-2.0) so the driver no longer
 * needs to be flashed as a separate module: the manager stages the file matching the running
 * kernel under `/data/adb/rekernel_x` and drops a `post-fs-data.d` script that loads it at boot.
 */
@Immutable
data class RekernelXStatus(
    /** `/sys/module/rekernel_x` exists, so the driver is loaded in this boot. */
    val loaded: Boolean = false,
    /** The boot script is in place, i.e. the next reboot loads the driver. */
    val autoLoad: Boolean = false,
    /** Driver file staged under `/data/adb/rekernel_x`, empty when none was staged yet. */
    val stagedDriver: String = "",
    /** `uname -r`, shown when no bundled build matches the kernel. */
    val kernelRelease: String = "",
    /** `android<ver>-<core>` of the running kernel, empty when it cannot be derived. */
    val kmi: String = "",
    /** Bundled driver that matches [kmi], empty when the kernel is unsupported. */
    val bundledDriver: String = "",
    /** A flashed ReKernel-X module owns the driver; the bundled build stays unused. */
    val externalModule: Boolean = false,
    /**
     * Why the last hot load failed, empty when it was never attempted or it worked.
     *
     * A load the kernel rejects fails the same way at boot, so this is the only place that can tell
     * "wait for a reboot" apart from "this build does not fit this kernel".
     */
    val lastLoadError: String = "",
) {
    /** The switch reflects the boot decision, not the momentary module state. */
    val enabled: Boolean get() = autoLoad

    val kmiSupported: Boolean get() = bundledDriver.isNotEmpty()

    companion object {
        private const val PROBE_PREFIX = "rkx:"

        /** The one kernel series this build carries a driver for. */
        private const val SUPPORTED_CORE = "6.1"

        /**
         * `android<ver>-<core>` for a `uname -r` release, mirroring the upstream `customize.sh`.
         *
         * Release names look like `6.1.75-android14-11-gabcdef` (GKI) or `6.1.75-android14-9`.
         * Only [SUPPORTED_CORE] is recognised: the assets carry one driver and ksud embeds one
         * module, both `android14-6.1`, so any other core returns an empty kmi and the UI reports
         * the kernel as unsupported instead of naming a driver that cannot be loaded.
         */
        fun kmiOf(release: String): String {
            val trimmed = release.trim()
            if (trimmed.isEmpty()) return ""
            val core = trimmed.substringBefore('-').substringBeforeLast('.')
            if (core.count { it == '.' } != 1) return ""
            if (core != SUPPORTED_CORE) return ""
            val android = if (trimmed.contains("-android")) {
                trimmed.substringAfter("-android").substringBefore('-')
            } else {
                "14"
            }
            if (android.isEmpty() || android.toIntOrNull() == null) return ""
            return "android$android-$core"
        }

        fun fromProbe(output: String): RekernelXStatus {
            val values = output.lineSequence()
                .filter { it.startsWith(PROBE_PREFIX) }
                .mapNotNull { line ->
                    val body = line.removePrefix(PROBE_PREFIX)
                    val separator = body.indexOf('=')
                    if (separator <= 0) null
                    else body.substring(0, separator) to body.substring(separator + 1).trim()
                }
                .toMap()

            val release = values["release"].orEmpty()
            val kmi = kmiOf(release)
            return RekernelXStatus(
                loaded = values["loaded"] == "1",
                autoLoad = values["autoload"] == "1",
                stagedDriver = values["driver"].orEmpty(),
                kernelRelease = release,
                kmi = kmi,
                externalModule = values["module"] == "1",
                lastLoadError = values["lasterror"].orEmpty(),
            )
        }
    }
}
