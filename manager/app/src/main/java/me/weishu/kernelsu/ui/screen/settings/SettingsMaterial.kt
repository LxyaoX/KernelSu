package me.weishu.kernelsu.ui.screen.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Icon
import top.yukonga.miuix.kmp.basic.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.ExpressiveScaffold
import me.weishu.kernelsu.ui.component.material.SearchAppBar
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedListItem
import me.weishu.kernelsu.ui.component.material.SendLogBottomSheet
import me.weishu.kernelsu.ui.component.material.SnackBarHost
import me.weishu.kernelsu.ui.component.settings.SettingsCategory
import me.weishu.kernelsu.ui.component.settings.SettingsCategoryEntry
import me.weishu.kernelsu.ui.component.settings.SettingsCategoryGrid
import me.weishu.kernelsu.ui.component.settings.SettingsContentMaxWidth
import me.weishu.kernelsu.ui.component.uninstalldialog.UninstallDialog
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.screen.settings.search.SettingsHubAction
import me.weishu.kernelsu.ui.screen.settings.search.SettingsSearchResults
import me.weishu.kernelsu.ui.screen.settings.search.SettingsSearchShortcuts
import me.weishu.kernelsu.ui.screen.settings.search.SettingsSearchTarget
import me.weishu.kernelsu.ui.screen.settings.search.rememberSettingsSearchEntries
import me.weishu.kernelsu.ui.screen.settings.search.settingsSearchGroups
import top.yukonga.miuix.kmp.preference.ArrowPreference

private const val BUG_FEEDBACK_URL = "https://github.com/LyraVoid/FinaSU/issues/new/choose"

/**
 * @author weishu
 * @date 2023/1/1.
 */
@Composable
fun SettingPagerMaterial(
    uiState: SettingsUiState,
    actions: SettingsScreenActions,
    bottomInnerPadding: Dp,
) {
    val snackBarHost = remember { SnackbarHostState() }
    val showUninstallDialog = rememberSaveable { mutableStateOf(false) }
    var showBottomSheet by remember { mutableStateOf(false) }
    val uriHandler = LocalUriHandler.current
    val navigator = LocalNavigator.current
    var searchText by rememberSaveable { mutableStateOf("") }
    val searchEntries = rememberSettingsSearchEntries()
    val searchGroups = settingsSearchGroups(searchText, uiState, searchEntries)

    // Every row the search can offer knows where it goes; the hub only has to carry out the three
    // kinds of landing it can perform itself.
    val onSearchTarget: (SettingsSearchTarget) -> Unit = { target ->
        when (target) {
            is SettingsSearchTarget.Category -> actions.onOpenCategory(target.key)
            is SettingsSearchTarget.Page -> navigator.push(target.route)
            is SettingsSearchTarget.Hub -> when (target.action) {
                SettingsHubAction.BUG_FEEDBACK -> uriHandler.openUri(BUG_FEEDBACK_URL)
                SettingsHubAction.SEND_LOG -> showBottomSheet = true
                SettingsHubAction.UNINSTALL -> showUninstallDialog.value = true
            }
        }
    }

    UninstallDialog(
        show = showUninstallDialog.value,
        onDismissRequest = { showUninstallDialog.value = false }
    )

    ExpressiveScaffold(
        topBar = {
            // The hub keeps its compact bar in both layout variants: the search lives in the full
            // screen layer, so there is no inline bar here to collapse with the scroll.
            SearchAppBar(
                largeTitle = false,
                topSearch = true,
                searchBeforeActions = true,
                searchPlaceholder = stringResource(R.string.settings_search_hint),
                title = { Text(stringResource(R.string.settings)) },
                searchText = searchText,
                onSearchTextChange = { searchText = it },
                onClearClick = { searchText = "" },
                snackbarHostState = snackBarHost,
                defaultContent = { bottomPadding, closeSearch ->
                    SettingsSearchShortcuts(
                        onOpenCategory = { key ->
                            onSearchTarget(SettingsSearchTarget.Category(key))
                            closeSearch()
                        },
                        modifier = Modifier
                            .wrapContentWidth(Alignment.CenterHorizontally)
                            .widthIn(max = SettingsContentMaxWidth),
                        contentPadding = PaddingValues(bottom = bottomPadding),
                    )
                },
                searchContent = { bottomPadding, closeSearch ->
                    SettingsSearchResults(
                        groups = searchGroups,
                        onSelect = { entry ->
                            // Landing on the page matters more than the query that got us there.
                            onSearchTarget(entry.target)
                            closeSearch()
                        },
                        modifier = Modifier
                            .wrapContentWidth(Alignment.CenterHorizontally)
                            .widthIn(max = SettingsContentMaxWidth),
                        contentPadding = PaddingValues(bottom = bottomPadding),
                    )
                },
            )
        },
        snackbarHost = { SnackBarHost(hostState = snackBarHost, modifier = Modifier.padding(bottom = bottomInnerPadding)) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .wrapContentWidth(Alignment.CenterHorizontally)
                .widthIn(max = SettingsContentMaxWidth)
                .verticalScroll(rememberScrollState())
        ) {
            val entries = SettingsCategory.entries.map { category ->
                SettingsCategoryEntry(category) { actions.onOpenCategory(category.key) }
            }
            SettingsCategoryGrid(
                entries = entries,
                // The identity header used to hold this corner; the grid now owns the page's top
                // inset itself instead of sitting on a 4dp sliver under the bar.
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 13.dp),
            )

            SegmentedColumn(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                content = listOf(
                    {
                        ArrowPreference(
                            title = stringResource(id = R.string.settings_bug_feedback),
                            startAction = {
                                Icon(
                                    Icons.Filled.BugReport,
                                    stringResource(id = R.string.settings_bug_feedback)
                                )
                            },
                            onClick = { uriHandler.openUri(BUG_FEEDBACK_URL) },
                        )
                    },
                    {
                        ArrowPreference(
                            title = stringResource(id = R.string.send_log),
                            startAction = {
                                Icon(
                                    Icons.Filled.Description,
                                    stringResource(id = R.string.send_log)
                                )
                            },
                            onClick = { showBottomSheet = true },
                        )
                    },
                    {
                        ArrowPreference(
                            title = stringResource(id = R.string.about),
                            startAction = {
                                Icon(
                                    Icons.Filled.Info,
                                    stringResource(id = R.string.about)
                                )
                            },
                            onClick = actions.onOpenAbout,
                        )
                    }
                )
            )

            if (uiState.isLkmMode) {
                SegmentedColumn(
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                    content = listOf(
                        {
                            val uninstall = stringResource(id = R.string.settings_uninstall)
                            ArrowPreference(
                                title = uninstall,
                                startAction = { Icon(Icons.Filled.Delete, uninstall) },
                                onClick = { showUninstallDialog.value = true },
                                enabled = !uiState.isLateLoadMode,
                            )
                        }
                    )
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            if (showBottomSheet) {
                SendLogBottomSheet(
                    onDismiss = { showBottomSheet = false },
                    snackbarHostState = snackBarHost,
                )
            }
            Spacer(modifier = Modifier.height(bottomInnerPadding))
        }
    }
}
