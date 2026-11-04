package me.weishu.kernelsu.ui.component.bottombar

import top.yukonga.miuix.kmp.basic.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource

/** Renders a bottom navigation destination icon in the tint its selection implies. */
@Composable
fun NavBarIcon(
    destination: BottomBarDestination,
    isSelected: Boolean,
    modifier: Modifier = Modifier,
    tint: Color = if (isSelected) {
        MaterialTheme.colorScheme.onSecondaryContainer
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    },
) {
    Icon(
        imageVector = if (isSelected) destination.iconSelected else destination.iconNotSelected,
        contentDescription = stringResource(destination.label),
        tint = tint,
        modifier = modifier,
    )
}
