package me.weishu.kernelsu.ui.screen.kernelmodule

import android.text.format.Formatter
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Description
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Memory
import androidx.compose.material.icons.outlined.MoreHoriz
import androidx.compose.material.icons.outlined.PauseCircle
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Restore
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import top.yukonga.miuix.kmp.basic.HorizontalDivider
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import me.weishu.kernelsu.ui.theme.FolkMotion
import top.yukonga.miuix.kmp.theme.MiuixTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SheetValue
import top.yukonga.miuix.kmp.basic.SnackbarHostState
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.pulltorefresh.PullToRefreshDefaults
import androidx.compose.material3.pulltorefresh.rememberPullToRefreshState
import androidx.compose.material3.rememberBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.launch
import me.weishu.kernelsu.R
import me.weishu.kernelsu.data.model.KernelModule
import me.weishu.kernelsu.data.model.LoadState
import me.weishu.kernelsu.modulecard.ModuleCardConfig
import me.weishu.kernelsu.ui.component.material.ExpressiveScaffold
import me.weishu.kernelsu.ui.component.material.ExpressiveSwitch
import me.weishu.kernelsu.ui.component.material.FolkBottomSheet
import me.weishu.kernelsu.ui.component.material.FolkFilledTonalButton
import me.weishu.kernelsu.ui.component.material.FolkSmallExtendedFloatingActionButton
import me.weishu.kernelsu.ui.component.material.FolkTextButton
import me.weishu.kernelsu.ui.component.material.SearchAppBar
import me.weishu.kernelsu.ui.component.material.SnackBarHost
import me.weishu.kernelsu.ui.component.material.TopBarBackButton
import me.weishu.kernelsu.ui.component.module.AdaptiveModuleButtonRow
import me.weishu.kernelsu.ui.component.module.ModuleActionsSheet
import me.weishu.kernelsu.ui.component.module.ModuleButtonConfig
import me.weishu.kernelsu.ui.component.module.ModuleCardAction
import me.weishu.kernelsu.ui.component.module.ModuleCardLabel
import me.weishu.kernelsu.ui.component.module.ModuleCardShell
import me.weishu.kernelsu.ui.component.module.TwoColumnGrid
import me.weishu.kernelsu.ui.component.module.splicedLazyColumnGroup
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.screen.module.ModuleButtonStyle
import me.weishu.kernelsu.ui.viewmodel.KernelModuleViewModel
import me.weishu.kernelsu.ui.component.material.ExpressiveDialog

@Composable
fun KernelModuleScreen() {
    val viewModel = viewModel<KernelModuleViewModel>()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    val actions = remember(viewModel) { createActions(viewModel) }
    val navigator = LocalNavigator.current
    val snackbar = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val pullState = rememberPullToRefreshState()
    val installingMessage = stringResource(R.string.kernel_modules_installing)
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) viewModel.install(uri)
    }

    LifecycleResumeEffect(Unit) {
        viewModel.ensureLoaded()
        onPauseOrDispose { }
    }

    LaunchedEffect(state.error) {
        val message = state.error ?: return@LaunchedEffect
        snackbar.showSnackbar(message)
        actions.onErrorShown()
    }
    LaunchedEffect(state.notice) {
        val message = state.notice ?: return@LaunchedEffect
        snackbar.showSnackbar(message)
        actions.onNoticeShown()
    }

    val listContent: @Composable (PaddingValues) -> Unit = { padding ->
        KernelModuleList(
            state = state,
            actions = actions,
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        )
    }

    ExpressiveScaffold(
        topBar = {
            SearchAppBar(
                largeTitle = true,
                topSearch = false,
                collapsibleSearch = true,
                title = { Text(stringResource(R.string.kernel_modules)) },
                searchText = state.searchText,
                onSearchTextChange = actions.onSearchTextChange,
                onClearClick = { actions.onSearchTextChange("") },
                searchPlaceholder = stringResource(R.string.kernel_modules_search_hint),
                snackbarHostState = snackbar,
                navigationIcon = {
                    TopBarBackButton(
                        onClick = { navigator.pop() },
                        horizontalPadding = 0.dp,
                    )
                },
                defaultContent = { bottom, _ ->
                    listContent(
                        PaddingValues(
                            start = 16.dp,
                            end = 16.dp,
                            top = 16.dp,
                            bottom = bottom + 88.dp,
                        )
                    )
                },
                searchContent = { bottom, _ ->
                    listContent(
                        PaddingValues(
                            start = 16.dp,
                            end = 16.dp,
                            top = 16.dp,
                            bottom = bottom + 88.dp,
                        )
                    )
                },
            )
        },
        floatingActionButton = {
            if (state.rootAvailable) {
                FolkSmallExtendedFloatingActionButton(
                    onClick = {
                        if (state.isInstalling) {
                            scope.launch {
                                snackbar.showSnackbar(
                                    installingMessage
                                )
                            }
                        } else {
                            picker.launch("*/*")
                        }
                    },
                    icon = { Icon(Icons.Filled.Add, null) },
                    text = {
                        Text(
                            stringResource(
                                if (state.isInstalling) {
                                    R.string.kernel_modules_installing
                                } else {
                                    R.string.kernel_modules_import
                                }
                            )
                        )
                    },
                )
            }
        },
        snackbarHost = { SnackBarHost(hostState = snackbar) },
    ) { inner ->
        PullToRefreshBox(
            modifier = Modifier
                .fillMaxSize()
                .padding(inner),
            isRefreshing = state.isRefreshing,
            onRefresh = actions.onRefresh,
            state = pullState,
            indicator = {
                PullToRefreshDefaults.LoadingIndicator(
                    modifier = Modifier.align(Alignment.TopCenter),
                    isRefreshing = state.isRefreshing,
                    state = pullState,
                )
            },
        ) {
            listContent(PaddingValues(bottom = 88.dp))
        }
    }

    state.detailModule?.let { module ->
        KernelModuleDetailsSheet(
            module = module,
            onDismiss = actions.onDismissDetails,
        )
    }
    state.paramsModule?.let { module ->
        KernelModuleParamsSheet(
            module = module,
            draft = state.paramsDraft,
            pending = module.id in state.pending,
            onDraftChange = actions.onParamsDraftChange,
            onSave = actions.onSaveParams,
            onDismiss = actions.onDismissParams,
        )
    }
    state.logModule?.let { module ->
        KernelModuleLogSheet(
            module = module,
            loading = state.isLoadingLog,
            result = state.logResult,
            onDismiss = actions.onDismissLog,
        )
    }
    state.confirmRemove?.let { module ->
        ExpressiveDialog(
            onDismissRequest = actions.onDismissRemove,
            title = { Text(stringResource(R.string.kernel_modules_remove_title, module.name)) },
            text = { Text(stringResource(R.string.kernel_modules_remove_body)) },
            confirmButton = {
                FolkTextButton(
                    onClick = actions.onConfirmRemove,
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) {
                    Text(stringResource(R.string.kernel_modules_remove))
                }
            },
            dismissButton = {
                FolkTextButton(onClick = actions.onDismissRemove) {
                    Text(stringResource(android.R.string.cancel))
                }
            },
        )
    }
}

private fun createActions(viewModel: KernelModuleViewModel): KernelModuleActions = KernelModuleActions(
    onRefresh = { viewModel.refresh() },
    onSearchTextChange = viewModel::updateSearchText,
    onInstall = viewModel::install,
    onToggleAutoload = viewModel::toggleAutoload,
    onLoad = viewModel::load,
    onEnable = viewModel::enable,
    onDisable = viewModel::disable,
    onOpenDetails = viewModel::openDetails,
    onDismissDetails = viewModel::dismissDetails,
    onOpenParams = viewModel::openParams,
    onParamsDraftChange = viewModel::updateParamsDraft,
    onSaveParams = viewModel::saveParams,
    onDismissParams = viewModel::dismissParams,
    onOpenLog = viewModel::openLog,
    onDismissLog = viewModel::dismissLog,
    onRequestRemove = viewModel::requestRemove,
    onConfirmRemove = viewModel::confirmRemove,
    onDismissRemove = viewModel::dismissRemove,
    onNoticeShown = viewModel::clearNotice,
    onErrorShown = viewModel::clearError,
)

@Composable
private fun KernelModuleList(
    state: KernelModuleUiState,
    actions: KernelModuleActions,
    modifier: Modifier,
) {
    when {
        !state.rootAvailable -> MessageState(
            modifier,
            stringResource(R.string.kernel_modules_root_required),
            stringResource(R.string.kernel_modules_root_required_summary),
        )
        state.loadError != null -> MessageState(
            modifier,
            stringResource(R.string.kernel_modules_read_error),
            state.loadError,
        )
        !state.hasLoaded -> MessageState(
            modifier,
            stringResource(R.string.kernel_modules_loading),
            null,
        )
        state.modules.isEmpty() -> MessageState(
            modifier,
            stringResource(R.string.kernel_modules_empty),
            stringResource(R.string.kernel_modules_empty_summary),
        )
        state.visibleModules.isEmpty() -> MessageState(
            modifier,
            stringResource(R.string.kernel_modules_no_results),
            null,
        )
        else -> {
            val modules = state.visibleModules
            val wide = LocalConfiguration.current.screenWidthDp >= 600
            var expandedModuleId by rememberSaveable { mutableStateOf<String?>(null) }
            val itemCard: @Composable (KernelModule) -> Unit = { module ->
                KernelModuleCard(
                    module = module,
                    state = state,
                    actions = actions,
                    expanded = expandedModuleId == module.id,
                    onExpandToggle = {
                        expandedModuleId = if (expandedModuleId == module.id) null else module.id
                    },
                )
            }
            Column(modifier) {
                if (state.safeMode) {
                    SafeModeNotice()
                }
                if (wide) {
                    TwoColumnGrid(
                        items = modules,
                        key = { it.id },
                        modifier = Modifier.weight(1f),
                        verticalSpacing = 16.dp,
                        horizontalSpacing = 16.dp,
                        contentPadding = PaddingValues(16.dp),
                    ) { module ->
                        itemCard(module)
                    }
                } else {
                    val spliced = ModuleCardConfig.splicedCardGroup
                    LazyColumn(
                        modifier = Modifier.weight(1f),
                    ) {
                        item { Spacer(Modifier.height(8.dp)) }
                        if (spliced) {
                            splicedLazyColumnGroup(
                                items = modules,
                                key = { _, module -> module.id },
                                contentType = { _, _ -> "kernel_module" },
                            ) { _, module ->
                                itemCard(module)
                            }
                        } else {
                            items(modules, key = { it.id }) { module ->
                                Box(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                                    itemCard(module)
                                }
                            }
                        }
                        item { Spacer(Modifier.height(8.dp)) }
                    }
                }
            }
        }
    }
}

@Composable
private fun SafeModeNotice() {
    Column(
        Modifier
            .fillMaxWidth()
            .padding(16.dp),
    ) {
        Text(
            stringResource(R.string.kernel_modules_safe_mode),
            style = MiuixTheme.textStyles.headline1,
            color = MiuixTheme.colorScheme.error,
        )
        Text(
            stringResource(R.string.kernel_modules_safe_mode_summary),
            style = MiuixTheme.textStyles.body2,
            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
        )
    }
}

@Composable
private fun MessageState(modifier: Modifier, title: String, summary: String?) {
    Box(modifier.padding(32.dp), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
                Icons.Outlined.Memory,
                contentDescription = null,
                modifier = Modifier.size(40.dp),
                tint = MiuixTheme.colorScheme.primary,
            )
            Spacer(Modifier.height(12.dp))
            Text(title, style = MiuixTheme.textStyles.headline1)
            summary?.let {
                Spacer(Modifier.height(6.dp))
                Text(
                    it,
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
        }
    }
}

@Composable
private fun KernelModuleCard(
    module: KernelModule,
    state: KernelModuleUiState,
    actions: KernelModuleActions,
    expanded: Boolean,
    onExpandToggle: () -> Unit,
) {
    val showMoreInfo = ModuleCardConfig.showMoreInfo
    val foldEnabled = ModuleCardConfig.foldSystemModule
    val iconOnly = ModuleButtonStyle.fromKey(ModuleCardConfig.buttonStyle) == ModuleButtonStyle.IconOnly
    val haptics = LocalHapticFeedback.current
    val pending = module.id in state.pending
    val autoloadPending = module.id in state.autoloadPending
    val fileSize = if (module.fileSize > 0) {
        Formatter.formatShortFileSize(LocalContext.current, module.fileSize)
    } else {
        ""
    }
    val technicalMetadata = listOf(
        module.architecture.takeIf { it.isNotBlank() }
            ?.let { "${stringResource(R.string.kernel_modules_architecture)}: $it" },
        fileSize.takeIf { it.isNotBlank() }
            ?.let { "${stringResource(R.string.kernel_modules_file_size)}: $it" },
    ).filterNotNull().joinToString(" · ")
    val dependencySummary = module.depends.joinToString(", ")
        .ifBlank { stringResource(R.string.kernel_modules_no_dependencies) }
    val stateText = stateLabel(module)
    val expandLabel = stringResource(R.string.expand)
    val collapseLabel = stringResource(R.string.collapse)
    val autoloadLabel = stringResource(R.string.kernel_modules_autoload)
    val moreLabel = stringResource(R.string.kernel_modules_more)
    val statusText = if (module.disabled) {
        stringResource(R.string.kernel_modules_disabled)
    } else {
        stateText
    }
    val statusContainerColor = when {
        module.disabled || module.loaded == LoadState.UNKNOWN -> MiuixTheme.colorScheme.errorContainer
        module.loaded == LoadState.LOADED -> MiuixTheme.colorScheme.primaryContainer
        else -> MiuixTheme.colorScheme.secondaryContainer
    }
    val statusContentColor = when {
        module.disabled || module.loaded == LoadState.UNKNOWN -> MiuixTheme.colorScheme.onErrorContainer
        module.loaded == LoadState.LOADED -> MiuixTheme.colorScheme.onPrimaryContainer
        else -> MiuixTheme.colorScheme.onSecondaryContainer
    }
    var showMoreActions by rememberSaveable(module.id) { mutableStateOf(false) }

    val loadButton = if (module.loaded != LoadState.LOADED) {
        ModuleButtonConfig(
            icon = Icons.Outlined.PlayArrow,
            text = stringResource(R.string.kernel_modules_load),
            contentDescription = stringResource(R.string.kernel_modules_load),
            enabled = module.loadable && !state.safeMode && !pending && !module.disabled,
            onClick = { actions.onLoad(module) },
        )
    } else {
        null
    }
    val paramsAction = if (module.parameters.isNotEmpty()) {
        ModuleCardAction(
            icon = Icons.Outlined.Tune,
            title = stringResource(R.string.kernel_modules_params),
            description = stringResource(R.string.kernel_modules_params_summary),
            enabled = !pending && module.error == null,
            onClick = { actions.onOpenParams(module) },
        )
    } else {
        null
    }
    val logAction = ModuleCardAction(
        icon = Icons.Outlined.Description,
        title = stringResource(R.string.kernel_modules_last_result),
        description = stringResource(R.string.kernel_modules_last_result_summary),
        enabled = !pending && module.error == null,
        onClick = { actions.onOpenLog(module) },
    )
    val detailsAction = ModuleCardAction(
        icon = Icons.Outlined.Info,
        title = stringResource(R.string.kernel_modules_details),
        description = stringResource(R.string.kernel_modules_details_summary),
        onClick = { actions.onOpenDetails(module) },
    )
    val blockAction = ModuleCardAction(
        icon = if (module.disabled) Icons.Outlined.Restore else Icons.Outlined.PauseCircle,
        title = stringResource(
            if (module.disabled) R.string.kernel_modules_enable else R.string.kernel_modules_disable
        ),
        description = stringResource(
            if (module.disabled) R.string.kernel_modules_enable_note else R.string.kernel_modules_disable_note
        ),
        enabled = !pending && !state.safeMode && module.error == null && !module.protected,
        onClick = {
            if (module.disabled) actions.onEnable(module) else actions.onDisable(module)
        },
    )
    val removeAction = ModuleCardAction(
        icon = Icons.Outlined.Delete,
        title = stringResource(R.string.kernel_modules_remove),
        description = stringResource(R.string.kernel_modules_remove_summary),
        enabled = !pending && !module.protected,
        destructive = true,
        onClick = { actions.onRequestRemove(module) },
    )
    val moreActions = listOfNotNull(
        paramsAction,
        logAction,
        detailsAction,
        blockAction,
        removeAction,
    )
    val moreButton = ModuleButtonConfig(
        icon = Icons.Outlined.MoreHoriz,
        text = moreLabel,
        contentDescription = moreLabel,
        onClick = { showMoreActions = true },
    )

    val labels = buildList {
        add(
            ModuleCardLabel(
                text = module.id,
                containerColor = MiuixTheme.colorScheme.primaryContainer,
                contentColor = MiuixTheme.colorScheme.onPrimaryContainer,
            )
        )
        add(
            ModuleCardLabel(
                text = statusText,
                containerColor = statusContainerColor,
                contentColor = statusContentColor,
            )
        )
        if (module.protected) {
            add(
                ModuleCardLabel(
                    text = stringResource(R.string.kernel_modules_protected),
                    containerColor = MiuixTheme.colorScheme.secondaryContainer,
                    contentColor = MiuixTheme.colorScheme.onSecondaryContainer,
                )
            )
        }
    }
    val subtitles = if (showMoreInfo) {
        buildList {
            if (module.version.isNotBlank()) {
                add("${stringResource(R.string.module_version)}: ${module.version}")
            }
            if (module.author.isNotBlank()) {
                add("${stringResource(R.string.module_author)}: ${module.author}")
            }
            val internalName = module.internalName
            if (!internalName.isNullOrBlank() && internalName != module.id && internalName != module.name) {
                add("${stringResource(R.string.kernel_modules_internal_name)}: $internalName")
            }
            if (technicalMetadata.isNotBlank()) add(technicalMetadata)
            if (module.vermagic.isNotBlank()) {
                add("${stringResource(R.string.kernel_modules_vermagic)}: ${module.vermagic}")
            }
            add("${stringResource(R.string.kernel_modules_depends)}: $dependencySummary")
        }
    } else {
        emptyList()
    }

    ModuleCardShell(
        title = module.name,
        labels = labels,
        subtitles = subtitles,
        description = module.description.takeIf { showMoreInfo },
        expanded = expanded,
        onClickLabel = when {
            !foldEnabled -> null
            expanded -> collapseLabel
            else -> expandLabel
        },
        onClick = { if (foldEnabled) onExpandToggle() },
        errorText = module.error,
        loading = pending,
        switch = {
            ExpressiveSwitch(
                checked = module.effectiveAutoload,
                enabled = !pending && !autoloadPending && !state.safeMode && module.internalName != null &&
                    !module.protected,
                onCheckedChange = {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    actions.onToggleAutoload(module, it)
                },
                modifier = Modifier.semantics {
                    contentDescription = autoloadLabel
                },
            )
        },
    ) {
        AnimatedVisibility(
            visible = !foldEnabled || expanded,
            enter = fadeIn(FolkMotion.FadeInOut) + expandVertically(FolkMotion.smoothSpring()),
            exit = shrinkVertically(FolkMotion.smoothSpring()) + fadeOut(FolkMotion.FadeInOut),
        ) {
            Column {
                if (!pending) {
                    Spacer(Modifier.height(16.dp))
                } else {
                    Spacer(Modifier.height(12.dp))
                }
                AdaptiveModuleButtonRow(
                    buttons = listOfNotNull(loadButton),
                    trailingButton = moreButton,
                    iconOnly = iconOnly,
                    spacing = if (iconOnly) 12 else 8,
                )
            }
        }
    }

    ModuleActionsSheet(
        show = showMoreActions,
        title = stringResource(R.string.module_more_actions),
        actions = moreActions,
        onDismiss = { showMoreActions = false },
    )
}

@Composable
private fun stateLabel(module: KernelModule): String = when (module.loaded) {
    LoadState.LOADED -> stringResource(R.string.kernel_modules_state_loaded)
    LoadState.NOT_LOADED -> stringResource(R.string.kernel_modules_state_not_loaded)
    LoadState.UNKNOWN -> stringResource(
        R.string.kernel_modules_state_unknown,
        module.stateError?.let { ": $it" }.orEmpty(),
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun KernelModuleDetailsSheet(
    module: KernelModule,
    onDismiss: () -> Unit,
) {
    FolkBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberBottomSheetState(
            initialValue = SheetValue.Hidden,
            enabledValues = setOf(SheetValue.Hidden, SheetValue.Expanded),
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .verticalScroll(rememberScrollState())
                .padding(start = 24.dp, end = 24.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(module.name, style = MiuixTheme.textStyles.title2)
            Text(
                stateLabel(module),
                style = MiuixTheme.textStyles.body2,
                color = when (module.loaded) {
                    LoadState.LOADED -> MiuixTheme.colorScheme.primary
                    LoadState.UNKNOWN -> MiuixTheme.colorScheme.error
                    LoadState.NOT_LOADED -> MiuixTheme.colorScheme.onSurfaceVariantSummary
                },
            )
            if (module.description.isNotBlank()) {
                Text(module.description, style = MiuixTheme.textStyles.body2)
            }

            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            Text(
                stringResource(R.string.kernel_modules_basic_info),
                style = MiuixTheme.textStyles.headline1,
            )
            DetailRow(
                stringResource(R.string.kernel_modules_version),
                module.version.ifBlank { stringResource(R.string.kernel_modules_unknown) },
            )
            DetailRow(
                stringResource(R.string.kernel_modules_author),
                module.author.ifBlank { stringResource(R.string.kernel_modules_unknown) },
            )
            DetailRow(
                stringResource(R.string.kernel_modules_file_size),
                Formatter.formatShortFileSize(LocalContext.current, module.fileSize),
            )
            if (module.depends.isNotEmpty()) {
                DetailRow(
                    stringResource(R.string.kernel_modules_depends),
                    module.depends.joinToString(", "),
                )
            }
            if (module.parameters.isNotEmpty()) {
                DetailRow(
                    stringResource(R.string.kernel_modules_parameters),
                    module.parameters.joinToString("\n") {
                        it.name + (it.valueType?.let { type -> " ($type)" } ?: "")
                    },
                )
            }

            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            Text(
                stringResource(R.string.kernel_modules_technical_info),
                style = MiuixTheme.textStyles.headline1,
            )
            SelectionContainer {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    DetailRow(stringResource(R.string.kernel_modules_id), module.id)
                    module.internalName?.let {
                        DetailRow(stringResource(R.string.kernel_modules_internal_name), it)
                    }
                    module.architecture.takeIf { it.isNotBlank() }?.let {
                        DetailRow(stringResource(R.string.kernel_modules_architecture), it)
                    }
                    module.vermagic.takeIf { it.isNotBlank() }?.let {
                        DetailRow(stringResource(R.string.kernel_modules_vermagic), it)
                    }
                    DetailRow(stringResource(R.string.kernel_modules_sha256), module.sha256)
                    module.error?.let {
                        DetailRow(stringResource(R.string.kernel_modules_error), it)
                    }
                    if (module.diagnostics.isNotEmpty()) {
                        DetailRow(
                            stringResource(R.string.kernel_modules_diagnostics),
                            module.diagnostics.joinToString("\n"),
                        )
                    }
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                horizontalArrangement = Arrangement.End,
            ) {
                FolkTextButton(onClick = onDismiss) {
                    Text(stringResource(R.string.close))
                }
            }
        }
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Column {
        Text(label, style = MiuixTheme.textStyles.footnote2)
        Text(value, style = MiuixTheme.textStyles.body2)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun KernelModuleParamsSheet(
    module: KernelModule,
    draft: String,
    pending: Boolean,
    onDraftChange: (String) -> Unit,
    onSave: () -> Unit,
    onDismiss: () -> Unit,
) {
    FolkBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberBottomSheetState(
            initialValue = SheetValue.Hidden,
            enabledValues = setOf(SheetValue.Hidden, SheetValue.Expanded),
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .imePadding()
                .navigationBarsPadding()
                .verticalScroll(rememberScrollState())
                .padding(start = 24.dp, end = 24.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                stringResource(R.string.kernel_modules_params_title, module.name),
                style = MiuixTheme.textStyles.title3,
            )
            Text(
                stringResource(R.string.kernel_modules_params_help),
                style = MiuixTheme.textStyles.body2,
                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
            OutlinedTextField(
                value = draft,
                onValueChange = onDraftChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text(stringResource(R.string.kernel_modules_params_field)) },
                minLines = 3,
            )
            if (module.parameters.isNotEmpty()) {
                Text(
                    module.parameters.joinToString("\n") {
                        it.name + (it.valueType?.let { type -> ": $type" } ?: "")
                    },
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
            ) {
                FolkTextButton(onClick = onDismiss) {
                    Text(stringResource(android.R.string.cancel))
                }
                FolkFilledTonalButton(
                    enabled = !pending,
                    onClick = onSave,
                ) {
                    Text(stringResource(R.string.kernel_modules_save_params))
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun KernelModuleLogSheet(
    module: KernelModule,
    loading: Boolean,
    result: me.weishu.kernelsu.data.model.KernelModuleLastResult?,
    onDismiss: () -> Unit,
) {
    FolkBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberBottomSheetState(
            initialValue = SheetValue.Hidden,
            enabledValues = setOf(SheetValue.Hidden, SheetValue.Expanded),
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(start = 24.dp, end = 24.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                stringResource(R.string.kernel_modules_log_title, module.name),
                style = MiuixTheme.textStyles.title3,
            )
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 520.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                if (loading) {
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                } else if (result == null) {
                    Text(stringResource(R.string.kernel_modules_no_log))
                } else {
                    SelectionContainer {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            if (result.message.isNotBlank()) {
                                Text(result.message, style = MiuixTheme.textStyles.body2)
                            }
                            DetailRow(
                                stringResource(R.string.kernel_modules_log_operation),
                                result.operation,
                            )
                            DetailRow(
                                stringResource(R.string.kernel_modules_log_outcome),
                                result.outcome,
                            )
                            DetailRow(
                                stringResource(R.string.kernel_modules_log_boot),
                                result.bootId,
                            )
                        }
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
            ) {
                FolkTextButton(onClick = onDismiss) {
                    Text(stringResource(R.string.close))
                }
            }
        }
    }
}
