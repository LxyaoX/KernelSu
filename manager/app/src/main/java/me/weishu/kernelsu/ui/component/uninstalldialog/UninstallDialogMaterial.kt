package me.weishu.kernelsu.ui.component.uninstalldialog

import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.dialog.rememberConfirmDialog
import me.weishu.kernelsu.ui.component.material.FolkTextButton
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedListItem
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.screen.flash.FlashIt
import me.weishu.kernelsu.ui.screen.flash.UninstallType
import me.weishu.kernelsu.ui.screen.flash.UninstallType.PERMANENT
import me.weishu.kernelsu.ui.screen.flash.UninstallType.RESTORE_STOCK_IMAGE
import top.yukonga.miuix.kmp.preference.ArrowPreference
import me.weishu.kernelsu.ui.component.material.ExpressiveDialog

@Composable
fun UninstallDialogMaterial(
    show: Boolean,
    onDismissRequest: () -> Unit
) {
    val navigator = LocalNavigator.current
    val options = listOf(
        // TEMPORARY,
        PERMANENT,
        RESTORE_STOCK_IMAGE
    )
    val showConfirmDialog = remember { mutableStateOf(false) }
    val runType = remember { mutableStateOf<UninstallType?>(null) }

    val run = { type: UninstallType ->
        when (type) {
            PERMANENT -> navigator.push(Route.Flash(FlashIt.FlashUninstall))
            RESTORE_STOCK_IMAGE -> navigator.push(Route.Flash(FlashIt.FlashRestore))
            else -> Unit
        }
    }

    if (show) {
        ExpressiveDialog(
            onDismissRequest = onDismissRequest,
            title = { Text(stringResource(R.string.settings_uninstall)) },
            text = {
                SegmentedColumn(
                    modifier = Modifier,
                    content = options.map { type ->
                        {
                            ArrowPreference(
                                title = stringResource(type.title),
                                summary = stringResource(type.message),
                                startAction = {
                                    Icon(
                                        imageVector = type.icon,
                                        contentDescription = null
                                    )
                                },
                                onClick = {
                                    showConfirmDialog.value = true
                                    runType.value = type
                                },
                            )
                        }
                    }
                )
            },
            confirmButton = {
                FolkTextButton(onClick = onDismissRequest) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    val confirmDialog = rememberConfirmDialog(
        onConfirm = {
            showConfirmDialog.value = false
            onDismissRequest()
            runType.value?.let { type ->
                run(type)
            }
        },
        onDismiss = {
            showConfirmDialog.value = false
        }
    )

    val dialogTitle = runType.value?.let { type ->
        options.find { it == type }?.let { stringResource(it.title) }
    } ?: ""
    val dialogContent = runType.value?.let { type ->
        options.find { it == type }?.let { stringResource(it.message) }
    }

    if (showConfirmDialog.value) {
        confirmDialog.showConfirm(title = dialogTitle, content = dialogContent)
    }
}
