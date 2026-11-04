package me.weishu.kernelsu.ui.component.material

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import top.yukonga.miuix.kmp.basic.Snackbar
import top.yukonga.miuix.kmp.basic.SnackbarData
import top.yukonga.miuix.kmp.basic.SnackbarHost
import top.yukonga.miuix.kmp.basic.SnackbarHostState

/**
 * The app's snackbar host, drawn by miuix.
 *
 * It used to wrap Material's host in a `SwipeToDismissBox` to get swipe-to-dismiss; miuix's host
 * has `canSwipeToDismiss` for that, so the wrapper is now just the host.
 */
@Composable
fun SnackBarHost(
    modifier: Modifier = Modifier,
    hostState: SnackbarHostState,
    snackBar: @Composable (SnackbarData) -> Unit = { Snackbar(it) },
) {
    SnackbarHost(
        state = hostState,
        modifier = modifier,
        canSwipeToDismiss = true,
        content = snackBar,
    )
}
