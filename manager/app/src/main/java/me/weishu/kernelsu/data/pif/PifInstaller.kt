package me.weishu.kernelsu.data.pif

import android.util.Base64
import com.topjohnwu.superuser.Shell
import java.io.File
import me.weishu.kernelsu.data.block
import me.weishu.kernelsu.data.model.PifConfig
import me.weishu.kernelsu.data.model.PifOption
import me.weishu.kernelsu.data.model.PifProp
import me.weishu.kernelsu.data.model.PifStatus
import me.weishu.kernelsu.ksuApp

/**
 * The bundled PlayIntegrityFix payload: where it lives, what it is doing, and the one file this page
 * edits.
 *
 * The payload belongs to ksud. It ships inside `libksud.so`, ksud writes it to [ROOT] on every boot
 * and links that directory into `/data/adb/modules` there - because the library it carries opens
 * `/data/adb/modules/playintegrityfix/{module.prop,pif.prop,classes.dex}` by absolute name and has
 * no setting to move them. The link is a symbolic link, which keeps it out of the module list, so
 * the metamodule that publishes the module tree never sees it.
 *
 * So nothing here installs or removes a module: this object reports what ksud left on disk, flips
 * the disable marker that zygiskd reads, and edits `pif.prop` - the identity and the six switches.
 * The one write that belongs to ksud is [reinstall], which is how a payload whose files were damaged
 * is put back.
 */
object PifInstaller {
    /** Upstream release carried by the payload; keep in sync with `pif::VERSION`. */
    const val VERSION = "v4.9-inject-s"

    /** Where ksud installs the payload. Mirrors `pif::ROOT`. */
    const val ROOT = "/data/adb/ksu/lib/yukizygisk/builtin/playintegrityfix"

    /** The path the library opens by name; a symbolic link to [ROOT]. Mirrors `pif::LINK`. */
    const val MODULE_DIR = "/data/adb/modules/playintegrityfix"

    /** The file this page edits; kept as a name because the directory can differ, see [activeDir]. */
    private const val PROP_FILE = "pif.prop"

    /** Markers around the configuration inside the single probe; no prop file carries these. */
    private const val CONFIG_BEGIN = "__pif_config_begin__"
    private const val CONFIG_END = "__pif_config_end__"

    /** Current state of the payload and the file the page edits. */
    fun status(): PifStatus {
        // One shell for the whole picture. Which directory is in force and what is in it are
        // decided on a single line, so opening the page costs one round trip instead of three.
        val raw = output(
            "d=$ROOT; " +
                "if [ -d $MODULE_DIR ] && [ ! -L $MODULE_DIR ]; then d=$MODULE_DIR; echo external=1; fi; " +
                "if [ -f \"\$d/module.prop\" ]; then echo installed=1; else echo installed=0; fi; " +
                "if [ -f \"\$d/disable\" ]; then echo disabled=1; else echo disabled=0; fi; " +
                // Linked means zygiskd loads the built-in copy; a real directory at the module path
                // means an upstream install owns it and the built-in payload sits there unused.
                "if [ -L $MODULE_DIR ]; then echo linked=1; fi; " +
                "grep -m1 '^version=' \"\$d/module.prop\" 2>/dev/null; " +
                "echo $CONFIG_BEGIN; cat \"\$d/$PROP_FILE\" 2>/dev/null; echo $CONFIG_END",
        )
        return PifStatus.fromProbe(raw, block(raw, CONFIG_BEGIN, CONFIG_END))
    }

    /**
     * The directory whose files are the ones in force.
     *
     * An upstream install owns the module path, so its `pif.prop` and its `disable` marker are what
     * decide the verdict - editing the built-in copy next to it would write to a file nothing
     * reads, and the page would report a state the device is not in.
     */
    private fun activeDir(external: Boolean): String = if (external) MODULE_DIR else ROOT

    /**
     * Asks ksud to write the payload again, keeping `pif.prop`.
     *
     * A ksud update does this on the next boot by itself; this is for the case where the files were
     * damaged or the link went stale and the user does not want to wait for a reboot.
     */
    fun reinstall() {
        val result = run("$DAEMON pif install")
        check(result.isSuccess) { "could not write the payload under $ROOT" }
    }

    /**
     * Takes over an upstream install at [MODULE_DIR], keeping its `pif.prop`.
     *
     * Only meaningful while [PifStatus.external] is true: upstream's module owns the path, so the
     * built-in payload cannot be linked until that directory is gone. ksud runs upstream's own
     * `uninstall.sh` first so the properties it changed go back.
     */
    fun adopt() {
        val result = run("$DAEMON pif adopt")
        check(result.isSuccess) { "could not take over $MODULE_DIR" }
    }

    /**
     * Replaces `pif.prop` with a file the user picked.
     *
     * The identity is what this payload is for, and upstream ships a WebUI to edit it; here the
     * file the module actually reads is the editor, so an import is the edit. [base64] carries the
     * bytes over the shell the manager already holds, the same way the keybox import does.
     */
    fun importProp(base64: String) {
        require(base64.isNotBlank()) { "empty configuration" }
        val current = status()
        check(current.installed) { "$MODULE_DIR is not installed" }
        val text = String(Base64.decode(base64, Base64.NO_WRAP), Charsets.UTF_8)
        check(PifProp.isUsable(text)) { "not a PlayIntegrityFix configuration" }
        writeProp("${activeDir(current.external)}/$PROP_FILE", text)
    }

    /**
     * Turns the payload on or off.
     *
     * The marker is ksud's and zygiskd's, so this is the same switch the engine's own page uses; the
     * library is only handed to zygote at start-up, which is why the caller says "reboot to apply".
     */
    fun setEnabled(enabled: Boolean) {
        val current = status()
        check(current.installed) { "$MODULE_DIR is not installed" }
        val marker = "${activeDir(current.external)}/disable"
        val result = if (enabled) {
            run("rm -f $marker")
        } else {
            run("touch $marker", "chmod 0644 $marker")
        }
        check(result.isSuccess) { "could not write $marker" }
    }

    /** Flips one switch in `pif.prop`, leaving every other line untouched. */
    fun setOption(option: PifOption, value: Boolean) {
        val state = status()
        check(state.installed) { "$MODULE_DIR is not installed" }
        val path = "${activeDir(state.external)}/$PROP_FILE"
        val current = output("cat $path 2>/dev/null")
        check(current.isNotBlank()) { "$path is missing" }
        writeProp(path, PifProp.withOption(current, option, value))
    }

    fun config(): PifConfig = status().config

    /**
     * Publishes `pif.prop` atomically.
     *
     * The library reads the file during zygote start-up, so a half-written one would be read as a
     * shorter config; writing to a temporary name and renaming keeps the previous contents until the
     * replacement is complete.
     */
    private fun writeProp(path: String, text: String) {
        val encoded = singleLineBase64(
            Base64.encodeToString(text.toByteArray(Charsets.UTF_8), Base64.NO_WRAP),
        )
        val result = run(
            "echo '$encoded' | base64 -d > $path.tmp",
            "chmod 0644 $path.tmp",
            "chown 0:0 $path.tmp",
            "mv -f $path.tmp $path",
        )
        check(result.isSuccess) { "could not write $path" }
    }

    /** [run] splits on newlines, so a wrapped payload would be executed as several commands. */
    private fun singleLineBase64(encoded: String): String =
        encoded.filterNot { it.isWhitespace() }.also {
            require(it.isNotEmpty()) { "empty base64 payload" }
        }

    /** The ksud binary this app ships, which is also what owns the payload's layout. */
    private val DAEMON: String
        get() = File(ksuApp.applicationInfo.nativeLibraryDir, "libksud.so").absolutePath

    private fun job(vararg commands: String): Shell.Job {
        val lines = commands.asSequence()
            .flatMap { it.lineSequence() }
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .toList()
        return Shell.getShell().newJob().add(*lines.toTypedArray())
    }

    private fun run(vararg commands: String): Shell.Result = job(*commands).exec()

    private fun output(vararg commands: String): String {
        val lines = ArrayList<String>()
        job(*commands).to(lines).exec()
        return lines.joinToString("\n")
    }
}
