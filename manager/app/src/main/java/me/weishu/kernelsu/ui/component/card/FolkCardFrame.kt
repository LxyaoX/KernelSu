package me.weishu.kernelsu.ui.component.card

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.ui.component.material.folkPressScale
import me.weishu.kernelsu.ui.component.module.LocalInsideSplicedGroup
import me.weishu.kernelsu.ui.navigation3.navOrigin
import me.weishu.kernelsu.ui.theme.ContinuousCornerShape
import me.weishu.kernelsu.ui.theme.FolkMotion
import top.yukonga.miuix.kmp.basic.Surface
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * The shell every card of the module design shares: the animated corner, press scaling and the
 * card fill.
 *
 * Cards only differ in what they put inside, so every card wraps its content in this frame and
 * inherits the same look and interactions.
 */
@Composable
fun FolkCardFrame(
    expanded: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    onClickLabel: String? = null,
    onLongPress: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    val haptics = LocalHapticFeedback.current
    val interactionSource = remember { MutableInteractionSource() }
    val pressed by interactionSource.collectIsPressedAsState()

    val cardCorner by animateDpAsState(
        targetValue = when {
            expanded -> 28.dp
            pressed -> 24.dp
            else -> 20.dp
        },
        animationSpec = FolkMotion.smoothSpring(),
        label = "cardCorner",
    )
    val cardShape = ContinuousCornerShape(cardCorner)
    val insideSplicedGroup = LocalInsideSplicedGroup.current

    val clickModifier = Modifier
        .fillMaxWidth()
        .navOrigin(
            cornerRadius = cardCorner,
            color = MiuixTheme.colorScheme.surfaceContainer,
            interactionSource = interactionSource,
        )
        .animateContentSize(animationSpec = FolkMotion.smoothSpring())
        .folkPressScale(interactionSource)
        .combinedClickable(
            interactionSource = interactionSource,
            indication = null,
            onClickLabel = onClickLabel,
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                onClick()
            },
            onLongClick = onLongPress?.let { callback ->
                {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    callback()
                }
            },
        )

    val framed: @Composable () -> Unit = {
        Box(Modifier.fillMaxWidth()) { content() }
    }

    if (insideSplicedGroup) {
        // The group already draws the surface; a second one would double the corner and the tint.
        Box(modifier = modifier.then(clickModifier)) { framed() }
    } else {
        Surface(
            modifier = modifier.clip(cardShape).then(clickModifier),
            shape = cardShape,
            color = MiuixTheme.colorScheme.surfaceContainer,
        ) {
            framed()
        }
    }
}
