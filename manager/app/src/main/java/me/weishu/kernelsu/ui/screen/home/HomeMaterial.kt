package me.weishu.kernelsu.ui.screen.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.ui.util.useFullFeaturedLayout
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.expressiveTopAppBarColors
import me.weishu.kernelsu.ui.component.material.FolkIconButton
import me.weishu.kernelsu.ui.component.rebootlistpopup.RebootListPopup
import me.weishu.kernelsu.ui.navigation3.LocalNavOriginState
import me.weishu.kernelsu.ui.navigation3.captureNavOrigin
import me.weishu.kernelsu.ui.navigation3.navThemeSignature
import androidx.compose.foundation.layout.RowScope
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.material3.TopAppBar as MaterialTopAppBar
import me.weishu.kernelsu.media.VisualMediaConfig
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.ScrollBehavior
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.theme.MiuixTheme


@Composable
fun HomePagerMaterial(
    state: HomeUiState,
    actions: HomeActions,
    bottomInnerPadding: Dp,
    superuserCount: Int = 0,
    moduleEnabledCount: Int = 0,
) {
    val scrollBehavior = MiuixScrollBehavior()
    // miuix's scaffold, not the Material one the rest of the app uses: it measures a top bar whose
    // height changes as it collapses, which a Material scaffold would answer by moving the content.
    MiuixScaffold(
        topBar = { TopBar(state = state, actions = actions, scrollBehavior = scrollBehavior) },
        containerColor = MiuixTheme.colorScheme.surface,
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // The order the home page has always had in the reference build: what state the kernel
            // is in, then anything the user has to act on, then the two tallies, then the facts.
            StatusCard(state = state, actions = actions)
            HomeWarnings(state = state, actions = actions)
            if (useFullFeaturedLayout()) {
                StatsTiles(
                    superuserCount = superuserCount,
                    moduleEnabledCount = moduleEnabledCount,
                    onOpenSuperUser = actions.onOpenSuperUser,
                    onOpenModule = actions.onOpenModule,
                )
            }
            InfoCard(
                systemInfo = state.systemInfo,
                susfs = state.susfs,
                zygiskProvider = state.zygiskProvider,
                onSusfs = actions.onSusfs,
            )
            Spacer(
                Modifier.height(
                    bottomInnerPadding + if (!useFullFeaturedLayout())
                        WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() else 0.dp
                )
            )
        }
    }
}

@Composable
private fun TopBar(
    state: HomeUiState,
    actions: HomeActions,
    scrollBehavior: ScrollBehavior,
) {
    val context = LocalContext.current
    // A configured title image keeps the Material bar it was designed for: miuix's bar takes a
    // string, and this app lets that string be replaced by a picture.
    val customTitleImage = VisualMediaConfig.titleEnabled &&
        VisualMediaConfig.file(context, false) != null
    val originState = LocalNavOriginState.current
    val density = LocalDensity.current
    val view = LocalView.current
    val originColorScheme = MaterialTheme.colorScheme
    val originColor = originColorScheme.surfaceContainer
    val originThemeSignature = navThemeSignature(originColorScheme)
    var installBounds by remember { mutableStateOf(Rect.Zero) }
    val barActions: @Composable RowScope.() -> Unit = {
        if (state.susfs.detected) {
            FolkIconButton(onClick = actions.onSusfs) {
                Icon(
                    imageVector = androidx.compose.material.icons.Icons.Rounded.VisibilityOff,
                    contentDescription = stringResource(R.string.susfs_title),
                )
            }
        }
        FolkIconButton(
            onClick = {
                if (installBounds != Rect.Zero) {
                    originState.recordOrigin(
                        captureNavOrigin(
                            view = view,
                            rect = installBounds,
                            cornerRadius = with(density) { 24.dp.toPx() },
                            color = originColor,
                            themeSignature = originThemeSignature,
                        ),
                    )
                }
                actions.onInstallClick()
            },
            modifier = Modifier.onGloballyPositioned { installBounds = it.boundsInRoot() },
            enabled = !state.isLateLoadMode,
        ) {
            Icon(
                imageVector = Icons.Filled.AutoFixHigh,
                contentDescription = stringResource(R.string.install),
            )
        }
        RebootListPopup()
    }
    if (customTitleImage) {
        MaterialTopAppBar(
            title = { me.weishu.kernelsu.ui.component.HomeTitleImage() },
            actions = barActions,
            colors = expressiveTopAppBarColors(),
            windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        )
    } else {
        MiuixTopAppBar(
            title = me.weishu.kernelsu.ui.component.homeTitleText(),
            color = MiuixTheme.colorScheme.surface,
            scrollBehavior = scrollBehavior,
            actions = barActions,
        )
    }
}
