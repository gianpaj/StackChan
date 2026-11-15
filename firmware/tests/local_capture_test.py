#!/usr/bin/env python3
"""Run production capture methods against failing codec, power and I2S calls."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root / '../main/hal/board/cores3_audio_codec.cc').resolve().read_text()
methods = []
for name in ('LocalMute', 'LocalWrite', 'LocalSetVolume', 'LocalStartCapture', 'LocalReadCapture', 'LocalEndCapture', 'LocalOutputReady', 'LocalLogOutputState'):
    start = source.index(f'bool CoreS3AudioCodec::{name}(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end])
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('A C++ compiler is required')
with tempfile.TemporaryDirectory(prefix='stackchan-capture-test-') as temp:
    work = Path(temp)
    (work / 'capture_methods.inc').write_text('\n\n'.join(methods))
    binary = work / 'capture_test'
    subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I', str(work),
                    str(root / 'local_capture_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
