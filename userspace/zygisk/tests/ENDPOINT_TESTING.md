Endpoint tests use disposable host files and isolated QEMU guests. They do not
connect to Android devices.

- `sudo python3 userspace/zygisk/tests/run_host_tests.py` runs ASan/UBSan tests,
  including endpoint publication, restart discovery and bootstrap ownership.
- `python3 userspace/zygisk/tests/run_bootstrap_tests.py` executes the actual
  ARM32/ARM64 byte templates and patch logic extracted from the injector and
  Tango sources. It needs both GNU cross compilers and `qemu-arm`/`qemu-aarch64`.
  It checks continuation arguments, packet handoff and failed-load fd cleanup.
- `python3 userspace/zygisk/tests/run_endpoint_kernel_tests.py --out /tmp/yz-endpoint`
  builds the production endpoint backend as a test-only module and runs both
  userspace ABIs against the one supported GKI, `android14-6.1` (`--kmi` is still
  accepted and takes only that value). It needs Docker's full DDK images, both GNU cross compilers and
  `qemu-system-aarch64`. On Windows, compilers and QEMU run in the selected WSL
  distribution. Results, exact backend hashes and logs are saved under `--out`.

The guest exercises empty pages, read-only mappings, privileged fd reopening,
copyout rollback, root admission, ABI separation, generations, stale unregister,
actual PID reuse, process exit/exec, feature disable and unload with retained
mappings. Handshakes control lifecycle transitions. PID allocation changes occur
only inside the disposable guest.

The test module uses standard shmem operations and restores the DDK modpost
export/namespace checks for its build. Full YukiSU builds retain the repository's
DDK and `check_symbol` flow. The fixture's root-only ioctl boundary tests the
backend; standalone Viola signature/session admission has separate predicate
tests in that repository.

These checks do not establish Android SELinux compatibility, ART/JNI behavior,
real module initialization, or detector results. Device acceptance still needs
the matched userland/LKM package and explicit authorization for each deployment,
LKM update and reboot operation.
