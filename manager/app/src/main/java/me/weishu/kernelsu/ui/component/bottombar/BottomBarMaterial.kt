package me.weishu.kernelsu.ui.component.bottombar

import androidx.compose.material3.MaterialTheme
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.weishu.kernelsu.ui.LocalMainPagerState
import me.weishu.kernelsu.ui.util.useFullFeaturedLayout
import top.yukonga.miuix.kmp.basic.Badge
import top.yukonga.miuix.kmp.basic.NavigationBar
import top.yukonga.miuix.kmp.basic.NavigationBarItem

/**
 * The docked bar, drawn with miuix's navigation components.
 *
 * miuix owns the indicator, the selected-icon swap, the ripple and the badge placement, so a
 * destination only has to say which of its two vectors applies.
 */
@Composable
fun BottomBarMaterial(navigationBadge: NavigationBadgeState) {
    val fullFeatured = useFullFeaturedLayout()
    if (!fullFeatured) return

    val mainPagerState = LocalMainPagerState.current
    val visibilityRevision = NavBarVisibilityConfig.revision.collectAsStateWithLifecycle().value
    val destinations = remember(visibilityRevision) { NavBarVisibilityConfig.visibleDestinations() }
    val barStyle = BottomBarControl.style(BottomBarLayout.Docked)

    NavigationBar(color = barStyle.containerColor) {
        destinations.forEach { destination ->
            val selected = mainPagerState.selectedDestination == destination
            val badge = badgeFor(destination, navigationBadge)
            val badgeSlot: (@Composable () -> Unit)? = if (badge != null) {
                { NavBadgeDot(badge) }
            } else {
                null
            }
            NavigationBarItem(
                modifier = Modifier.weight(1f),
                icon = if (selected) destination.iconSelected else destination.iconNotSelected,
                label = stringResource(destination.label),
                selected = selected,
                onClick = {
                    if (!selected) {
                        mainPagerState.animateToDestination(destination)
                    }
                },
                badge = badgeSlot,
            )
        }
    }
}

/**
 * The count miuix draws on an icon.
 *
 * The text colour is passed in rather than inherited: miuix badges publish their content colour
 * through miuix's own `LocalContentColor`, which is not the one Material's `Text` reads, and an
 * inherited colour here would paint dark digits on the accent fill.
 */
@Composable
internal fun NavBadgeDot(badge: NavBadge) {
    val container = when (badge.tone) {
        BadgeTone.Alert -> MaterialTheme.colorScheme.error
        BadgeTone.Accent -> MaterialTheme.colorScheme.primary
    }
    val content = when (badge.tone) {
        BadgeTone.Alert -> MaterialTheme.colorScheme.onError
        BadgeTone.Accent -> MaterialTheme.colorScheme.onPrimary
    }
    Badge(containerColor = container, contentColor = content) {
        Text(text = badge.count.toString(), color = content)
    }
}
