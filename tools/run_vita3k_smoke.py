#!/usr/bin/env python3
"""Run one installed test app and require Vita3K to exit cleanly on SIGTERM."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--app', default='SF4CE0000')
    parser.add_argument('--seconds', type=float, default=180)
    parser.add_argument('--screenshots', default='15,40,60,100,110,120,150,175')
    parser.add_argument('--log', type=Path, default=Path('build/vita3k-smoke.log'))
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    env = dict(os.environ, VITA3K_AUTO_SCREENSHOT=args.screenshots)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.emulator.resolve()), '--keep-config', '--load-config',
               '-c', str(args.config.resolve()), '-r', args.app]
    with args.log.open('wb') as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        print(f'Test process {process.pid}; log: {args.log}', flush=True)
        try:
            try:
                status = process.wait(timeout=args.seconds)
                print(f'Emulator exited before the end of the test: {status}', file=sys.stderr)
                return 1
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    status = process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    print('SIGTERM failed to stop the test process', file=sys.stderr)
                    return 1
            if status != 0:
                print(f'Emulator shutdown failed: {status}', file=sys.stderr)
                return 1
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    text = args.log.read_text(errors='replace')
    if 'SDL quit requested; stopping the application session' not in text or 'Stopping -> Idle' not in text:
        print('Clean session shutdown was not confirmed in the log', file=sys.stderr)
        return 1
    print('Smoke run completed; SIGTERM stopped and cleaned up the session (exit 0)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
