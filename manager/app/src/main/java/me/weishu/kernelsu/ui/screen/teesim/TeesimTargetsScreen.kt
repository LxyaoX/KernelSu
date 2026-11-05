package me.weishu.kernelsu.ui.screen.teesim

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Deselect
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.SelectAll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import me.weishu.kernelsu.R
import me.weishu.kernelsu.data.model.TeesimConfig
import me.weishu.kernelsu.ui.component.AppIconImage
import me.weishu.kernelsu.ui.component.material.ExpressiveScaffold
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedListItem
import me.weishu.kernelsu.ui.component.material.SegmentedTextField
import me.weishu.kernelsu.ui.component.material.TopBarBackButton
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.viewmodel.TeesimViewModel
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton

/**
 * Picks the apps whose attestation the simulator answers.
 *
 * Selection is by package name because that is what `config.json` stores. Apps that belong to a
 * profile other than `default` are left out of the list on purpose: those profiles carry their own
 * keybox, and moving an app into `default` from here would silently change what signs it.
 *
 * The profile can also auto-include, in which case the ticks below are only the apps that had to be
 * named by hand; anything installed later joins on its own. Both halves are saved together because
 * upstream turns a profile that names nothing and includes nothing into a configuration error, not
 * an empty target set.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TeesimTargetsScreen() {
    val viewModel = viewModel<TeesimViewModel>()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    val navigator = LocalNavigator.current
    val scrollBehavior = MiuixScrollBehavior()

    LaunchedEffect(Unit) { viewModel.loadApps() }

    // An app installed while this screen is open has to appear without reopening it - and with the
    // profile that auto-includes new apps, it also has to become a target right away.
    val context = LocalContext.current
    DisposableEffect(context, viewModel) {
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_PACKAGE_ADDED)
            addAction(Intent.ACTION_PACKAGE_REMOVED)
            addAction(Intent.ACTION_PACKAGE_CHANGED)
            addDataScheme("package")
        }
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                // Replacing an app fires REMOVED and then ADDED; reloading on both is cheap because
                // the query is one binder call and the result is cached until the next broadcast.
                viewModel.onPackagesChanged()
            }
        }
        context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        onDispose { context.unregisterReceiver(receiver) }
    }

    val savedApps = state.status?.targetApps.orEmpty()
    var selected by remember(savedApps) { mutableStateOf(savedApps.toSet()) }
    var query by rememberSaveable { mutableStateOf("") }

    val reserved = state.status?.otherApps.orEmpty().toSet()
    val visible = state.apps.filter { app ->
        app.packageName !in reserved &&
            (state.showSystemApps || !app.system) &&
            (state.showOtherUsers || !app.isOtherUser) &&
            (query.isBlank() ||
                app.label.contains(query, ignoreCase = true) ||
                app.packageName.contains(query, ignoreCase = true))
    }
    // One package may be listed once per user; "select all shown" is still about packages.
    val visiblePackages = visible.map { it.packageName }.distinct()
    // Saving replaces the whole app list, so an unreadable or unparsable config must not be
    // edited: an empty picker would otherwise write an empty profile over the user's selection.
    val config = state.status?.config
    val configUsable = config != null && !config.broken
    val canEdit = !state.appsLoading && !state.writing && !state.loading && configUsable

    // Upstream's `autoIncludeNewApps`: the daemon answers for user apps installed after it first
    // ran, so the list below is no longer the whole story. Two profiles asking for it would claim
    // the same apps and the daemon treats that as a broken configuration, so a profile that
    // already holds the flag blocks ours instead of being overwritten from here.
    val savedAutoInclude = config?.autoInclude == true
    var autoInclude by remember(savedAutoInclude) { mutableStateOf(savedAutoInclude) }
    val autoIncludeHolder = config?.autoIncludeElsewhere.orEmpty()
    // An empty list is legal precisely because auto-include covers the profile; upstream rejects a
    // profile that names nothing and includes nothing.
    val canSave = canEdit && autoIncludeHolder.isEmpty() && (selected.isNotEmpty() || autoInclude)

    ExpressiveScaffold(
        topBar = {
            MiuixTopAppBar(
                title = stringResource(R.string.teesim_targets_title),
                navigationIcon = { TopBarBackButton(onClick = { navigator.pop() }) },
                actions = {
                    // Only a profile that takes apps in runs out of date on its own, so the button
                    // is offered exactly where re-reading the installed packages can change
                    // something.
                    IconButton(
                        enabled = canEdit && autoInclude && autoIncludeHolder.isEmpty(),
                        onClick = viewModel::rescan,
                    ) {
                        Icon(
                            Icons.Filled.Refresh,
                            contentDescription = stringResource(R.string.teesim_rescan),
                        )
                    }
                    MiuixTextButton(
                        text = stringResource(R.string.teesim_save),
                        onClick = {
                            viewModel.setTargets(selected.toList(), autoInclude)
                            navigator.pop()
                        },
                        enabled = canSave,
                    )
                },
                scrollBehavior = scrollBehavior,
            )
        }
    ) { inner ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(inner)
                .nestedScroll(scrollBehavior.nestedScrollConnection),
        ) {
            SegmentedColumn(modifier = Modifier.padding(horizontal = 16.dp)) {
                item {
                    SegmentedTextField(
                        label = stringResource(R.string.teesim_search_apps),
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                    )
                }
                item {
                    SwitchPreference(
                        title = stringResource(R.string.teesim_auto_include),
                        summary = if (autoIncludeHolder.isEmpty()) {
                            stringResource(R.string.teesim_auto_include_summary)
                        } else {
                            stringResource(R.string.teesim_auto_include_taken, autoIncludeHolder)
                        },
                        checked = autoInclude,
                        enabled = canEdit && autoIncludeHolder.isEmpty(),
                        onCheckedChange = { autoInclude = it },
                    )
                }
                item {
                    SwitchPreference(
                        title = stringResource(R.string.teesim_show_system_apps),
                        checked = state.showSystemApps,
                        onCheckedChange = viewModel::setShowSystemApps,
                    )
                }
                if (state.apps.any { it.isOtherUser }) {
                    item {
                        SwitchPreference(
                            title = stringResource(R.string.teesim_show_other_users),
                            summary = stringResource(R.string.teesim_show_other_users_summary),
                            checked = state.showOtherUsers,
                            onCheckedChange = viewModel::setShowOtherUsers,
                        )
                    }
                }
            }

            // Bulk actions work on what the filter currently shows, so selecting a vendor prefix
            // stays a two-tap action instead of a scroll through the whole package list.
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 24.dp, end = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = stringResource(R.string.teesim_selected_count, selected.size),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.weight(1f),
                )
                IconButton(
                    enabled = canEdit && visiblePackages.isNotEmpty(),
                    onClick = { selected = selected + visiblePackages },
                ) {
                    Icon(
                        Icons.Filled.SelectAll,
                        contentDescription = stringResource(R.string.teesim_select_all),
                    )
                }
                IconButton(
                    enabled = canEdit && selected.isNotEmpty(),
                    onClick = { selected = selected - visiblePackages.toSet() },
                ) {
                    Icon(
                        Icons.Filled.Deselect,
                        contentDescription = stringResource(R.string.teesim_clear_selection),
                    )
                }
            }

            if (state.appsLoading || state.loading) {
                Text(
                    text = stringResource(R.string.teesim_loading),
                    modifier = Modifier.padding(16.dp),
                )
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    contentPadding = PaddingValues(16.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    if (!state.loading && !configUsable) {
                        item(key = "unreadable") {
                            Text(
                                text = stringResource(R.string.teesim_read_error),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.error,
                                modifier = Modifier.padding(bottom = 8.dp),
                            )
                        }
                    }
                    if (autoInclude && selected.isEmpty()) {
                        item(key = "auto-only") {
                            Text(
                                text = stringResource(R.string.teesim_auto_include_only),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(bottom = 8.dp),
                            )
                        }
                    }
                    if (reserved.isNotEmpty()) {
                        item(key = "reserved") {
                            Text(
                                text = stringResource(
                                    R.string.teesim_targets_hint,
                                    reserved.size,
                                    TeesimConfig.DEFAULT_PROFILE,
                                ),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(bottom = 8.dp),
                            )
                        }
                    }
                    items(visible, key = { it.key }) { app ->
                        val checked = selected.contains(app.packageName)
                        SegmentedListItem(
                            enabled = canEdit,
                            onClick = {
                                selected = if (checked) {
                                    selected - app.packageName
                                } else {
                                    selected + app.packageName
                                }
                            },
                            leadingContent = {
                                AppIconImage(
                                    packageInfo = app.packageInfo,
                                    label = app.label,
                                    modifier = Modifier.size(40.dp),
                                )
                            },
                            headlineContent = { Text(app.label) },
                            supportingContent = {
                                Text(
                                    if (app.isOtherUser) {
                                        stringResource(
                                            R.string.teesim_app_row_other_user,
                                            app.packageName,
                                            userBadge(app.userId),
                                        )
                                    } else {
                                        app.packageName
                                    }
                                )
                            },
                            trailingContent = {
                                androidx.compose.material3.Checkbox(
                                    checked = checked,
                                    // The row toggles too, so either gesture works.
                                    onCheckedChange = { nowChecked ->
                                        selected = if (nowChecked) {
                                            selected + app.packageName
                                        } else {
                                            selected - app.packageName
                                        }
                                    },
                                )
                            },
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                }
            }
        }
    }
}

/**
 * How a user id is named in a row.
 *
 * 999 is where an app clone lives on MIUI and HyperOS, which is the case this list exists for; every
 * other id is reported as a number rather than guessed at, because a work profile can be any id.
 */
@Composable
private fun userBadge(userId: Int): String =
    if (userId == CLONE_USER) {
        stringResource(R.string.teesim_user_clone, userId)
    } else {
        stringResource(R.string.teesim_user_other, userId)
    }

/** MIUI and HyperOS install a cloned app under this user. */
private const val CLONE_USER = 999
