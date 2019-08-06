#!/usr/bin/env python3

import os
import subprocess
import tempfile
from multiprocessing.pool import ThreadPool

os.chdir(os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..')))

def run_clang_format(path):
    with tempfile.NamedTemporaryFile(delete=False) as tf:
        cmd = ['docker', 'run',
               '--rm',
               '-v', os.path.realpath('.') + ':/alephzero',
               '-w', '/alephzero',
               'alephzero-clang-format', '-style', 'file', path]
        subprocess.call(
            cmd,
            stdin=subprocess.PIPE,
            stdout=tf,
            stderr=subprocess.PIPE)
        return path, tf.name


pool = ThreadPool(100)
clang_format_futures = []

for dirpath, _, files in os.walk('include'):
    for filename in sorted(files):
        path = os.path.join(dirpath, filename)
        clang_format_futures.append(pool.apply_async(run_clang_format, (path,)))

for dirpath, _, files in os.walk('src'):
    for filename in sorted(files):
        path = os.path.join(dirpath, filename)
        clang_format_futures.append(pool.apply_async(run_clang_format, (path,)))

for fut in clang_format_futures:
    orig_path, new_path = fut.get()
    orig_data = open(orig_path).read()
    new_data = open(new_path).read()
    new_data = new_data.replace('\r\n', '\n')  # ???
    if orig_data != new_data:
        subprocess.Popen(
            ['meld', '-n', orig_path, new_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
