package me.weishu.kernelsu.ui.component.module

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.ui.component.card.FolkCardFrame
import me.weishu.kernelsu.ui.theme.LocalModuleDescriptionMaxLines
import top.yukonga.miuix.kmp.basic.LinearProgressIndicator
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** One chip on a module card: its text plus the pair of colours it is drawn with. */
@Immutable
data class ModuleCardLabel(
    val text: String,
    val containerColor: Color,
    val contentColor: Color,
)

/**
 * The body both module cards share: the chips, the title and its metadata lines, the switch at the
 * top right, the description, a load error and an operation progress bar.
 *
 * The two screens that list modules - APM and kernel modules - describe their entries with
 * different fields but draw them identically. Everything below the description is the caller's
 * business, so the action rows stay in the screens that own their actions.
 *
 * Every piece of text here comes from the miuix type ramp, which is what the card frame around it
 * is drawn from as well.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ModuleCardShell(
    title: String,
    labels: List<ModuleCardLabel>,
    subtitles: List<String>,
    description: String?,
    expanded: Boolean,
    onClickLabel: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    titleDecoration: TextDecoration? = null,
    descriptionMaxLines: Int = LocalModuleDescriptionMaxLines.current,
    errorText: String? = null,
    loading: Boolean = false,
    switch: (@Composable () -> Unit)? = null,
    footer: @Composable ColumnScope.() -> Unit,
) {
    FolkCardFrame(
        expanded = expanded,
        onClickLabel = onClickLabel,
        onClick = onClick,
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
                Column(Modifier.weight(1f)) {
                    if (labels.isNotEmpty()) {
                        FlowRow(
                            modifier = Modifier.padding(bottom = 8.dp),
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp),
                        ) {
                            labels.forEach { label ->
                                ModuleLabel(
                                    text = label.text,
                                    containerColor = label.containerColor,
                                    contentColor = label.contentColor,
                                )
                            }
                        }
                    }
                    Text(
                        text = title,
                        style = MiuixTheme.textStyles.headline1,
                        textDecoration = titleDecoration,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                    subtitles.forEach { line ->
                        Spacer(Modifier.height(2.dp))
                        Text(
                            text = line,
                            style = MiuixTheme.textStyles.body2,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            textDecoration = titleDecoration,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
                switch?.invoke()
            }

            if (!description.isNullOrBlank()) {
                Spacer(Modifier.height(12.dp))
                Text(
                    text = description,
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                    textDecoration = titleDecoration,
                    maxLines = descriptionMaxLines,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (errorText != null) {
                Spacer(Modifier.height(8.dp))
                Text(
                    text = errorText,
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.error,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (loading) {
                Spacer(Modifier.height(12.dp))
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            footer()
        }
    }
}
