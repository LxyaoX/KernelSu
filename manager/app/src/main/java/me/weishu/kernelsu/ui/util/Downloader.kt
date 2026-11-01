package me.weishu.kernelsu.ui.util

import android.net.Uri
import android.os.SystemClock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.ui.util.module.LatestVersionInfo
import okhttp3.Request
import me.weishu.kernelsu.data.modulestore.ExpectedFile

/**
 * @author weishu
 * @date 2023/6/22.
 */
/** Downloads [url] and returns the downloaded file, or `null` when it failed. */
suspend fun download(
    url: String,
    fileName: String,
    onDownloaded: (Uri) -> Unit = {},
    onDownloading: () -> Unit = {},
    onProgress: (Int) -> Unit = {},
    onFailed: (String) -> Unit = {},
    expected: ExpectedFile? = null,
): Uri? {
    onDownloading()

    val downloadId = DownloadManager.enqueue(
        context = ksuApp,
        url = url,
        fileName = fileName,
        expected = expected,
    )

    val lastState = DownloadManager.downloads
        .onEach { map -> map[downloadId]?.let { onProgress(it.progress) } }
        .first { map ->
            val status = map[downloadId]?.status
            status == DownloadManager.Status.COMPLETED ||
                status == DownloadManager.Status.FAILED
        }[downloadId]

    if (lastState?.status == DownloadManager.Status.FAILED) {
        onFailed(lastState.error.orEmpty())
        return null
    }
    return lastState?.resultUri?.also { uri -> withContext(Dispatchers.Main) { onDownloaded(uri) } }
}

internal suspend fun isDownloadAvailable(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    runCatching {
        ksuApp.contentResolver.openFileDescriptor(uri, "r").use { it != null }
    }.getOrDefault(false)
}

/**
 * The release feed the manager checks for updates.
 *
 * It must point at FinaSU's own repository: pointing it upstream (tiann/KernelSU) compared a FinaSU
 * build against KernelSU's releases, so once upstream's version code overtook FinaSU's the manager
 * would have offered the KernelSU APK as a FinaSU update.
 */
private const val MANAGER_RELEASES_API = "https://api.github.com/repos/LyraVoid/FinaSU/releases/latest"

/** Release assets are named `<manager>_v<versionName>_<versionCode>-<buildType>.apk`. */
private val RELEASE_ASSET_NAME = Regex("v(.+?)_(\\d+)")

/**
 * Extracts the `(versionName, versionCode)` carried by a release asset name, or `null` when the
 * name is not an APK or does not carry a version code.
 */
internal fun parseReleaseAssetName(name: String): Pair<String, Int>? {
    if (!name.endsWith(".apk")) return null
    val match = RELEASE_ASSET_NAME.find(name) ?: return null
    val versionCode = match.groupValues[2].toIntOrNull() ?: return null
    return match.groupValues[1] to versionCode
}

/**
 * How long an answer from the release feed is reused.
 *
 * The home page re-runs this on every resume and every capability refresh, and the feed only moves
 * when a build is published, so without a cache a single visit cost one HTTPS round trip per return
 * to the page. A build that reports itself as already current is reused for the full window; a
 * failed or offline check is retried sooner so a flaky link does not hide an update for half an hour.
 */
private const val UPDATE_CHECK_TTL_MS = 30 * 60_000L
private const val UPDATE_CHECK_RETRY_MS = 5 * 60_000L

private val updateCheckLock = Any()
private var updateCheckAt = 0L
private var updateCheckResult: LatestVersionInfo? = null

fun checkNewVersion(): LatestVersionInfo {
    synchronized(updateCheckLock) {
        val cached = updateCheckResult
        if (cached != null) {
            val age = SystemClock.elapsedRealtime() - updateCheckAt
            val ttl = if (cached.versionCode > 0) UPDATE_CHECK_TTL_MS else UPDATE_CHECK_RETRY_MS
            if (age < ttl) return cached
        }
    }
    val fresh = fetchNewVersion()
    synchronized(updateCheckLock) {
        updateCheckAt = SystemClock.elapsedRealtime()
        updateCheckResult = fresh
    }
    return fresh
}

private fun fetchNewVersion(): LatestVersionInfo {
    if (!isNetworkAvailable(ksuApp)) return LatestVersionInfo()
    // default null value if failed
    val defaultValue = LatestVersionInfo()
    runCatching {
        ksuApp.okhttpClient.newCall(Request.Builder().url(MANAGER_RELEASES_API).build()).execute()
            .use { response ->
                if (!response.isSuccessful) {
                    return defaultValue
                }
                val body = response.body.string()
                val json = org.json.JSONObject(body)
                val changelog = json.optString("body")

                val assets = json.getJSONArray("assets")
                for (i in 0 until assets.length()) {
                    val asset = assets.getJSONObject(i)
                    val parsed = parseReleaseAssetName(asset.getString("name")) ?: continue
                    val downloadUrl = asset.getString("browser_download_url")

                    return LatestVersionInfo(
                        versionName = parsed.first,
                        versionCode = parsed.second,
                        downloadUrl = downloadUrl,
                        changelog = changelog,
                    )
                }

            }
    }
    return defaultValue
}
