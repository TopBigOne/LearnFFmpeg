#!/usr/bin/env python3
"""
把 logcat 里的 AddressSanitizer 报告符号化：调用栈的 "(xxx.so+0x偏移)" 换成 函数名 + 源文件:行号。

用法（在仓库根目录）：
    adb logcat -d -s wrap.sh | python3 scripts/asan_symbolize.py
    adb logcat -d -s wrap.sh | python3 scripts/asan_symbolize.py --abi armeabi-v7a

符号来自 asan 构建的未裁剪 so（app/build/intermediates/merged_native_libs/asan/...），
必须和手机上安装的是同一次构建。只符号化本工程的 so，系统库（libart.so 等）原样保留。
详见 lib_av/src/main/cpp/util/ASAN.md。
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAME = re.compile(r'^(?P<prefix>\s*#\d+\s+0x[0-9a-f]+\s+)\((?P<path>[^()+\s]+)\+(?P<offset>0x[0-9a-f]+)\)(?P<rest>.*)$')
COLOR = re.compile(r'\x1b\[[0-9;]*m')
LOGCAT_PREFIX = re.compile(r'^.*?wrap\.sh\s*:\s?')


def find_ndk():
    if os.environ.get('ANDROID_NDK_HOME'):
        return os.environ['ANDROID_NDK_HOME']
    sdk = os.environ.get('ANDROID_HOME')
    props = os.path.join(ROOT, 'local.properties')
    if not sdk and os.path.isfile(props):
        for line in open(props, encoding='utf-8'):
            if line.startswith('sdk.dir='):
                sdk = line.split('=', 1)[1].strip()
    gradle = open(os.path.join(ROOT, 'lib_av', 'build.gradle.kts'), encoding='utf-8').read()
    version = re.search(r'ndkVersion\s*=\s*"([^"]+)"', gradle).group(1)
    return os.path.join(sdk, 'ndk', version)


def find_symbolizer(ndk):
    prebuilt = os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt')
    for host in os.listdir(prebuilt):
        path = os.path.join(prebuilt, host, 'bin', 'llvm-symbolizer')
        if os.path.isfile(path):
            return path
    sys.exit('llvm-symbolizer not found in ' + prebuilt)


def symbolize(symbolizer, so_path, offset):
    out = subprocess.run([symbolizer, '--obj=' + so_path, '--inlining=false', offset],
                         capture_output=True, text=True).stdout.strip().splitlines()
    if len(out) >= 2 and out[0] != '??':
        return out[0], os.path.relpath(out[1], ROOT) if out[1].startswith(ROOT) else out[1]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--abi', default='arm64-v8a')
    args = parser.parse_args()

    sym_dir = os.path.join(ROOT, 'app', 'build', 'intermediates', 'merged_native_libs', 'asan',
                           'mergeAsanNativeLibs', 'out', 'lib', args.abi)
    if not os.path.isdir(sym_dir):
        sys.exit('symbol dir not found: %s (run ./gradlew :app:assembleAsan first)' % sym_dir)
    symbolizer = find_symbolizer(find_ndk())
    cache = {}
    for raw in sys.stdin:
        line = COLOR.sub('', LOGCAT_PREFIX.sub('', raw.rstrip('\n')))
        m = FRAME.match(line)
        if m:
            so_path = os.path.join(sym_dir, os.path.basename(m.group('path')))
            if os.path.isfile(so_path):
                key = (so_path, m.group('offset'))
                if key not in cache:
                    cache[key] = symbolize(symbolizer, so_path, m.group('offset'))
                if cache[key]:
                    func, location = cache[key]
                    line = '%s%s %s' % (m.group('prefix'), func, location)
        print(line)


if __name__ == '__main__':
    main()
