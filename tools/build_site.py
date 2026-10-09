#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build this game's standalone Pages site using the pinned ZuluBlaster tools."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_COMMIT = "fea018441580b3ba17d5f54f493b639f515e189d"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=ROOT.parent / 'p2000c-zulublaster-sasi-drive')
    parser.add_argument('--emulator', type=Path, help='Path to compiled p2000c-web, without suffix')
    parser.add_argument('--check', type=Path, help='Headless emulator for real boot validation')
    args = parser.parse_args()
    toolchain = args.toolchain.resolve()
    actual = subprocess.check_output(['git', '-C', str(toolchain), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != TOOLCHAIN_COMMIT:
        parser.error(f'Toolchain must be at {TOOLCHAIN_COMMIT}, got {actual}')
    sys.path.insert(0, str(toolchain / 'src'))
    from p2000c_disk.builder import build_image
    from p2000c_disk.config import ConfigUpdates, apply_config_updates
    from p2000c_disk.filesystem import put_files
    from p2000c_disk.menu_boot import cold_menu_boot
    from p2000c_disk.site import build_site
    from p2000c_disk.verify import require_valid_image
    config = json.loads((ROOT / 'web/game.json').read_text())
    artifacts = [ROOT / 'build' / name for name in config['artifacts']]
    for path in artifacts:
        if not path.is_file():
            parser.error(f'Missing {path}; run make build first')
    media = ROOT / 'build/site-media'
    media.mkdir(parents=True, exist_ok=True)
    for name in ['HD0_256.hda', 'HD1_256.hda']:
        build_image(media / name, toolchain / 'assets/boot/hdboot-split.trk', layout='split')
    disk = media / 'HD0_256.hda'
    put_files(disk, artifacts, partition='low', user_number=0)
    apply_config_updates(disk, ConfigUpdates(autostart='A:' + config['command']))
    # Start only on cold boot/reset, so quitting returns to CP/M without looping.
    with disk.open('r+b') as stream:
        system = cold_menu_boot(stream.read(8192))
        stream.seek(0)
        stream.write(system)
    for name in ['HD0_256.hda', 'HD1_256.hda']:
        require_valid_image(media / name)
    if args.check:
        command = [str(args.check.resolve()), '--ipl', str(toolchain / 'tools/emulator/firmware/IPLDUMP.BIN'),
                   '--hard-disk-0', str(disk), '--hard-disk-1', str(media / 'HD1_256.hda'),
                   '--fast-storage']
        if config.get('splash_cycles'):
            command += ['--run', str(config['splash_cycles']), '--send', ' ']
        if config.get('splash_marker'):
            command += ['--wait-for', config['splash_marker'], '--send', ' ']
        command += ['--wait-for', config['boot_marker'], '--send', 'Q', '--wait-for', 'A>', '--run', '5000000', '--output', 'json']
        result = subprocess.run(command, timeout=120, capture_output=True, text=True)
        if result.returncode:
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit(result.returncode)
        if config['boot_marker'] in result.stdout:
            raise SystemExit('Game restarted after quitting instead of returning to CP/M')
        print('Verified direct boot and exit to CP/M:', config['id'])
    emulator = (args.emulator or toolchain / 'build/emulator-web/p2000c-web').resolve()
    # Supply the game's web files with the shared, licensed ROM and site assembler.
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='site-input-') as temp:
        inputs = Path(temp)
        shutil.copytree(ROOT / 'web', inputs / 'web')
        firmware = inputs / 'tools/emulator/firmware'
        firmware.mkdir(parents=True)
        shutil.copyfile(toolchain / 'tools/emulator/firmware/IPLDUMP.BIN', firmware / 'IPLDUMP.BIN')
        (inputs / 'web/game.js').write_text('window.GAME = ' + json.dumps(config, ensure_ascii=False) + ';\n')
        shutil.copyfile(toolchain / 'web/p2000c-font.png', inputs / 'web/p2000c-font.png')
        for index, source in enumerate(config['screenshots']):
            target = inputs / 'web/screenshots' / f'{index}.png'
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(ROOT / source, target)
        # The QR codes embedded in Chess still point to /#FEN. Keep that contract.
        if config['command'] == 'SCHAKEN':
            subprocess.run([sys.executable, str(ROOT / 'tools/gen_site.py')], check=True)
            shutil.copytree(ROOT / 'build/site', inputs / 'web/position')
        out = build_site(ROOT / '_site', media, emulator, root=inputs)
    licenses = out / 'licenses'
    licenses.mkdir()
    shutil.copyfile(ROOT / 'LICENSE', licenses / 'game.txt')
    shutil.copyfile(toolchain / 'LICENSE', licenses / 'zulublaster.txt')
    for core in ['superzazu_z80', 'blink16_8086']:
        shutil.copyfile(toolchain / 'tools/emulator/src/third_party' / core / 'LICENSE', licenses / (core + '.txt'))
    (out / 'provenance.json').write_text(json.dumps({
        'toolchain_repository': 'https://github.com/ifilot/p2000c-zulublaster-sasi-drive',
        'toolchain_commit': TOOLCHAIN_COMMIT,
        'game_commit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'artifacts': config['artifacts'],
    }, indent=2) + '\n')
    print(f'Built {out}')

if __name__ == '__main__':
    main()
