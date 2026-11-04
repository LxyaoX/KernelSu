package me.weishu.kernelsu.ui.component.onboarding

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.MenuBook
import androidx.compose.material.icons.automirrored.outlined.OpenInNew
import androidx.compose.material.icons.outlined.AutoAwesome
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.Extension
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.FolkFilledTonalButton
import me.weishu.kernelsu.ui.component.material.FolkTextButton
import me.weishu.kernelsu.ui.component.material.folkPressScale
import me.weishu.kernelsu.ui.theme.FolkShape

private const val GuidePageCount = 4

@Composable
fun FirstRunGuide(onDismiss: () -> Unit) {
    var currentPage by rememberSaveable { mutableIntStateOf(0) }

    BackHandler {
        if (currentPage == 0) {
            onDismiss()
        } else {
            currentPage--
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            dismissOnClickOutside = false,
        ),
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(WindowInsets.safeDrawing)
                .padding(horizontal = 24.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center,
        ) {
            BoxWithConstraints {
                val pageMaxHeight = (maxHeight - 150.dp).coerceAtLeast(1.dp)
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .widthIn(max = 480.dp),
                    shape = FolkShape.Dialog,
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                ) {
                    Column(modifier = Modifier.padding(28.dp)) {
                        val slide =
                            MaterialTheme.motionScheme.defaultSpatialSpec<IntOffset>()
                        val fade =
                            MaterialTheme.motionScheme.defaultEffectsSpec<Float>()
                        AnimatedContent(
                            targetState = currentPage,
                            modifier = Modifier
                                .fillMaxWidth()
                                .heightIn(max = pageMaxHeight),
                            transitionSpec = {
                                val forward = targetState > initialState
                                (slideInHorizontally(slide) { width ->
                                    if (forward) width else -width
                                } + fadeIn(fade)).togetherWith(
                                    slideOutHorizontally(slide) { width ->
                                        if (forward) -width else width
                                    } + fadeOut(fade),
                                )
                            },
                            label = "guide-page",
                        ) { page ->
                            when (page) {
                                0 -> GuidePage(
                                    title = stringResource(R.string.guide_welcome_title),
                                    description = stringResource(R.string.guide_welcome_desc),
                                    icon = { GuideIcon(Icons.Outlined.AutoAwesome) },
                                )

                                1 -> GuidePage(
                                    title = stringResource(R.string.guide_repo_title),
                                    description = stringResource(R.string.guide_repo_desc),
                                    icon = { GuideIcon(painterResource(R.drawable.github)) },
                                ) {
                                    LinkBlock(
                                        url = stringResource(R.string.guide_repo_url),
                                        action = stringResource(R.string.guide_repo_action),
                                    )
                                }

                                2 -> GuidePage(
                                    title = stringResource(R.string.guide_features_title),
                                    description = stringResource(R.string.guide_features_desc),
                                    icon = { GuideIcon(Icons.Outlined.Extension) },
                                ) {
                                    Column(
                                        modifier = Modifier.fillMaxWidth(),
                                        verticalArrangement = Arrangement.spacedBy(14.dp),
                                    ) {
                                        FeatureRow(stringResource(R.string.guide_feature_zygisk))
                                    }
                                }

                                else -> GuidePage(
                                    title = stringResource(R.string.guide_docs_title),
                                    description = stringResource(R.string.guide_docs_desc),
                                    icon = { GuideIcon(Icons.AutoMirrored.Outlined.MenuBook) },
                                ) {
                                    LinkBlock(
                                        url = stringResource(R.string.home_learn_finasu_url),
                                        action = stringResource(R.string.guide_docs_action),
                                    )
                                }
                            }
                        }

                        Spacer(Modifier.height(20.dp))
                        PageIndicator(currentPage)
                        Spacer(Modifier.height(16.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            if (currentPage == 0) {
                                Spacer(Modifier.width(88.dp).height(48.dp))
                            } else {
                                GuideTextButton(
                                    text = stringResource(R.string.guide_prev),
                                    onClick = { currentPage-- },
                                )
                            }
                            GuideTextButton(
                                text = stringResource(R.string.guide_skip),
                                onClick = onDismiss,
                            )
                            if (currentPage == GuidePageCount - 1) {
                                FolkFilledTonalButton(
                                    onClick = onDismiss,
                                    modifier = Modifier.heightIn(min = 48.dp),
                                ) {
                                    Text(
                                        text = stringResource(R.string.guide_start),
                                        style = MaterialTheme.typography.labelLarge,
                                    )
                                }
                            } else {
                                GuideTextButton(
                                    text = stringResource(R.string.guide_next),
                                    onClick = { currentPage++ },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun GuidePage(
    title: String,
    description: String,
    icon: @Composable BoxScope.() -> Unit,
    content: (@Composable () -> Unit)? = null,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            modifier = Modifier
                .size(96.dp)
                .background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
            contentAlignment = Alignment.Center,
            content = icon,
        )
        Text(
            text = title,
            modifier = Modifier.padding(top = 20.dp),
            style = MaterialTheme.typography.headlineSmallEmphasized,
            textAlign = TextAlign.Center,
        )
        Text(
            text = description,
            modifier = Modifier.padding(top = 12.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
        content?.let {
            Box(modifier = Modifier.padding(top = 18.dp)) {
                it()
            }
        }
    }
}

@Composable
private fun GuideIcon(imageVector: androidx.compose.ui.graphics.vector.ImageVector) {
    Icon(
        imageVector = imageVector,
        contentDescription = null,
        modifier = Modifier.size(44.dp),
        tint = MaterialTheme.colorScheme.onPrimaryContainer,
    )
}

@Composable
private fun GuideIcon(painter: Painter) {
    Icon(
        painter = painter,
        contentDescription = null,
        modifier = Modifier.size(44.dp),
        tint = MaterialTheme.colorScheme.onPrimaryContainer,
    )
}

@Composable
private fun FeatureRow(text: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = Icons.Outlined.CheckCircle,
            contentDescription = null,
            modifier = Modifier.size(24.dp),
            tint = MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.width(8.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

@Composable
private fun LinkBlock(url: String, action: String) {
    val interactionSource = remember { MutableInteractionSource() }
    val haptic = LocalHapticFeedback.current
    val uriHandler = LocalUriHandler.current
    val target = if (url.startsWith("https://")) url else "https://$url"

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .folkPressScale(interactionSource)
            .background(
                color = MaterialTheme.colorScheme.surfaceContainerHighest,
                shape = FolkShape.Corner16,
            )
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                role = Role.Button,
                onClickLabel = action,
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    uriHandler.openUri(target)
                },
            )
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = url,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.labelLarge.copy(fontFeatureSettings = "tnum"),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Spacer(Modifier.width(12.dp))
        Icon(
            imageVector = Icons.AutoMirrored.Outlined.OpenInNew,
            contentDescription = null,
            modifier = Modifier.size(20.dp),
        )
    }
}

@Composable
private fun PageIndicator(currentPage: Int) {
    val description = stringResource(
        R.string.guide_page_indicator,
        currentPage + 1,
        GuidePageCount,
    )
    Row(
        modifier = Modifier.semantics { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        repeat(GuidePageCount) { page ->
            val selected = page == currentPage
            Box(
                Modifier
                    .size(if (selected) 10.dp else 8.dp)
                    .background(
                        color = if (selected) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f)
                        },
                        shape = CircleShape,
                    ),
            )
        }
    }
}

@Composable
private fun GuideTextButton(text: String, onClick: () -> Unit) {
    FolkTextButton(
        onClick = onClick,
        modifier = Modifier.heightIn(min = 48.dp),
    ) {
        Text(text = text, style = MaterialTheme.typography.labelLarge)
    }
}
