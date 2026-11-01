import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess


# The tree supports one series. The 5.10..5.15 kernels needed the legacy modpost
# arguments and 6.18 needed `MO=`; both branches went away with them.
KMIS = ['android14-6.1']
root = Path(__file__).resolve().parents[3]
tests = Path(__file__).resolve().parent


def linux_path(path):
    path = path.resolve()
    if os.name != 'nt':
        return str(path)
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]


def invoke(command, log, linux=False):
    if linux and os.name == 'nt':
        command = ['wsl', '-d', args.wsl_distribution, '-u', 'root', '--', *command]
    with log.open('wb') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'{log}: command failed ({result.returncode})')


def initramfs(init, module):
    entries = [(name, stat.S_IFDIR | 0o755, b'', 0, 0)
               for name in ('dev', 'proc', 'sys')]
    entries += [('dev/console', stat.S_IFCHR | 0o600, b'', 5, 1),
                ('init', stat.S_IFREG | 0o755, init.read_bytes(), 0, 0),
                ('probe.ko', stat.S_IFREG | 0o600, module.read_bytes(), 0, 0),
                ('TRAILER!!!', 0, b'', 0, 0)]
    output = bytearray()
    for inode, (name, mode, data, major, minor) in enumerate(entries, 1):
        name = name.encode() + b'\0'
        fields = [inode, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0,
                  len(data), 0, 0, major, minor, len(name), 0]
        output += b'070701' + ''.join(f'{value:08x}' for value in fields).encode() + name
        output += b'\0' * (-len(output) % 4)
        output += data
        output += b'\0' * (-len(output) % 4)
    return output + b'\0' * (-len(output) % 512)


def verify(kmi):
    build = Path(args.out).resolve() / kmi
    build.mkdir(parents=True, exist_ok=True)
    backend = root / 'kernel/feature/yukizygisk/endpoint.c'
    for destination, source in [('probe.c', tests / 'kernel_endpoint_probe.c'),
                                ('endpoint_impl.c', backend),
                                ('prepare_modpost.sh', tests / 'prepare_endpoint_modpost.sh')]:
        shutil.copyfile(source, build / destination)
    kbuild = (root / 'kernel/feature/yukizygisk/Kbuild').read_text()
    namespace = 'yz-endpoint-symvers :=' + kbuild.split('yz-endpoint-symvers :=', 1)[1]
    (build / 'Makefile').write_text('''obj-m := endpoint_probe.o
endpoint_probe-y := probe.o
ccflags-y += -I/production/kernel/feature/yukizygisk -I/production
ifneq ($(shell grep -A3 'sched_process_exit,' $(srctree)/include/trace/events/sched.h | grep -c 'bool group_dead'),0)
ccflags-y += -DYZ_EXIT_HAS_GROUP_DEAD
endif
''' + namespace, newline='\n')
    modpost = '/probe/modpost-tool/modpost'
    command = ('bash /probe/prepare_modpost.sh && make -C "$KDIR" M=/probe '
               + f'ARCH=arm64 LLVM=1 CC=clang MODPOST="{modpost}" modules '
               + '&& cp "$KDIR/arch/arm64/boot/Image" /probe/Image')
    invoke(['docker', 'run', '--rm', '--pull=never', '--entrypoint', 'bash',
            '--mount', f'type=bind,source={build},target=/probe',
            '--mount', f'type=bind,source={root},target=/production,readonly',
            f'ghcr.io/ylarod/ddk:{kmi}-{args.ddk_release}', '-lc', command], build / 'build.log')
    backend_hash = hashlib.sha256((build / 'endpoint_impl.c').read_bytes()).hexdigest()
    results = []
    for abi, compiler in [('64', 'aarch64-linux-gnu-gcc'), ('32', 'arm-linux-gnueabihf-gcc')]:
        init = build / ('init' + abi)
        invoke([compiler, '-static', '-O2', '-I' + linux_path(root),
                linux_path(tests / 'endpoint_kernel_test.c'), '-o', linux_path(init)],
               build / ('compile' + abi + '.log'), linux=True)
        cpio = build / ('init' + abi + '.cpio')
        cpio.write_bytes(initramfs(init, build / 'endpoint_probe.ko'))
        log = build / ('guest' + abi + '.log')
        invoke(['timeout', '90', 'qemu-system-aarch64', '-machine', 'virt,gic-version=3',
                '-cpu', 'max', '-accel', 'tcg', '-smp', '4', '-m', '1536',
                '-nodefaults', '-nographic', '-serial', 'stdio', '-monitor', 'none',
                '-nic', 'none', '-no-reboot', '-kernel', linux_path(build / 'Image'),
                '-initrd', linux_path(cpio), '-append',
                'console=ttyAMA0 rdinit=/init panic=-1 panic_on_warn=1 loglevel=6 kaslr.disabled=1'],
               log, linux=True)
        text = log.read_text(errors='replace')
        if ('ENDPOINT_GUEST_PASS' not in text or
                any(value in text for value in ('ENDPOINT_GUEST_FAIL', 'WARNING:', 'Kernel panic'))):
            raise RuntimeError(f'Guest test failed: {log}')
        results.append({'kmi': kmi, 'abi': abi, 'pass': True,
                        'backend_sha256': backend_hash})
        print(f'PASS: {kmi} ARM{abi}', flush=True)
    (build / 'results.json').write_text(json.dumps(results, indent=2))


parser = argparse.ArgumentParser(description='Test the production endpoint backend in isolated GKI guests.')
parser.add_argument('--out', required=True)
parser.add_argument('--kmi', choices=KMIS, action='append')
parser.add_argument('--ddk-release', default='20260828')
parser.add_argument('--wsl-distribution', default='Ubuntu')
args = parser.parse_args()
for kmi in args.kmi or KMIS:
    verify(kmi)
