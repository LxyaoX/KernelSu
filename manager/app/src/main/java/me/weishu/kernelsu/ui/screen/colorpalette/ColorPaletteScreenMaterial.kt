package me.weishu.kernelsu.ui.screen.colorpalette

import androidx.annotation.StringRes
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.captionBar
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Segment
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.DarkMode
import androidx.compose.material.icons.filled.LightMode
import androidx.compose.material.icons.outlined.ToggleOn
import androidx.compose.material.icons.rounded.Check
import top.yukonga.miuix.kmp.basic.Icon
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import top.yukonga.miuix.kmp.basic.SnackbarHostState
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.material3.ripple
import androidx.compose.material3.toShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.materialkolor.PaletteStyle
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.ExpressiveScaffold
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedItemContainer
import me.weishu.kernelsu.ui.component.material.SegmentedSwitchItem
import me.weishu.kernelsu.ui.component.material.SliderTrackConfig
import me.weishu.kernelsu.ui.component.material.SnackBarHost
import me.weishu.kernelsu.ui.component.material.TopBarBackButton
import me.weishu.kernelsu.ui.component.settings.SettingsContentMaxWidth
import me.weishu.kernelsu.ui.theme.ColorMode
import me.weishu.kernelsu.ui.theme.FolkShape
import me.weishu.kernelsu.ui.theme.keyColorOptions
import me.weishu.kernelsu.ui.theme.rememberKernelSUColorScheme
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior

private class ThemeModeChoice(
    val mode: ColorMode,
    val icon: ImageVector,
    @StringRes val labelRes: Int,
)

private val themeModeChoices = listOf(
    ThemeModeChoice(ColorMode.LIGHT, Icons.Filled.LightMode, R.string.settings_theme_mode_light),
    ThemeModeChoice(ColorMode.DARK, Icons.Filled.DarkMode, R.string.settings_theme_mode_dark),
    ThemeModeChoice(ColorMode.SYSTEM, Icons.Filled.AutoAwesome, R.string.settings_theme_mode_system),
)

private val ThemeGroupModifier = Modifier
    .fillMaxWidth()
    .padding(horizontal = 16.dp)

@Composable
fun ColorPaletteScreenMaterial(
    state: ColorPaletteUiState,
    actions: ColorPaletteScreenActions,
) {
    val scrollBehavior = MiuixScrollBehavior()
    val currentColorMode = state.currentColorMode
    val currentKeyColor = state.uiState.keyColor
    val colorStyle = state.currentPaletteStyle
    val haptic = LocalHapticFeedback.current
    val snackbarHostState = remember { SnackbarHostState() }
    val activeMode = if (currentColorMode.isAmoled) ColorMode.DARK else currentColorMode
    val darkTheme = activeMode == ColorMode.DARK || activeMode == ColorMode.SYSTEM && isSystemInDarkTheme()
    val systemSwatch = rememberKernelSUColorScheme(
        seedColor = Color.Unspecified,
        isDark = darkTheme,
        isAmoled = currentColorMode.isAmoled,
        paletteStyle = colorStyle,
    ).primary

    ExpressiveScaffold(
        topBar = {
            MiuixTopAppBar(
                title = stringResource(R.string.settings_theme),
                navigationIcon = {
                    TopBarBackButton(onClick = actions.onBack)
                },
                scrollBehavior = scrollBehavior,
            )
        },
        snackbarHost = { SnackBarHost(hostState = snackbarHostState) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { paddingValues ->
        val navBars = WindowInsets.navigationBars.asPaddingValues()
        val captionBar = WindowInsets.captionBar.asPaddingValues()

        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .wrapContentWidth(Alignment.CenterHorizontally)
                .widthIn(max = SettingsContentMaxWidth),
            contentPadding = PaddingValues(
                top = 8.dp,
                bottom = 16.dp + navBars.calculateBottomPadding() + captionBar.calculateBottomPadding(),
            ),
            verticalArrangement = Arrangement.spacedBy(13.dp),
        ) {
            item {
                SegmentedColumn(
                    modifier = ThemeGroupModifier,
                    content = listOf(
                        {
                            SegmentedItemContainer {
                                ThemeModeRow(
                                    activeMode = activeMode,
                                    onSelect = { mode ->
                                        haptic.performHapticFeedback(HapticFeedbackType.ContextClick)
                                        val keepAmoled = mode == ColorMode.DARK && currentColorMode.isAmoled
                                        actions.onSetColorMode(if (keepAmoled) ColorMode.DARK_AMOLED else mode)
                                    }
                                )
                            }
                        },
                        {
                            SegmentedItemContainer {
                                KeyColorPicker(
                                    currentKeyColor = currentKeyColor,
                                    systemSwatch = systemSwatch,
                                    rippleColor = if (darkTheme) {
                                        Color.Black.copy(alpha = 0.50f)
                                    } else {
                                        Color.White.copy(alpha = 0.90f)
                                    },
                                    onSelect = { color ->
                                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                        actions.onSetKeyColor(color)
                                    }
                                )
                            }
                        },
                        {
                            SegmentedItemContainer {
                                ColorStyleRow(
                                    selected = colorStyle,
                                    onSelect = { style ->
                                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                        actions.onSetColorStyle(style.name)
                                    }
                                )
                            }
                        },
                    ),
                )
            }

            item {
                SegmentedColumn(
                    modifier = ThemeGroupModifier,
                    title = stringResource(R.string.settings_theme_options),
                ) {
                    if (darkTheme) {
                        item {
                            SwitchPreference(
                                title = stringResource(R.string.settings_amoled_theme),
                                summary = stringResource(R.string.settings_amoled_theme_desc),
                                startAction = { Icon(Icons.Filled.DarkMode, contentDescription = null) },
                                checked = currentColorMode.isAmoled,
                                onCheckedChange = {
                                    enabled ->
                                                                       actions.onSetColorMode(if (enabled) ColorMode.DARK_AMOLED else ColorMode.DARK)
                                },
                            )
                        }
                    }
                    item {
                        SwitchPreference(
                            title = stringResource(R.string.settings_discrete_slider),
                            summary = stringResource(R.string.settings_discrete_slider_desc),
                            startAction = { Icon(Icons.AutoMirrored.Outlined.Segment, contentDescription = null) },
                            checked = SliderTrackConfig.isDiscrete,
                            onCheckedChange = { SliderTrackConfig.setDiscreteSteps(it) },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ThemeModeRow(
    activeMode: ColorMode,
    onSelect: (ColorMode) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        themeModeChoices.forEach { choice ->
            ThemeModeCard(
                icon = choice.icon,
                label = stringResource(choice.labelRes),
                selected = activeMode == choice.mode,
                onClick = { onSelect(choice.mode) },
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun ThemeModeCard(
    icon: ImageVector,
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scale by animateFloatAsState(
        targetValue = if (selected) 1.05f else 1f,
        animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec()
    )
    val containerColor = if (selected) {
        MaterialTheme.colorScheme.primaryContainer
    } else {
        MaterialTheme.colorScheme.surfaceContainerLow
    }
    val contentColor = if (selected) {
        MaterialTheme.colorScheme.onPrimaryContainer
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

    Column(
        modifier = modifier
            .scale(scale)
            .clip(FolkShape.Corner24)
            .background(containerColor)
            .clickable(role = Role.RadioButton, onClick = onClick)
            .padding(vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(24.dp),
            tint = contentColor
        )
        Text(
            text = label,
            style = MaterialTheme.typography.bodyLarge,
            color = contentColor,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun KeyColorPicker(
    currentKeyColor: Int,
    systemSwatch: Color,
    rippleColor: Color,
    onSelect: (Int) -> Unit,
) {
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .clipToBounds(),
        horizontalArrangement = Arrangement.spacedBy(20.dp),
        verticalAlignment = Alignment.CenterVertically,
        contentPadding = PaddingValues(start = 24.dp, end = 16.dp, top = 10.dp, bottom = 12.dp),
    ) {
        item(key = 0) {
            KeyColorCircle(
                label = stringResource(R.string.settings_key_color_default),
                displayColor = systemSwatch,
                selected = currentKeyColor == 0,
                outlined = true,
                rippleColor = rippleColor,
                onClick = { onSelect(0) }
            )
        }
        items(keyColorOptions, key = { it.argb }) { option ->
            KeyColorCircle(
                label = stringResource(option.labelRes),
                displayColor = Color(option.argb),
                selected = currentKeyColor == option.argb,
                outlined = false,
                rippleColor = rippleColor,
                onClick = { onSelect(option.argb) }
            )
        }
    }
}

@Composable
private fun KeyColorCircle(
    label: String,
    displayColor: Color,
    selected: Boolean,
    outlined: Boolean,
    rippleColor: Color,
    onClick: () -> Unit,
) {
    Column(
        modifier = Modifier.widthIn(min = 56.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Box(
            modifier = Modifier
                .size(56.dp)
                .clickable(
                    role = Role.RadioButton,
                    interactionSource = remember { MutableInteractionSource() },
                    indication = ripple(bounded = true, color = rippleColor)
                ) { onClick() },
            contentAlignment = Alignment.Center
        ) {
            KeyColorDot(
                displayColor = displayColor,
                selected = selected,
                outlined = outlined
            )
        }

        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
private fun KeyColorDot(
    displayColor: Color,
    selected: Boolean,
    outlined: Boolean,
) {
    val scale by animateFloatAsState(
        targetValue = if (selected) 1.1f else 1f,
        animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec()
    )
    val shape = if (selected) MaterialShapes.Cookie9Sided.toShape() else CircleShape
    val checkColor = if (displayColor.luminance() > 0.5f) Color.Black else Color.White

    Box(
        modifier = Modifier
            .size(56.dp)
            .scale(scale)
            .clip(shape)
            .background(displayColor, shape)
            .then(
                if (outlined) {
                    Modifier.border(2.dp, MaterialTheme.colorScheme.outline, shape)
                } else {
                    Modifier
                }
            ),
        contentAlignment = Alignment.Center
    ) {
        AnimatedVisibility(
            visible = selected,
            enter = scaleIn(animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec()) +
                fadeIn(animationSpec = MaterialTheme.motionScheme.defaultEffectsSpec()),
            exit = scaleOut(animationSpec = MaterialTheme.motionScheme.fastSpatialSpec()) +
                fadeOut(animationSpec = MaterialTheme.motionScheme.fastEffectsSpec())
        ) {
            Icon(
                imageVector = Icons.Rounded.Check,
                contentDescription = null,
                modifier = Modifier.size(28.dp),
                tint = checkColor
            )
        }
    }
}

@Composable
private fun ColorStyleRow(
    selected: PaletteStyle,
    onSelect: (PaletteStyle) -> Unit,
) {
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
    ) {
        items(PaletteStyle.entries, key = { it.name }) { style ->
            ColorStyleChip(
                label = paletteStyleLabel(style),
                selected = selected == style,
                onClick = { onSelect(style) }
            )
        }
    }
}

@Composable
private fun ColorStyleChip(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val scale by animateFloatAsState(
        targetValue = if (selected) 1.05f else 1f,
        animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec()
    )
    val containerColor = if (selected) {
        MaterialTheme.colorScheme.primaryContainer
    } else {
        MaterialTheme.colorScheme.surfaceContainerLow
    }
    val contentColor = if (selected) {
        MaterialTheme.colorScheme.onPrimaryContainer
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

    Box(
        modifier = Modifier
            .scale(scale)
            .clip(FolkShape.Corner20)
            .background(containerColor)
            .clickable(role = Role.RadioButton, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = contentColor,
            maxLines = 1
        )
    }
}
