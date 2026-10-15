#!/usr/bin/env python3
"""Compile and run asset-free port regression tests with ASan/UBSan."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='sf4ce-tests-') as temporary:
        for test in sorted((root / 'tests').glob('*_test.cpp')):
            executable = Path(temporary) / test.stem
            sources = [str(test)]
            if test.stem == 'render_interpolation_test':
                sources += [str(root / 'loader/render_interpolation.cpp')]
            command = [args.compiler, '-std=c++17', '-g', '-O1',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       '-I' + str(root / 'tests/gl_stub'), *sources, '-o', str(executable)]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)
    print('All host regression tests passed.')


if __name__ == '__main__':
    main()
