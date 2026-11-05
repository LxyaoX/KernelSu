package me.weishu.kernelsu.ui.component.module

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.SubcomposeLayout
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.Text
import me.weishu.kernelsu.ui.theme.ContinuousCornerShape
import top.yukonga.miuix.kmp.basic.Surface
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** The pair of colours a module card button is filled and lettered with. */
@Immutable
data class ModuleButtonColors(
    val containerColor: Color,
    val contentColor: Color,
)

/** miuix's button shape and metrics, so an action row reads like any other miuix button. */
internal object ModuleButtonDefaults {
    val CornerRadius = 16.dp
    val MinWidth = 58.dp
    val MinHeight = 40.dp
    val InsideMargin = PaddingValues(horizontal = 16.dp, vertical = 13.dp)
    val IconOnlyMargin = PaddingValues(horizontal = 12.dp, vertical = 12.dp)

    @Composable
    fun colors(): ModuleButtonColors = ModuleButtonColors(
        containerColor = MiuixTheme.colorScheme.secondaryContainer,
        contentColor = MiuixTheme.colorScheme.onSecondaryContainer,
    )
}

data class ModuleButtonConfig(
    val icon: ImageVector,
    val text: String,
    val contentDescription: String,
    val onClick: () -> Unit,
    val onLongClick: (() -> Unit)? = null,
    val enabled: Boolean = true,
    val colors: ModuleButtonColors? = null,
)

/**
 * One chip on a card - a module id, "update", "WebUI" and the like.
 *
 * It is a miuix surface coloured by the caller, so it sits on the card the way miuix's own tags do
 * instead of carrying Material's tonal container and label style.
 */
@Composable
fun ModuleLabel(text: String, containerColor: Color, contentColor: Color) {
    Surface(color = containerColor, contentColor = contentColor, shape = ContinuousCornerShape(6.dp)) {
        Text(
            text = text,
            style = MiuixTheme.textStyles.footnote2,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
        )
    }
}

@Composable
fun AdaptiveModuleButtonRow(
    buttons: List<ModuleButtonConfig>,
    trailingButton: ModuleButtonConfig? = null,
    iconOnly: Boolean,
    spacing: Int = 8,
) {
    if (iconOnly) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            buttons.forEach { config -> ModuleActionButton(config, true) }
            Spacer(Modifier.weight(1f))
            trailingButton?.let { config -> ModuleActionButton(config, true) }
        }
    } else {
        AdaptiveButtonRowLayout(
            buttons = buttons,
            trailingButton = trailingButton,
            spacing = spacing,
        )
    }
}

@Composable
private fun AdaptiveButtonRowLayout(
    buttons: List<ModuleButtonConfig>,
    trailingButton: ModuleButtonConfig?,
    spacing: Int,
) {
    var iconOnlyCount by remember(buttons.size) { mutableIntStateOf(0) }
    SubcomposeLayout(modifier = Modifier.fillMaxWidth()) { constraints ->
        val spacingPx = spacing.dp.roundToPx()
        val availableWidth = constraints.maxWidth
        fun measure(tag: String, config: ModuleButtonConfig, iconOnly: Boolean): Int =
            subcompose(tag) { ModuleActionButton(config, iconOnly) }.first().measure(Constraints()).width

        val buttonWidthsWithText = buttons.mapIndexed { i, c -> measure("measure_text_$i", c, false) }
        val buttonWidthsIconOnly = buttons.mapIndexed { i, c -> measure("measure_icon_$i", c, true) }
        val trailingWidthWithText = trailingButton?.let { measure("measure_trailing_text", it, false) } ?: 0
        val trailingWidthIconOnly = trailingButton?.let { measure("measure_trailing_icon", it, true) } ?: 0
        val totalSpacing = if (buttons.isNotEmpty() || trailingButton != null) {
            (buttons.size + (if (trailingButton != null) 1 else 0) - 1).coerceAtLeast(0) * spacingPx
        } else {
            0
        }
        val minSpacerWidth = 8.dp.roundToPx()
        var currentIconOnlyCount = 0
        for (i in 0..buttons.size) {
            val leftButtonsWidth = buttons.indices.sumOf { index -> if (index < i) buttonWidthsIconOnly[index] else buttonWidthsWithText[index] }
            val trailingWidth = if (trailingButton != null) {
                val totalWithTrailingText = leftButtonsWidth + trailingWidthWithText + totalSpacing + minSpacerWidth
                if (totalWithTrailingText <= availableWidth) trailingWidthWithText else trailingWidthIconOnly
            } else {
                0
            }
            val totalWidth = leftButtonsWidth + trailingWidth + totalSpacing + minSpacerWidth
            if (totalWidth <= availableWidth) {
                currentIconOnlyCount = i
                break
            }
            currentIconOnlyCount = i + 1
        }
        iconOnlyCount = currentIconOnlyCount.coerceAtMost(buttons.size)
        val leftButtonsWidth = buttons.indices.sumOf { index -> if (index < iconOnlyCount) buttonWidthsIconOnly[index] else buttonWidthsWithText[index] }
        val trailingIconOnly = if (trailingButton != null) {
            (leftButtonsWidth + trailingWidthWithText + totalSpacing + minSpacerWidth) > availableWidth
        } else {
            false
        }
        val finalPlaceables = buttons.mapIndexed { i, c ->
            subcompose("button_$i") { ModuleActionButton(c, i < iconOnlyCount) }.first().measure(Constraints())
        }
        val trailingPlaceable = trailingButton?.let { c ->
            subcompose("trailing_button") { ModuleActionButton(c, trailingIconOnly) }.first().measure(Constraints())
        }
        val layoutHeight = maxOf(finalPlaceables.maxOfOrNull { it.height } ?: 0, trailingPlaceable?.height ?: 0)
        layout(availableWidth, layoutHeight) {
            var x = 0
            finalPlaceables.forEach {
                it.placeRelative(x, 0)
                x += it.width + spacingPx
            }
            trailingPlaceable?.let { it.placeRelative(availableWidth - it.width, 0) }
        }
    }
}

/**
 * A card action, drawn the way a miuix button is: a squircle fill at miuix's corner radius and
 * metrics, with the label in miuix's button style.
 *
 * It stays a hand-rolled surface rather than miuix's `Button` because the row measures every action
 * with and without its label to decide how many fit, and a long press opens the shortcut dialog,
 * which `Button` does not forward.
 */
@Composable
fun ModuleActionButton(config: ModuleButtonConfig, iconOnly: Boolean, modifier: Modifier = Modifier) {
    val colors = config.colors ?: ModuleButtonDefaults.colors()
    val containerColor =
        if (config.enabled) colors.containerColor else MiuixTheme.colorScheme.disabledSecondaryVariant
    val contentColor =
        if (config.enabled) colors.contentColor else MiuixTheme.colorScheme.disabledOnSecondaryVariant
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = modifier
            .defaultMinSize(ModuleButtonDefaults.MinWidth, ModuleButtonDefaults.MinHeight)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = LocalIndication.current,
                enabled = config.enabled,
                onLongClick = config.onLongClick,
                onClick = config.onClick,
            ),
        shape = ContinuousCornerShape(ModuleButtonDefaults.CornerRadius),
        color = containerColor,
        contentColor = contentColor,
    ) {
        Row(
            modifier = Modifier.padding(
                if (iconOnly) ModuleButtonDefaults.IconOnlyMargin else ModuleButtonDefaults.InsideMargin
            ),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = config.icon,
                contentDescription = config.contentDescription,
                modifier = Modifier.size(20.dp),
                tint = contentColor,
            )
            if (!iconOnly) {
                Spacer(Modifier.width(8.dp))
                Text(
                    text = config.text,
                    style = MiuixTheme.textStyles.body2,
                    color = contentColor,
                    maxLines = 1,
                )
            }
        }
    }
}
