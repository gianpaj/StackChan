#!/usr/bin/env python3
"""Test the production adapter and patched Oido engine with public PCM.

Run after fetching firmware dependencies: python3 firmware/tests/oido_recognizer_test.py
IDF mapping, clock, task and SHA primitives are mocked. The runner verifies the
model digest before using the mock SHA result. Engine math and adapter code run.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import wave

root = Path(__file__).resolve().parents[1]
engine = root / 'oido/esp32/components/tinyasr'
model = root / 'oido/models/oido_v2b.tnm'
assert hashlib.sha256(model.read_bytes()).hexdigest() == '9e8b40913f532fb44b716a1f882f38b3b21e220713e417899aba32ace8c35066'
cc, cxx = shutil.which('clang'), shutil.which('clang++')
if not cc or not cxx:
    raise SystemExit('clang and clang++ are required')
headers = {
    'esp_err.h': '''#pragma once
using esp_err_t = int;
enum { ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NOT_FINISHED=1, ESP_ERR_NOT_FOUND,
ESP_ERR_INVALID_SIZE, ESP_ERR_INVALID_CRC, ESP_ERR_NO_MEM,
ESP_ERR_INVALID_RESPONSE, ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_ARG,
ESP_ERR_INVALID_STATE };
''',
    'itofs_service.h': '''#pragma once
#include <cstddef>
struct itofs_workspace_t {void* hot; size_t hot_bytes; void* psram; size_t psram_bytes;};
''',
    'esp_partition.h': '''#pragma once
#include <cstddef>
#include "esp_err.h"
struct esp_partition_t {size_t size;};
using esp_partition_mmap_handle_t = unsigned;
enum {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, ESP_PARTITION_MMAP_DATA};
const esp_partition_t* esp_partition_find_first(int,int,const char*);
esp_err_t esp_partition_mmap(const esp_partition_t*,size_t,size_t,int,const void**,unsigned*);
void esp_partition_munmap(unsigned);
''',
    'esp_timer.h': '#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
    'freertos/FreeRTOS.h': '#pragma once\n',
    'freertos/task.h': '#pragma once\nvoid vTaskDelay(unsigned);\n',
    'mbedtls/sha256.h': '''#pragma once
#include <cstddef>
struct mbedtls_sha256_context {size_t bytes;};
void mbedtls_sha256_init(mbedtls_sha256_context*);
int mbedtls_sha256_starts(mbedtls_sha256_context*,int);
int mbedtls_sha256_update(mbedtls_sha256_context*,const unsigned char*,size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context*,unsigned char*);
void mbedtls_sha256_free(mbedtls_sha256_context*);
''',
}
with tempfile.TemporaryDirectory(prefix='stackchan-oido-test-') as directory:
    work = Path(directory)
    for name, contents in headers.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    with wave.open(str(root / 'oido/docs/demo/examples/b05.wav')) as audio:
        assert audio.getframerate() == 16000 and audio.getnchannels() == 1 and audio.getsampwidth() == 2
        (work / 'pcm.raw').write_bytes(audio.readframes(audio.getnframes()))
    flags = ['-O2', '-g', '-ffp-contract=off', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-I', str(work), '-I', str(engine / 'include'),
             '-I', str(engine), '-I', str(root / 'main/conversation')]
    objects = []
    for name in ('tinyasr', 'kernels', 'tinyasr_lm', 'tasr_nemo', 'tasr_seg'):
        obj = work / f'{name}.o'
        subprocess.run([cc, '-std=c11', '-D_DEFAULT_SOURCE', '-Wno-unused-function',
                        *flags, '-c', str(engine / f'{name}.c'), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = work / 'recognizer_test'
    subprocess.run([cxx, '-std=c++17', '-Wall', '-Wextra', *flags,
                    str(root / 'tests/oido_recognizer_test.cpp'),
                    str(root / 'main/conversation/oido_recognizer.cc'), *objects,
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(model), str(work / 'pcm.raw')], check=True)
