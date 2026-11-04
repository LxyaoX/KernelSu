package me.weishu.kernelsu.ui.component.bottombar

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import top.yukonga.miuix.kmp.basic.Badge
import androidx.compose.material3.MaterialTheme
import top.yukonga.miuix.kmp.basic.Surface
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.weishu.kernelsu.ui.util.useFullFeaturedLayout
import me.weishu.kernelsu.ui.LocalMainPagerState

/** Vertical space the floating bar occupies from the screen bottom, excluding system insets. */
fun floatingBarReservedHeight(compact: Boolean, style: FloatingBarConfig.Style): Dp =
    (if (style == FloatingBarConfig.Style.Drawer) 64.dp else if (compact) 68.dp else 72.dp) + 32.dp

/**
 * Floating bottom navigation bar rendered above the page content.
 *
 * Two forms ([FloatingBarConfig.Style]): [Standard][FloatingBarStandard] capsule and the
 * expanding [Drawer][FloatingBarDrawer].
 */
@Composable
internal fun FloatingBar(
    navigationBadge: NavigationBadgeState,
    modifier: Modifier = Modifier,
    onUserInteraction: () -> Unit = {},
) {
    if (!useFullFeaturedLayout()) return
    val mainPagerState = LocalMainPagerState.current
    val revision by FloatingBarConfig.revision.collectAsStateWithLifecycle()
    val style = remember(revision) { FloatingBarConfig.style }
    val compact = remember(revision) { FloatingBarConfig.compact }
    val barStyle = BottomBarControl.style(BottomBarLayout.Floating)
    val containerColor = barStyle.containerColor
    val selectionColor = barStyle.indicatorColor
    val visibilityRevision by NavBarVisibilityConfig.revision.collectAsStateWithLifecycle()
    val destinations = remember(visibilityRevision) { NavBarVisibilityConfig.visibleDestinations() }
    val selectedIndex = mainPagerState.selectedPage
    // The pill's fill is derived from the page, so it does not carry the raised neutral's own lift and
    // a tonal tint on top would only pull it back toward the canvas it was moved off of. It is given a
    // hairline edge instead: an outline depends on nothing the theme painted behind the bar, so the
    // shape stays legible even where the step is small.

    BoxWithConstraints(
        modifier = modifier
            .fillMaxWidth()
            .padding(bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()),
    ) {
        val horizontalPadding = when {
            maxWidth > 600.dp -> 32.dp
            maxWidth > 400.dp -> 24.dp
            else -> 16.dp
        }
        Box(
            modifier = Modifier.fillMaxWidth().padding(horizontal = horizontalPadding, vertical = 16.dp),
            contentAlignment = Alignment.Center,
        ) {
            val barShape: Shape = when {
                style == FloatingBarConfig.Style.Drawer -> CircleShape
                compact -> RoundedCornerShape(percent = 50)
                else -> MaterialTheme.shapes.large
            }
            Surface(
                modifier = Modifier
                    .wrapContentWidth()
                    .clip(barShape),
                shape = barShape,
                color = containerColor,
                // A translucent surface cannot carry a shadow: the shadow is painted behind a fill that
                // no longer hides it, so it reads as a dark halo around the capsule instead of a lift.
                shadowElevation = 8.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
            ) {
                if (style == FloatingBarConfig.Style.Drawer) {
                    FloatingBarDrawer(
                        destinations = destinations,
                        selectedIndex = selectedIndex,
                        badge = { badgeFor(it, navigationBadge) },
                        onSelect = {
                            if (it == mainPagerState.selectedDestination) return@FloatingBarDrawer
                            onUserInteraction()
                            mainPagerState.animateToDestination(it)
                        },
                        selectionColor = selectionColor,
                    )
                } else {
                    FloatingBarStandard(
                        destinations = destinations,
                        selectedIndex = selectedIndex,
                        badge = { badgeFor(it, navigationBadge) },
                        onSelect = {
                            onUserInteraction()
                            mainPagerState.animateToDestination(it)
                        },
                        selectionColor = selectionColor,
                    )
                }
            }
        }
    }
}

@Composable
internal fun FloatingBadge(badge: NavBadge) {
    when (badge.tone) {
        BadgeTone.Alert -> Badge { Text(badge.count.toString()) }
        BadgeTone.Accent -> Badge(
            containerColor = MaterialTheme.colorScheme.primary,
            contentColor = MaterialTheme.colorScheme.onPrimary,
        ) { Text(badge.count.toString()) }
    }
}
