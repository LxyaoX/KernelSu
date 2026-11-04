package me.weishu.kernelsu.ui.component.material

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Search
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.ScrollBehavior
import top.yukonga.miuix.kmp.basic.SnackbarHostState
import top.yukonga.miuix.kmp.basic.Surface
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * The search surface every list page shares: a bar carrying the page title, an inline search field,
 * and a full-screen layer with the shortcuts or the results once the field is opened.
 *
 * It used to be Material's contained search bar (`LargeFlexibleTopAppBar` plus
 * `ExpandedFullScreenContainedSearchBar`) with its own scroll-fraction collapse animation. miuix has
 * no equivalent, so the bar is a plain row on the miuix surface colour and the full-screen layer is
 * a dialog. The collapse animation is gone - the title no longer shrinks away while scrolling - and
 * `defaultContent`/`searchContent` keep the contract they had.
 */
@Composable
fun SearchAppBar(
    title: @Composable () -> Unit,
    searchText: String,
    onSearchTextChange: (String) -> Unit,
    onClearClick: () -> Unit,
    snackbarHostState: SnackbarHostState,
    navigationIcon: @Composable (() -> Unit)? = null,
    actions: @Composable (() -> Unit)? = null,
    scrollBehavior: ScrollBehavior? = null,
    largeTitle: Boolean = true,
    topSearch: Boolean = false,
    searchBeforeActions: Boolean = false,
    collapsibleSearch: Boolean = false,
    searchPlaceholder: String = "",
    defaultContent: @Composable BoxScope.(bottomPadding: Dp, closeSearch: () -> Unit) -> Unit = { _, _ -> },
    searchContent: @Composable BoxScope.(bottomPadding: Dp, closeSearch: () -> Unit) -> Unit = { _, _ -> },
) {
    val keyboardController = LocalSoftwareKeyboardController.current
    val focusManager = LocalFocusManager.current
    var searchOpen by remember { mutableStateOf(false) }

    val closeSearch: () -> Unit = {
        searchOpen = false
        focusManager.clearFocus()
        keyboardController?.hide()
    }

    val searchIcon: @Composable () -> Unit = {
        FolkIconButton(onClick = { searchOpen = true }) {
            Icon(Icons.Filled.Search, contentDescription = null)
        }
    }

    Column(modifier = Modifier.fillMaxWidth()) {
        Surface(color = MiuixTheme.colorScheme.surface) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .windowInsetsPadding(
                        WindowInsets.safeDrawing.only(
                            WindowInsetsSides.Top + WindowInsetsSides.Horizontal
                        )
                    )
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(if (largeTitle) 64.dp else 56.dp)
                        .padding(horizontal = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    navigationIcon?.invoke()
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .padding(start = if (navigationIcon == null) 16.dp else 4.dp)
                    ) {
                        title()
                    }
                    val actionSlot: @Composable RowScope.() -> Unit = {
                        if (searchBeforeActions && topSearch) searchIcon()
                        actions?.invoke()
                        if (!searchBeforeActions && topSearch) searchIcon()
                    }
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(2.dp),
                        content = actionSlot,
                    )
                }
                SearchField(
                    value = searchText,
                    onValueChange = onSearchTextChange,
                    onClearClick = onClearClick,
                    placeholder = searchPlaceholder,
                    editable = false,
                    onOpen = { searchOpen = true },
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                )
                Box(modifier = Modifier.height(8.dp))
            }
        }
    }

    if (searchOpen) {
        Dialog(
            onDismissRequest = closeSearch,
            properties = DialogProperties(usePlatformDefaultWidth = false),
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(MiuixTheme.colorScheme.surface)
                    .statusBarsPadding()
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 4.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    FolkIconButton(
                        onClick = closeSearch,
                        modifier = Modifier.padding(horizontal = 8.dp),
                    ) {
                        Icon(Icons.Filled.Search, contentDescription = null)
                    }
                    SearchField(
                        value = searchText,
                        onValueChange = onSearchTextChange,
                        onClearClick = onClearClick,
                        placeholder = searchPlaceholder,
                        editable = true,
                        modifier = Modifier
                            .weight(1f)
                            .padding(end = 12.dp),
                    )
                }
                Box(modifier = Modifier.fillMaxSize()) {
                    if (searchText.isNotEmpty()) {
                        searchContent(0.dp, closeSearch)
                    } else {
                        defaultContent(0.dp, closeSearch)
                    }
                    Box(
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .navigationBarsPadding()
                            .imePadding()
                            .padding(bottom = 16.dp)
                    ) {
                        SnackBarHost(hostState = snackbarHostState)
                    }
                }
            }
        }
    }
}

/**
 * The rounded field under the bar and inside the search layer.
 *
 * The inline copy is read-only: tapping it opens the search layer, which owns the only editable
 * field. That keeps one keyboard and one cursor for the query, which is what the contained search
 * bar did as well.
 */
@Composable
private fun SearchField(
    value: String,
    onValueChange: (String) -> Unit,
    onClearClick: () -> Unit,
    placeholder: String,
    editable: Boolean,
    modifier: Modifier = Modifier,
    onOpen: (() -> Unit)? = null,
) {
    val keyboardController = LocalSoftwareKeyboardController.current
    val focusRequester = remember { FocusRequester() }
    if (editable) {
        LaunchedEffect(Unit) { focusRequester.requestFocus() }
    }
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = MiuixTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(percent = 50),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .then(
                    if (onOpen != null) {
                        Modifier.clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null,
                            onClick = onOpen,
                        )
                    } else {
                        Modifier
                    }
                )
                .padding(horizontal = 14.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Icon(
                imageVector = Icons.Filled.Search,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                tint = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
            Box(modifier = Modifier.weight(1f)) {
                if (value.isEmpty()) {
                    Text(
                        text = placeholder,
                        style = MiuixTheme.textStyles.body2,
                        color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        maxLines = 1,
                    )
                }
                if (editable) {
                    BasicTextField(
                        value = value,
                        onValueChange = onValueChange,
                        modifier = Modifier
                            .fillMaxWidth()
                            .focusRequester(focusRequester),
                        singleLine = true,
                        textStyle = TextStyle(
                            color = MiuixTheme.colorScheme.onSurface,
                            fontSize = MiuixTheme.textStyles.body2.fontSize,
                        ),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                        keyboardActions = KeyboardActions(
                            onSearch = { keyboardController?.hide() },
                        ),
                        cursorBrush = SolidColor(MiuixTheme.colorScheme.primary),
                    )
                } else if (value.isNotEmpty()) {
                    Text(
                        text = value,
                        style = MiuixTheme.textStyles.body2,
                        color = MiuixTheme.colorScheme.onSurface,
                        maxLines = 1,
                    )
                }
            }
            if (value.isNotEmpty()) {
                IconButton(
                    onClick = onClearClick,
                    backgroundColor = Color.Transparent,
                    minHeight = 20.dp,
                    minWidth = 20.dp,
                ) {
                    Icon(
                        imageVector = Icons.Filled.Clear,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                        tint = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                    )
                }
            }
        }
    }
}

/**
 * Kept for source compatibility: pages ask for a scroll behaviour to hand to the bar and to
 * `nestedScroll`. miuix's own behaviour is the only one in play now, so the Material one they used
 * to pass is ignored.
 */
@Composable
fun rememberSearchAppBarScrollBehavior(
    @Suppress("UNUSED_PARAMETER") topAppBarScrollBehavior: ScrollBehavior? = null,
): ScrollBehavior = MiuixScrollBehavior()
