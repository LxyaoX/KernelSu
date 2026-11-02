package me.weishu.kernelsu.ui.theme

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.spring
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.materialkolor.PaletteStyle
import com.materialkolor.dynamiccolor.ColorSpec
import com.materialkolor.rememberDynamicColorScheme

/** The color standard every scheme is generated with; see [effectiveFor] for the one exception. */
private val DEFAULT_COLOR_SPEC = ColorSpec.SpecVersion.SPEC_2025

fun ColorScheme.amoledBackground(amoled: Boolean): ColorScheme =
    if (!amoled) this
    else copy(
        background = Color.Black,
        surface = Color.Black,
        surfaceDim = Color(0xFF0D0D0D),
        surfaceContainerLowest = Color.Black,
        surfaceContainerLow = Color(0xFF050505),
        surfaceContainer = Color(0xFF0A0A0A),
        surfaceVariant = Color(0xFF121212),
        surfaceContainerHigh = Color(0xFF121212),
        surfaceContainerHighest = Color(0xFF1A1A1A),
        surfaceBright = Color(0xFF1F1F1F),
    )

/**
 * Pins the error family to the fixed Material 3 error palette.
 *
 * MaterialKolor derives error from the seed, so the hue drifts with the user's
 * palette: the same "not installed" card can render as the standard soft
 * `errorContainer` in one theme and a loud saturated red in another. Attention
 * and destructive states must stay recognisable, so we keep the semantic error
 * role fixed whatever the seed does.
 */
fun ColorScheme.withSemanticError(isDark: Boolean): ColorScheme {
    val semantic = if (isDark) darkColorScheme() else lightColorScheme()
    return copy(
        error = semantic.error,
        onError = semantic.onError,
        errorContainer = semantic.errorContainer,
        onErrorContainer = semantic.onErrorContainer,
    )
}

@Composable
fun rememberKernelSUColorScheme(
    seedColor: Color,
    isDark: Boolean,
    isAmoled: Boolean,
    paletteStyle: PaletteStyle,
    contrastLevel: Double = 0.0,
    generationMode: String = "custom",
): ColorScheme {
    val context = LocalContext.current
    if (generationMode == "classic" && seedColor == Color.Unspecified) {
        return (if (isDark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context))
            .withSemanticError(isDark).amoledBackground(isAmoled)
    }
    if (generationMode == "classic") {
        FptPalette.scheme(seedColor, isDark)?.let {
            return it.withSemanticError(isDark).amoledBackground(isAmoled)
        }
    }
    val seed = if (seedColor == Color.Unspecified) {
        (if (isDark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)).primary
    } else {
        FptPalette.seed(seedColor, isDark)
    }
    return rememberDynamicColorScheme(
        seedColor = seed,
        isDark = isDark,
        isAmoled = false,
        style = paletteStyle,
        // Spec 2025 is the only standard this build knows. A palette style without 2025
        // definitions falls back to its own, which is a library constraint, not a choice.
        specVersion = DEFAULT_COLOR_SPEC.effectiveFor(paletteStyle),
        contrastLevel = if (generationMode == "custom") contrastLevel else 0.0,
    ).withSemanticError(isDark).amoledBackground(isAmoled)
}

@Composable
fun ColorScheme.animateAsState(): ColorScheme {
    @Composable
    fun animateColor(color: Color): Color = animateColorAsState(
        targetValue = color,
        animationSpec = spring(),
        label = "theme_color_animation"
    ).value

    return ColorScheme(
        primary = animateColor(primary),
        onPrimary = animateColor(onPrimary),
        primaryContainer = animateColor(primaryContainer),
        onPrimaryContainer = animateColor(onPrimaryContainer),
        inversePrimary = animateColor(inversePrimary),
        secondary = animateColor(secondary),
        onSecondary = animateColor(onSecondary),
        secondaryContainer = animateColor(secondaryContainer),
        onSecondaryContainer = animateColor(onSecondaryContainer),
        tertiary = animateColor(tertiary),
        onTertiary = animateColor(onTertiary),
        tertiaryContainer = animateColor(tertiaryContainer),
        onTertiaryContainer = animateColor(onTertiaryContainer),
        background = animateColor(background),
        onBackground = animateColor(onBackground),
        surface = animateColor(surface),
        onSurface = animateColor(onSurface),
        surfaceVariant = animateColor(surfaceVariant),
        onSurfaceVariant = animateColor(onSurfaceVariant),
        surfaceTint = animateColor(surfaceTint),
        inverseSurface = animateColor(inverseSurface),
        inverseOnSurface = animateColor(inverseOnSurface),
        error = animateColor(error),
        onError = animateColor(onError),
        errorContainer = animateColor(errorContainer),
        onErrorContainer = animateColor(onErrorContainer),
        outline = animateColor(outline),
        outlineVariant = animateColor(outlineVariant),
        scrim = animateColor(scrim),
        surfaceBright = animateColor(surfaceBright),
        surfaceDim = animateColor(surfaceDim),
        surfaceContainer = animateColor(surfaceContainer),
        surfaceContainerHigh = animateColor(surfaceContainerHigh),
        surfaceContainerHighest = animateColor(surfaceContainerHighest),
        surfaceContainerLow = animateColor(surfaceContainerLow),
        surfaceContainerLowest = animateColor(surfaceContainerLowest),

        primaryFixed = animateColor(primaryFixed),
        primaryFixedDim = animateColor(primaryFixedDim),
        onPrimaryFixed = animateColor(onPrimaryFixed),
        onPrimaryFixedVariant = animateColor(onPrimaryFixedVariant),
        secondaryFixed = animateColor(secondaryFixed),
        secondaryFixedDim = animateColor(secondaryFixedDim),
        onSecondaryFixed = animateColor(onSecondaryFixed),
        onSecondaryFixedVariant = animateColor(onSecondaryFixedVariant),
        tertiaryFixed = animateColor(tertiaryFixed),
        tertiaryFixedDim = animateColor(tertiaryFixedDim),
        onTertiaryFixed = animateColor(onTertiaryFixed),
        onTertiaryFixedVariant = animateColor(onTertiaryFixedVariant)
    )
}
