package me.weishu.kernelsu.data.repository

import android.content.pm.PackageInfo
import me.weishu.kernelsu.data.model.AppInfo

interface SuperUserRepository {
    suspend fun getAppList(): Result<Pair<List<AppInfo>, List<Int>>>

    /**
     * Installed packages of every Android user, without a profile for any of them.
     *
     * The superuser list needs `AppProfile` per app, which is one binder round trip each; a picker
     * that only shows names, labels and the owning user has no use for that, so it asks for the
     * package list alone. Entries keep the per-user UID the framework assigned, which is how a
     * clone in another user is told apart from the original.
     */
    suspend fun getInstalledPackages(): Result<List<PackageInfo>>
    suspend fun refreshProfiles(currentApps: List<AppInfo>): Result<List<AppInfo>>
}
