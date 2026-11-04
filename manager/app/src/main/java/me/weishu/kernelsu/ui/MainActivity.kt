package me.weishu.kernelsu.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleOut
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerDefaults.flingBehavior
import androidx.compose.foundation.pager.PagerDefaults.pageNestedScrollConnection
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation3.runtime.NavKey
import androidx.navigation3.runtime.entryProvider
import androidx.navigation3.runtime.rememberDecoratedNavEntries
import androidx.navigation3.runtime.rememberSaveableStateHolderNavEntryDecorator
import androidx.navigation3.scene.Scene
import androidx.navigation3.scene.SceneInfo
import androidx.navigation3.scene.SinglePaneSceneStrategy
import androidx.navigation3.scene.rememberSceneState
import androidx.navigation3.ui.LocalNavAnimatedContentScope
import androidx.navigation3.ui.NavDisplay
import androidx.navigation3.ui.defaultTransitionSpec
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.NavigationEventTransitionState.InProgress
import androidx.navigationevent.compose.NavigationBackHandler
import androidx.navigationevent.compose.rememberNavigationEventState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.ui.component.bottombar.BottomBar
import me.weishu.kernelsu.ui.component.bottombar.BottomBarDestination
import me.weishu.kernelsu.ui.component.bottombar.FloatingBar
import me.weishu.kernelsu.ui.component.bottombar.FloatingBarConfig
import me.weishu.kernelsu.ui.component.bottombar.FloatingBarVisibilityState
import me.weishu.kernelsu.ui.component.bottombar.LocalFloatingBarReservedHeight
import me.weishu.kernelsu.ui.component.bottombar.LocalFloatingBarVisible
import me.weishu.kernelsu.ui.component.bottombar.LocalIsFloatingNavMode
import me.weishu.kernelsu.ui.component.bottombar.MainPagerState
import me.weishu.kernelsu.ui.component.bottombar.NavBarVisibilityConfig
import me.weishu.kernelsu.ui.component.bottombar.NavMode
import me.weishu.kernelsu.ui.component.bottombar.NavModeConfig
import me.weishu.kernelsu.ui.component.bottombar.NavigationBadgeState
import me.weishu.kernelsu.ui.component.bottombar.SideRail
import me.weishu.kernelsu.ui.component.bottombar.floatingBarReservedHeight
import me.weishu.kernelsu.ui.component.bottombar.rememberFloatingBarVisibilityState
import me.weishu.kernelsu.ui.component.bottombar.rememberMainPagerState
import me.weishu.kernelsu.ui.component.bottombar.useNavigationRail
import me.weishu.kernelsu.ui.component.onboarding.FirstRunGuide
import me.weishu.kernelsu.ui.navigation3.EXTRA_INCOMING_URIS
import me.weishu.kernelsu.ui.navigation3.IntentDispatcher
import me.weishu.kernelsu.ui.navigation3.LocalNavOriginState
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.navigation3.NavOriginState
import me.weishu.kernelsu.ui.navigation3.Navigator
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.navigation3.rememberNavContainerTransformDecorator
import me.weishu.kernelsu.ui.navigation3.rememberNavOriginState
import me.weishu.kernelsu.ui.navigation3.rememberNavigator
import me.weishu.kernelsu.ui.navigation3.trackNavPointer
import me.weishu.kernelsu.ui.screen.about.AboutScreen
import me.weishu.kernelsu.ui.screen.appprofile.AppProfileScreen
import me.weishu.kernelsu.ui.screen.colorpalette.ColorPaletteScreen
import me.weishu.kernelsu.ui.screen.executemoduleaction.ExecuteModuleActionScreen
import me.weishu.kernelsu.ui.screen.flash.FlashScreen
import me.weishu.kernelsu.ui.screen.home.HomePager
import me.weishu.kernelsu.ui.screen.install.InstallScreen
import me.weishu.kernelsu.ui.screen.module.ModulePager
import me.weishu.kernelsu.ui.screen.modulerepo.ModuleRepoDetailScreen
import me.weishu.kernelsu.ui.screen.modulerepo.ModuleRepoScreen
import me.weishu.kernelsu.ui.screen.settings.CategorySettingsScreen
import me.weishu.kernelsu.ui.screen.settings.LanguagePickerScreen
import me.weishu.kernelsu.ui.screen.settings.SettingPager
import me.weishu.kernelsu.ui.screen.sulog.SulogScreen
import me.weishu.kernelsu.ui.screen.superuser.SuperUserPager
import me.weishu.kernelsu.ui.screen.template.AppProfileTemplateScreen
import me.weishu.kernelsu.ui.screen.templateeditor.TemplateEditorScreen
import me.weishu.kernelsu.ui.theme.KernelSUTheme
import me.weishu.kernelsu.ui.theme.LocalColorMode
import me.weishu.kernelsu.ui.theme.LocalEnableNavigationBadge
import me.weishu.kernelsu.ui.theme.LocalModuleDescriptionMaxLines
import me.weishu.kernelsu.ui.util.CapabilityRepository
import me.weishu.kernelsu.ui.util.LocalCapabilityState
import me.weishu.kernelsu.ui.util.PagerNavigationSpringSpec
import me.weishu.kernelsu.ui.util.RootShell
import me.weishu.kernelsu.ui.util.getSuperuserCount
import me.weishu.kernelsu.ui.util.install
import me.weishu.kernelsu.ui.util.rememberContentReady
import me.weishu.kernelsu.ui.util.useFullFeaturedLayout
import me.weishu.kernelsu.ui.viewmodel.MainActivityViewModel
import me.weishu.kernelsu.ui.viewmodel.MainPagerConfig
import me.weishu.kernelsu.ui.viewmodel.ModuleViewModel
import me.weishu.kernelsu.ui.viewmodel.SuperUserViewModel

open class MainActivity : FragmentActivity() {
    private val intentChannel = Channel<Intent>(capacity = Channel.BUFFERED)
    private var contentReady = false
    private var splashStartedAt = 0L
    private val splashAnimationDurationMs = 500L

    override fun attachBaseContext(newBase: android.content.Context) {
        super.attachBaseContext(me.weishu.kernelsu.ui.util.AppLanguage.wrap(newBase))
    }

    @SuppressLint("UnusedMaterial3ScaffoldPaddingParameter")
    override fun onCreate(savedInstanceState: Bundle?) {
        val splashScreen = installSplashScreen()
        splashStartedAt = SystemClock.uptimeMillis()
        super.onCreate(savedInstanceState)
        splashScreen.setKeepOnScreenCondition {
            !contentReady || SystemClock.uptimeMillis() - splashStartedAt < splashAnimationDurationMs
        }

        val isManager = Natives.isManager
        if (isManager && Natives.kernelUAPIVersion == Natives.managerUAPIVersion) install()

        // Warm the shell slots and build the first capability snapshot off the main thread.
        RootShell.initialize()
        CapabilityRepository.refresh()

        @Suppress("DEPRECATION")
        val queuedIntents = savedInstanceState?.getParcelableArrayList<Intent>("pending_intents")
        if (queuedIntents != null) {
            queuedIntents.forEach { intentChannel.trySend(it) }
        } else if (savedInstanceState == null) {
            intent?.let(::queueIntent)
        }

        me.weishu.kernelsu.data.LegacyPreferences.purge(this)
        me.weishu.kernelsu.modulecard.ModuleCardConfig.load()
        me.weishu.kernelsu.ui.component.material.SliderTrackConfig.load(this)
        me.weishu.kernelsu.media.VisualMediaConfig.load(this)

        setContent {
            val viewModel = viewModel<MainActivityViewModel>()
            val uiState by viewModel.uiState.collectAsStateWithLifecycle()
            val selectedMainPage by viewModel.selectedMainPage.collectAsStateWithLifecycle()
            val appSettings = uiState.appSettings
            val darkMode = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && isSystemInDarkTheme())

            DisposableEffect(darkMode) {
                enableEdgeToEdge(
                    statusBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                    navigationBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                )
                window.isNavigationBarContrastEnforced = false
                onDispose { }
            }

            val navigator = rememberNavigator(Route.Main)
            val navOriginState = rememberNavOriginState()
            val systemDensity = LocalDensity.current
            val density = remember(systemDensity, uiState.pageScale) {
                Density(systemDensity.density * uiState.pageScale, systemDensity.fontScale)
            }

            val capability by CapabilityRepository.state.collectAsStateWithLifecycle()

            CompositionLocalProvider(
                LocalNavigator provides navigator,
                LocalNavOriginState provides navOriginState,
                LocalDensity provides density,
                LocalColorMode provides appSettings.colorMode.value,
                LocalEnableNavigationBadge provides uiState.enableNavigationBadge,
                LocalModuleDescriptionMaxLines provides uiState.moduleDescriptionMaxLines,
                LocalCapabilityState provides capability,
            ) {
                KernelSUTheme(appSettings = appSettings) {
                    IntentDispatcher(intentChannel = intentChannel)
                    val flashRoute = navigator.current() as? Route.Flash
                    var flashBackEnabled by remember(flashRoute) { mutableStateOf(false) }
                    val navigateBack: () -> Unit = {
                        when (val top = navigator.current()) {
                            is Route.Flash -> if (flashBackEnabled) navigator.pop()
                            is Route.TemplateEditor -> {
                                if (!top.readOnly) {
                                    navigator.setResult("template_edit", true)
                                } else {
                                    navigator.pop()
                                }
                            }

                            else -> navigator.pop()
                        }
                    }
                    val saveableStateHolderDecorator =
                        rememberSaveableStateHolderNavEntryDecorator<NavKey>()
                    val containerTransformDecorator =
                        rememberNavContainerTransformDecorator(navOriginState)
                    val entryDecorators = remember(
                        saveableStateHolderDecorator,
                        containerTransformDecorator,
                    ) {
                        listOf(saveableStateHolderDecorator, containerTransformDecorator)
                    }
                    val layoutDirection = LocalLayoutDirection.current
                    val transition = remember(layoutDirection, navOriginState) {
                        navTransition(layoutDirection, navOriginState)
                    }
                    val popTransition = remember(layoutDirection, navOriginState) {
                        navPopTransition(layoutDirection, navOriginState)
                    }
                    val predictivePopTransition = remember(layoutDirection, navOriginState) {
                        navPredictivePopTransition(layoutDirection, navOriginState)
                    }
                    val mainScreenEntry = remember(viewModel) {
                        @Composable {
                            MainScreen(
                                initialPage = viewModel.selectedMainPage.value,
                                onPageChanged = viewModel::setSelectedMainPage,
                            )
                        }
                    }

                    val navDisplay = @Composable {
                        val decoratedEntries = rememberDecoratedNavEntries(
                            backStack = navigator.backStack,
                            entryDecorators = entryDecorators,
                            entryProvider = entryProvider<NavKey> {
                                entry<Route.Main> { mainScreenEntry() }
                                entry<Route.About> { AboutScreen() }
                                entry<Route.Sulog> { SulogScreen() }
                                entry<Route.Susfs> { me.weishu.kernelsu.ui.screen.SusfsScreen() }
                                entry<Route.YukiZygisk> {
                                    me.weishu.kernelsu.ui.screen.yukizygisk.YukiZygiskScreen()
                                }
                                entry<Route.Teesim> {
                                    me.weishu.kernelsu.ui.screen.teesim.TeesimScreen()
                                }
                                entry<Route.TeesimTargets> {
                                    me.weishu.kernelsu.ui.screen.teesim.TeesimTargetsScreen()
                                }
                                entry<Route.ColorPalette> { ColorPaletteScreen() }
                                entry<Route.AppProfileTemplate> { AppProfileTemplateScreen() }
                                entry<Route.TemplateEditor> { key -> TemplateEditorScreen(key.template, key.readOnly) }
                                entry<Route.AppProfile> { key -> AppProfileScreen(key.uid) }
                                entry<Route.ModuleRepo> { ModuleRepoScreen() }
                                entry<Route.ModuleRepoDetail> { key -> ModuleRepoDetailScreen(key.module) }
                                entry<Route.KernelModules> {
                                    me.weishu.kernelsu.ui.screen.kernelmodule.KernelModuleScreen()
                                }
                                entry<Route.Install> { InstallScreen() }
                                entry<Route.Flash> { key ->
                                    FlashScreen(
                                        key.flashIt,
                                        onBackEnabledChange = { enabled ->
                                            if (navigator.current() == key) flashBackEnabled = enabled
                                        },
                                    )
                                }
                                entry<Route.ExecuteModuleAction> { key ->
                                    ExecuteModuleActionScreen(
                                        key.moduleId,
                                        key.fromShortcut
                                    )
                                }
                                entry<Route.SettingsCategory> { key -> CategorySettingsScreen(key.key) }
                                entry<Route.WebDavBrowser> { key ->
                                    me.weishu.kernelsu.ui.screen.settings.WebDavBrowserScreen(key.path)
                                }
                                entry<Route.BackupList> {
                                    me.weishu.kernelsu.ui.screen.settings.BackupListScreen()
                                }
                                entry<Route.Language> { LanguagePickerScreen() }
                            },
                        )
                        val sceneState = rememberSceneState(
                            entries = decoratedEntries,
                            sceneStrategies = listOf(SinglePaneSceneStrategy()),
                            onBack = navigateBack,
                        )
                        val gestureState = rememberNavigationEventState(
                            currentInfo = SceneInfo(sceneState.currentScene),
                            backInfo = sceneState.previousScenes.map { SceneInfo(it) },
                        )
                        NavigationBackHandler(
                            state = gestureState,
                            isBackEnabled = sceneState.currentScene.previousEntries.isNotEmpty(),
                            onBackCompleted = {
                                navOriginState.markPredictiveCommit()
                                repeat(
                                    decoratedEntries.size -
                                        sceneState.currentScene.previousEntries.size,
                                ) {
                                    navigateBack()
                                }
                            },
                        )
                        LaunchedEffect(gestureState, sceneState, navOriginState) {
                            snapshotFlow { gestureState.transitionState }.collect { gesture ->
                                if (gesture is InProgress) {
                                    navOriginState.updatePredictive(
                                        active = true,
                                        key = sceneState.currentScene.entries.lastOrNull()?.contentKey,
                                        fraction = 1f - gesture.latestEvent.progress,
                                    )
                                } else {
                                    navOriginState.updatePredictive(
                                        active = false,
                                        key = null,
                                        fraction = 1f,
                                    )
                                }
                            }
                        }
                        NavDisplay(
                            sceneState = sceneState,
                            navigationEventState = gestureState,
                            transitionSpec = transition,
                            popTransitionSpec = popTransition,
                            predictivePopTransitionSpec = predictivePopTransition,
                        )
                    }

                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .trackNavPointer(navOriginState)
                    ) {
                        androidx.compose.material3.Scaffold(
                            containerColor = MaterialTheme.colorScheme.surfaceContainer,
                        ) { navDisplay() }
                    }
                    val prefs = remember {
                        ksuApp.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    }
                    var showFirstRunGuide by rememberSaveable {
                        mutableStateOf(
                            savedInstanceState == null &&
                                !prefs.getBoolean("first_run_guide_done", false),
                        )
                    }
                    if (showFirstRunGuide) {
                        FirstRunGuide(
                            onDismiss = {
                                prefs.edit().putBoolean("first_run_guide_done", true).apply()
                                showFirstRunGuide = false
                            },
                        )
                    }
                    SideEffect { contentReady = true }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        queueIntent(intent)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        // Snapshot only unconsumed intents; a dispatcher that already ran has removed its own.
        val queued = arrayListOf<Intent>()
        while (true) {
            val pending = intentChannel.tryReceive().getOrNull() ?: break
            queued.add(pending)
        }
        queued.forEach { intentChannel.trySend(it) }
        outState.putParcelableArrayList("pending_intents", queued)
        super.onSaveInstanceState(outState)
    }

    /** Queue an external intent once, with its file payload flattened for [IntentDispatcher]. */
    private fun queueIntent(intent: Intent) {
        intentChannel.trySend(intent.normalizedForDispatch())
    }
}

/** Some file managers put the payload of a VIEW intent into this legacy list extra. */
private const val EXTRA_LEGACY_URIS = "uris"

/** Internal deep-link scheme; its URI is not a file payload and stays out of the flattened list. */
private const val SCHEME_INTERNAL_DEEP_LINK = "ksu"

/**
 * Collect the external file URIs an inbound intent carries. Each action delivers its payload
 * differently: SEND through `EXTRA_STREAM`, SEND_MULTIPLE through the same extra as a list, and
 * everything else (VIEW from a file manager or the desktop "open with" sheet) through
 * [Intent.getData], with a legacy `"uris"` list as a fallback.
 */
@Suppress("DEPRECATION")
private fun Intent.externalFileUris(): List<Uri> {
    val uris: List<Uri> = when (action) {
        Intent.ACTION_SEND -> listOfNotNull(
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
            } else {
                getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
            }
        )

        Intent.ACTION_SEND_MULTIPLE -> if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)?.toList().orEmpty()
        } else {
            getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)?.toList().orEmpty()
        }

        else -> listOfNotNull(
            data ?: if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                getParcelableArrayListExtra(EXTRA_LEGACY_URIS, Uri::class.java)?.firstOrNull()
            } else {
                getParcelableArrayListExtra<Uri>(EXTRA_LEGACY_URIS)?.firstOrNull()
            }
        )
    }
    return uris.filterNotNull().filter { it.scheme != SCHEME_INTERNAL_DEEP_LINK }
}

/** Attach the flattened payload so the dispatcher resolves every entry point through one list. */
private fun Intent.normalizedForDispatch(): Intent = apply {
    putParcelableArrayListExtra(EXTRA_INCOMING_URIS, ArrayList(externalFileUris()))
}

val LocalMainPagerState = staticCompositionLocalOf<MainPagerState> { error("LocalMainPagerState not provided") }

@SuppressLint("UnusedMaterial3ScaffoldPaddingParameter")
@Composable
fun MainScreen(
    initialPage: Int = 0,
    onPageChanged: (Int) -> Unit = {},
) {
    val navController = LocalNavigator.current
    val useNavigationRail = useNavigationRail()
    val navVisibilityRevision = NavBarVisibilityConfig.revision.collectAsStateWithLifecycle().value
    val destinations = remember(navVisibilityRevision) { NavBarVisibilityConfig.visibleDestinations() }
    // The persisted page is a destination, not a pager slot: hiding a destination moves the slot of
    // everything after it, while the destination the user was on has to stay put.
    val initialDestination = MainPagerConfig.coercePage(initialPage)
    val pagerState = rememberPagerState(
        initialPage = destinations.indexOfFirst { it.ordinal == initialDestination }.coerceAtLeast(0),
        pageCount = { destinations.size },
    )
    val mainPagerState = rememberMainPagerState(
        pagerState = pagerState,
        destinations = destinations,
        animatePageChanges = !useNavigationRail,
    )
    val isFullFeatured = useFullFeaturedLayout()
    var userScrollEnabled by remember(isFullFeatured) { mutableStateOf(isFullFeatured) }

    // Re-probe identity and the root shell whenever the app returns to the foreground, so a
    // grant/revocation done while we were backgrounded is reflected without a restart.
    LifecycleResumeEffect(Unit) {
        CapabilityRepository.refresh()
        onPauseOrDispose { }
    }

    val enableNavigationBadge = LocalEnableNavigationBadge.current
    val badgeEnabled = enableNavigationBadge && isFullFeatured
    val moduleViewModel = viewModel<ModuleViewModel>()
    val superUserViewModel = viewModel<SuperUserViewModel>()
    val grantedUidCount by remember(superUserViewModel) {
        superUserViewModel.uiState
            .map { state -> state.groupedApps.count { it.anyAllowSu } }
            .distinctUntilChanged()
    }.collectAsStateWithLifecycle(0)

    var startupPreloadStarted by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(isFullFeatured) {
        if (!isFullFeatured || startupPreloadStarted) {
            return@LaunchedEffect
        }

        moduleViewModel.initializePreferences()
        val moduleState = moduleViewModel.uiState.value
        if (!moduleState.hasLoaded) {
            if (!moduleState.isRefreshing) moduleViewModel.fetchModuleList()
            moduleViewModel.uiState.first { it.hasLoaded }
        }
        moduleViewModel.syncModuleUpdateInfo(moduleViewModel.uiState.value.modules)

        val superUserState = superUserViewModel.uiState.value
        if (!superUserState.hasLoaded) {
            superUserViewModel.initializePreferences()
            if (superUserState.isRefreshing) {
                superUserViewModel.uiState.first { it.hasLoaded }
            } else {
                superUserViewModel.loadAppList().join()
            }
        }

        startupPreloadStarted = true
    }

    // Loading the app list just for a badge is too expensive; read the kernel allowlist instead.
    var superuserCount by remember { mutableIntStateOf(0) }
    LaunchedEffect(isFullFeatured, grantedUidCount) {
        superuserCount = if (isFullFeatured) withContext(Dispatchers.IO) { getSuperuserCount() } else 0
    }

    val moduleEnabledCount by remember(moduleViewModel) {
        moduleViewModel.uiState
            .map { state -> state.modules.count { it.enabled } }
            .distinctUntilChanged()
    }.collectAsStateWithLifecycle(0)
    val moduleUpdatableCount by remember(moduleViewModel) {
        moduleViewModel.uiState
            .map { state -> state.updateInfo.count { it.value.downloadUrl.isNotBlank() } }
            .distinctUntilChanged()
    }.collectAsStateWithLifecycle(0)

    val navigationBadge = if (badgeEnabled) {
        NavigationBadgeState(
            superuserCount = superuserCount,
            moduleEnabledCount = moduleEnabledCount,
            moduleUpdatableCount = moduleUpdatableCount,
        )
    } else {
        NavigationBadgeState()
    }
    PagerSyncEffects(mainPagerState, destinations, onPageChanged)

    // A destination that was hidden releases its slot; the others move up instead of the user
    // being thrown to another page.
    LaunchedEffect(destinations) {
        mainPagerState.syncToDestinations()
    }

    MainScreenBackHandler(mainPagerState, navController)

    CompositionLocalProvider(
        LocalMainPagerState provides mainPagerState
    ) {
        val contentReady = rememberContentReady()
        val renderPagerPages = contentReady || mainPagerState.isNavigating
        val floatingBarRevision = FloatingBarConfig.revision.collectAsStateWithLifecycle().value
        val navModeRevision = NavModeConfig.revision.collectAsStateWithLifecycle().value
        val navMode = remember(navModeRevision) { NavModeConfig.mode }
        val floatingBarEnabled = navMode == NavMode.Floating
        val floatingBarStyle = remember(floatingBarRevision) { FloatingBarConfig.style }
        val floatingBarCompact = remember(floatingBarRevision) { FloatingBarConfig.compact }
        val floatingAutoHide = remember(floatingBarRevision) { FloatingBarConfig.autoHide }
        val floatingSwipeHide = remember(floatingBarRevision) { FloatingBarConfig.swipeHide }
        val floatingBarVisibility = rememberFloatingBarVisibilityState(
            enabled = floatingBarEnabled,
            autoHide = floatingAutoHide,
            swipeHide = floatingSwipeHide,
        )
        val pagerContent = @Composable { bottomInnerPadding: Dp ->
            HorizontalPager(
                state = mainPagerState.pagerState,
                beyondViewportPageCount = if (renderPagerPages) 1 else 0,
                overscrollEffect = null,
                userScrollEnabled = userScrollEnabled,
                pageNestedScrollConnection = pageNestedScrollConnection(
                    state = mainPagerState.pagerState,
                    orientation = androidx.compose.foundation.gestures.Orientation.Horizontal,
                ),
                flingBehavior = flingBehavior(
                    state = mainPagerState.pagerState,
                    snapAnimationSpec = PagerNavigationSpringSpec,
                ),
            ) { page ->
                val isCurrentPage = page == mainPagerState.pagerState.settledPage
                when (destinations.getOrNull(page)) {
                    BottomBarDestination.Home -> if (renderPagerPages || isCurrentPage) HomePager(navController, bottomInnerPadding, isCurrentPage, superuserCount, moduleEnabledCount)
                    BottomBarDestination.SuperUser -> if (renderPagerPages || isCurrentPage) SuperUserPager(navController, bottomInnerPadding, isCurrentPage)
                    BottomBarDestination.Module -> if (renderPagerPages || isCurrentPage) ModulePager(bottomInnerPadding, isCurrentPage)
                    BottomBarDestination.Setting -> if (renderPagerPages || isCurrentPage) SettingPager(navController, bottomInnerPadding, isCurrentPage)
                    null -> Unit
                }
            }
        }

        if (useNavigationRail) {
            val startInsets = WindowInsets.systemBars.union(WindowInsets.displayCutout)
                .only(WindowInsetsSides.Start)
            val navBarBottomPadding = WindowInsets.systemBars.asPaddingValues().calculateBottomPadding()

            androidx.compose.material3.Scaffold(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ) {
                Row {
                    SideRail(
                        navigationBadge = navigationBadge,
                    )
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .consumeWindowInsets(startInsets)
                    ) {
                        pagerContent(navBarBottomPadding)
                    }
                }
            }
        } else if (floatingBarEnabled) {
            FloatingNavHost(
                visibility = floatingBarVisibility,
                autoHide = floatingAutoHide,
                swipeHide = floatingSwipeHide,
                reservedHeight = floatingBarReservedHeight(floatingBarCompact, floatingBarStyle),
                navigationBadge = navigationBadge,
                onUserInteraction = { floatingBarVisibility.onUserInteraction() },
                pagerContent = pagerContent,
            )
        } else {
            val bottomBar = @Composable {
                BottomBar(navigationBadge)
            }

            androidx.compose.material3.Scaffold(
                bottomBar = bottomBar,
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ) { innerPadding ->
                pagerContent(innerPadding.calculateBottomPadding())
            }
        }
    }
}

@Composable
private fun FloatingNavHost(
    visibility: FloatingBarVisibilityState,
    autoHide: Boolean,
    swipeHide: Boolean,
    reservedHeight: Dp,
    navigationBadge: NavigationBadgeState,
    onUserInteraction: () -> Unit,
    pagerContent: @Composable (Dp) -> Unit,
) {
    val barVisible = when {
        !autoHide && !swipeHide -> true
        !autoHide -> !visibility.isScrollingDown
        !swipeHide -> visibility.visible
        else -> visibility.visible && !visibility.isScrollingDown
    }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .nestedScroll(visibility.connection)
    ) {
        CompositionLocalProvider(
            LocalIsFloatingNavMode provides true,
            LocalFloatingBarVisible provides barVisible,
            LocalFloatingBarReservedHeight provides reservedHeight,
        ) {
            pagerContent(
                reservedHeight +
                    WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
            )
            AnimatedVisibility(
                visible = barVisible,
                modifier = Modifier.align(Alignment.BottomCenter),
                enter = slideInVertically(initialOffsetY = { it }) + fadeIn(),
                exit = slideOutVertically(targetOffsetY = { it }) + fadeOut(),
            ) {
                FloatingBar(
                    navigationBadge = navigationBadge,
                    onUserInteraction = onUserInteraction,
                )
            }
        }
    }
}

@Composable
private fun PagerSyncEffects(
    mainPagerState: MainPagerState,
    destinations: List<BottomBarDestination>,
    onPageChanged: (Int) -> Unit,
) {
    val livePage = mainPagerState.pagerState.currentPage
    LaunchedEffect(livePage) {
        destinations.getOrNull(livePage)?.let { onPageChanged(it.ordinal) }
    }
    LaunchedEffect(livePage) {
        mainPagerState.syncPage()
    }
}

private fun navTransition(
    layoutDirection: LayoutDirection,
    originState: NavOriginState,
): AnimatedContentTransitionScope<Scene<NavKey>>.() -> ContentTransform = {
    val expandable = originState.hasExpandableOrigin()
    val targetKey = targetState.entries.lastOrNull()?.contentKey
    if (expandable) {
        targetKey?.let { key ->
            val origin = originState.consumePendingIfFresh()
            if (origin != null) {
                originState.putOrigin(key, origin)
            } else {
                originState.removeOrigin(key)
            }
        }
    }
    if (originState.hasCachedOrigin(targetKey)) {
        containerPushTransform()
    } else {
        targetKey?.let(originState::removeOrigin)
        slideTransition(layoutDirection).invoke(this)
    }
}

private fun navPopTransition(
    layoutDirection: LayoutDirection,
    originState: NavOriginState,
): AnimatedContentTransitionScope<Scene<NavKey>>.() -> ContentTransform = {
    val sourceKey = initialState.entries.lastOrNull()?.contentKey
    if (originState.hasCachedOrigin(sourceKey)) {
        cardPopTransform()
    } else {
        containerPopTransform()
    }
}

private fun navPredictivePopTransition(
    layoutDirection: LayoutDirection,
    originState: NavOriginState,
): AnimatedContentTransitionScope<Scene<NavKey>>.(Int) -> ContentTransform = {
    val sourceKey = initialState.entries.lastOrNull()?.contentKey
    if (originState.hasCachedOrigin(sourceKey)) {
        cardPopTransform()
    } else {
        containerPopTransform()
    }
}

private fun containerPushTransform(): ContentTransform =
    ContentTransform(
        targetContentEnter = fadeIn(
            animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
            initialAlpha = 0.999f,
        ),
        initialContentExit = fadeOut(
            animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
            targetAlpha = 0.999f,
        ),
    )

private fun containerPopTransform(): ContentTransform =
    ContentTransform(
        targetContentEnter = EnterTransition.None,
        initialContentExit = scaleOut(
            animationSpec = tween(durationMillis = 280, easing = FastOutSlowInEasing),
            targetScale = 0.86f,
            transformOrigin = TransformOrigin(0.08f, 0.08f),
        ) + fadeOut(
            animationSpec = tween(durationMillis = 240, easing = FastOutSlowInEasing),
        ),
        targetContentZIndex = -1f,
    )

/**
 * Keeps the outgoing scene alive for the card-shrink draw without transforming the whole page.
 * The 0.999 end alpha gives the exit a duration while remaining visually opaque.
 */
private fun cardPopTransform(): ContentTransform =
    ContentTransform(
        targetContentEnter = EnterTransition.None,
        initialContentExit = fadeOut(
            animationSpec = tween(durationMillis = 280, easing = FastOutSlowInEasing),
            targetAlpha = 0.999f,
        ),
        targetContentZIndex = -1f,
    )

private fun slideTransition(
    layoutDirection: LayoutDirection,
): AnimatedContentTransitionScope<Scene<NavKey>>.() -> ContentTransform = {
    val initialCount = initialState.entries.size + initialState.previousEntries.size
    val targetCount = targetState.entries.size + targetState.previousEntries.size
    if (targetCount < initialCount) {
        slideBackTransform(layoutDirection)
    } else {
        defaultTransitionSpec<NavKey>().invoke(this)
    }
}

private fun slideBackTransform(layoutDirection: LayoutDirection): ContentTransform =
    ContentTransform(
        targetContentEnter = EnterTransition.None,
        initialContentExit = slideOutHorizontally(
            animationSpec = tween(durationMillis = 280, easing = FastOutSlowInEasing),
            targetOffsetX = { width ->
                if (layoutDirection == LayoutDirection.Rtl) -width else width
            },
        ),
        targetContentZIndex = -1f,
    )

@Composable
private fun MainScreenBackHandler(
    mainState: MainPagerState,
    navController: Navigator,
) {
    val navAnimatedContentScope = LocalNavAnimatedContentScope.current
    val isPagerBackHandlerEnabled by remember(navAnimatedContentScope) {
        derivedStateOf {
            navController.current() is Route.Main && navController.backStackSize() == 1 &&
                mainState.selectedDestination != BottomBarDestination.Home &&
                !navController.wasJustPopped() &&
                !navAnimatedContentScope.transition.isRunning
        }
    }

    val navEventState = rememberNavigationEventState(NavigationEventInfo.None)

    NavigationBackHandler(
        state = navEventState,
        isBackEnabled = isPagerBackHandlerEnabled,
        onBackCompleted = {
            mainState.animateToDestination(BottomBarDestination.Home)
        }
    )
}
