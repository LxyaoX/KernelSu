package me.weishu.kernelsu.ui.component.material

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * The back button the top bar of every pushed page uses, drawn by miuix.
 *
 * It used to be Material's icon button with a `surfaceContainerHighest` disc behind it; miuix's
 * icon button paints its own background from the scheme, so the colours are gone.
 */
@Composable
fun TopBarBackButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
    horizontalPadding: Dp = 10.dp,
) {
    IconButton(
        onClick = onClick,
        modifier = modifier.padding(horizontal = horizontalPadding),
        backgroundColor = MiuixTheme.colorScheme.surfaceContainerHigh,
    ) {
        Icon(
            imageVector = Icons.AutoMirrored.Rounded.ArrowBack,
            contentDescription = contentDescription,
            tint = MiuixTheme.colorScheme.onSurface,
        )
    }
}
