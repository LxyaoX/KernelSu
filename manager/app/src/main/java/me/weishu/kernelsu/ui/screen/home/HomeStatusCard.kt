package me.weishu.kernelsu.ui.screen.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircleOutline
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.contentColorFor
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.statustag.StatusTag
import me.weishu.kernelsu.ui.theme.ColorMode
import me.weishu.kernelsu.ui.theme.FolkType
import me.weishu.kernelsu.ui.theme.LocalColorMode
import top.yukonga.miuix.kmp.basic.ButtonDefaults
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** The card corner radius, shared with [HomeCard]. */
private val WorkCardCorner = 20.dp

/** The card is a hero: it keeps this much height even on a short screen or a narrow font. */
private val WorkCardMinHeight = 188.dp

/** The mark is drawn far larger than the card and clipped by it, as the reference build does. */
private val WorkCardMarkSize = 150.dp

/**
 * What state the kernel module is in, as the page's hero card.
 *
 * Loaded: one tall card - the state in display type, the flags that change its behaviour along the
 * top edge, and the version under the title, with an oversized outline mark bleeding off the bottom
 * corner. Not loaded: the same tall card in neutral, saying why and offering the actions that fix
 * it. Tapping either one opens the installer.
 */
@Composable
internal fun StatusCard(
    state: HomeUiState,
    actions: HomeActions,
    modifier: Modifier = Modifier,
) {
    if (state.ksuVersion == null) {
        MissingCard(state = state, actions = actions, modifier = modifier)
    } else {
        WorkingCard(state = state, actions = actions, modifier = modifier)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun WorkingCard(
    state: HomeUiState,
    actions: HomeActions,
    modifier: Modifier,
) {
    val style = homeWorkCardStyle()

    val workingMode = when (state.lkmMode) {
        null -> null
        true -> "LKM"
        else -> "Built-In"
    }
    val modeHidden = rememberHomeThemeSettings().listWorkingCardModeHidden

    val dynamic = ColorMode.fromValue(LocalColorMode.current).isMonet
    val captionColor = MiuixTheme.colorScheme.onSurfaceVariantSummary
    // On the card the mark is the state itself, so it keeps the accent at full strength outside the
    // dynamic palette, exactly as the reference build draws it.
    val markTint = style.accentColor.copy(alpha = if (dynamic) 0.8f else 1f)
    val modeTagContent = if (dynamic) MiuixTheme.colorScheme.onPrimary else Color.White
    val errorTagColor = MiuixTheme.colorScheme.errorContainer
    val errorTagContent = MiuixTheme.colorScheme.onErrorContainer

    HomeCard(
        modifier = modifier.fillMaxWidth(),
        containerColor = style.containerColor,
        contentColor = style.contentColor ?: contentColorFor(style.containerColor),
        onClick = { if (!state.isLateLoadMode) actions.onInstallClick() },
    ) {
        // The mark is larger than the card on purpose; clipping keeps it inside the rounded shape.
        Box(modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(WorkCardCorner))) {
            Icon(
                imageVector = Icons.Rounded.CheckCircleOutline,
                contentDescription = null,
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .offset(x = 27.dp, y = 31.dp)
                    .size(WorkCardMarkSize),
                tint = markTint,
            )
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = WorkCardMinHeight)
                    .padding(22.dp),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    if (workingMode != null && !modeHidden) {
                        StatusTag(
                            label = workingMode,
                            contentColor = modeTagContent,
                            backgroundColor = style.accentColor,
                        )
                    }
                    if (state.showCustomLkmBadge) {
                        StatusTag(
                            label = stringResource(R.string.home_lkm_custom),
                            contentColor = style.tagContentColor,
                            backgroundColor = style.tagColor,
                        )
                    }
                    if (state.isSafeMode) {
                        StatusTag(
                            label = stringResource(R.string.safe_mode),
                            contentColor = errorTagContent,
                            backgroundColor = errorTagColor,
                        )
                    }
                    if (state.isLateLoadMode) {
                        StatusTag(
                            label = stringResource(R.string.jailbreak_mode),
                            contentColor = errorTagContent,
                            backgroundColor = errorTagColor,
                        )
                    }
                }
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = stringResource(R.string.home_working),
                        style = FolkType.Hero,
                    )
                    Text(
                        text = stringResource(
                            R.string.home_working_version,
                            "${state.ksuVersion}-${state.kernelUAPIVersion}",
                        ),
                        style = FolkType.Machine,
                        fontFamily = FontFamily.Monospace,
                        color = captionColor,
                    )
                }
            }
        }
    }
}

@Composable
private fun MissingCard(
    state: HomeUiState,
    actions: HomeActions,
    modifier: Modifier,
) {
    val notInstalled = state.kernelVersion.isSupportedSeries()
    val reason = stringResource(
        if (notInstalled) R.string.home_click_to_install else R.string.home_unsupported_reason
    )
    HomeCard(
        modifier = modifier.fillMaxWidth(),
        onClick = { if (!state.isLateLoadMode) actions.onInstallClick() },
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = WorkCardMinHeight)
                .padding(22.dp),
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Icon(
                    imageVector = Icons.Rounded.ErrorOutline,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = MiuixTheme.colorScheme.error,
                )
                Text(
                    text = reason,
                    style = FolkType.Caption,
                    color = MiuixTheme.colorScheme.error,
                )
            }
            Text(
                text = stringResource(
                    if (notInstalled) R.string.home_not_installed else R.string.home_unsupported
                ),
                modifier = Modifier.padding(vertical = 12.dp),
                style = FolkType.Hero,
            )
            if (notInstalled) {
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    MiuixTextButton(
                        text = stringResource(R.string.install),
                        onClick = actions.onInstallClick,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                    )
                    if (state.isSELinuxPermissive) {
                        MiuixTextButton(
                            text = stringResource(R.string.home_jailbreak),
                            onClick = actions.onJailbreakClick,
                        )
                    }
                }
            }
        }
    }
}
