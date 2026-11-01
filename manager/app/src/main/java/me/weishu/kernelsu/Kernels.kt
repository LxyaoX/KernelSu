package me.weishu.kernelsu

import android.system.Os

/**
 * @author weishu
 * @date 2022/12/10.
 */

data class KernelVersion(val major: Int, val patchLevel: Int, val subLevel: Int) {
    override fun toString(): String {
        return "$major.$patchLevel.$subLevel"
    }

    /**
     * Whether this kernel is one the manager supports at all: `android14-6.1`, i.e. Linux 6.1.
     *
     * One series, not "6.1 and newer". The APK carries a single `android14-6.1_kernelsu.ko`, ksud
     * embeds that same file, and `boot-patch` refuses any other series - so on a 6.6 / 6.12 / 6.18
     * kernel there is nothing to install, and saying so is better than offering an install that
     * ends in "unsupported kernel series". The kernel module enforces the same window with an
     * `#error` for both bounds.
     */
    fun isSupportedSeries(): Boolean {
        return major == 6 && patchLevel == 1
    }
}

fun parseKernelVersion(version: String): KernelVersion {
    val find = "(\\d+)\\.(\\d+)\\.(\\d+)".toRegex().find(version)
    if (find != null) {
        val major = find.groupValues[1].toIntOrNull()
        val patchLevel = find.groupValues[2].toIntOrNull()
        val subLevel = find.groupValues[3].toIntOrNull()
        if (major != null && patchLevel != null && subLevel != null) {
            return KernelVersion(major, patchLevel, subLevel)
        }
    }
    return KernelVersion(-1, -1, -1)
}

fun getKernelVersion(): KernelVersion {
    Os.uname().release.let {
        return parseKernelVersion(it)
    }
}