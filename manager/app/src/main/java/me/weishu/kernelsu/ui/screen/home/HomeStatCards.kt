package me.weishu.kernelsu.ui.screen.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.FolkMetricValue
import me.weishu.kernelsu.ui.theme.FolkType
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** Height every tile shares, so the pair of counts lines up whatever their numbers are. */
private val TileHeight = 88.dp

/**
 * The two tallies the manager keeps - granted apps and installed modules - as a pair of tiles: the
 * number in display type with its name underneath, each one a door into its own list.
 */
@Composable
internal fun StatsTiles(
    superuserCount: Int,
    moduleEnabledCount: Int,
    onOpenSuperUser: () -> Unit,
    onOpenModule: () -> Unit,
    modifier: Modifier = Modifier,
    available: Boolean = true,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        StatTile(
            value = superuserCount.takeIf { available }?.toString(),
            label = stringResource(R.string.superuser),
            onClick = onOpenSuperUser,
            modifier = Modifier.weight(1f),
        )
        StatTile(
            value = moduleEnabledCount.takeIf { available }?.toString(),
            label = stringResource(R.string.module),
            onClick = onOpenModule,
            modifier = Modifier.weight(1f),
        )
    }
}

/** One tappable tile: a number, then what it counts. */
@Composable
private fun StatTile(
    value: String?,
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    HomeCard(modifier = modifier, onClick = onClick.takeIf { value != null }) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .height(TileHeight)
                .padding(14.dp),
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            FolkMetricValue(
                value = value,
                unavailableLabel = stringResource(R.string.metric_unavailable),
                style = FolkType.Metric,
                unavailableStyle = FolkType.Caption,
                color = MiuixTheme.colorScheme.onSurface,
                unavailableColor = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
            Text(
                text = label,
                style = FolkType.Caption,
                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}
