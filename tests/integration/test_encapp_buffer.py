#!/usr/bin/env python3
import hashlib
import os
import shlex
import subprocess
import sys
from pathlib import Path
import tempfile


def main():
    if len(sys.argv) != 4:
        print("usage: test_encapp_buffer.py <enc_app> <enc_app_buffer> <data_dir>")
        return 1
    enc_app, enc_app_buffer, data_dir = sys.argv[1:4]
    data_dir = Path(data_dir)
    cfg = data_dir / "enc.cfg"
    yuv = data_dir / "sample.yuv"
    if not yuv.exists():
        yuv.write_bytes(b"\x00" * 384)
    args = shlex.split(cfg.read_text())

    # ensure the applications can locate their shared libraries on Windows by
    # prepending the binary directory to PATH
    bin_dir = Path(enc_app).resolve().parent
    env = dict(os.environ)
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")

    tmp1 = tempfile.NamedTemporaryFile(delete=False, suffix=".jxs")
    tmp2 = tempfile.NamedTemporaryFile(delete=False, suffix=".jxs")
    tmp1.close()
    tmp2.close()

    def run(cmd):
        proc = subprocess.run(cmd, env=env, text=True, capture_output=True)
        if proc.returncode != 0:
            print("command failed:", shlex.join(cmd), file=sys.stderr)
            if proc.stdout:
                print(proc.stdout, file=sys.stderr)
            if proc.stderr:
                print(proc.stderr, file=sys.stderr)
            raise SystemExit(proc.returncode)

    try:
        run([enc_app, '-i', str(yuv), *args, '-b', tmp1.name])
        run([enc_app_buffer, '-i', str(yuv), *args, '-b', tmp2.name])
        hash1 = hashlib.sha256(Path(tmp1.name).read_bytes()).hexdigest()
        hash2 = hashlib.sha256(Path(tmp2.name).read_bytes()).hexdigest()
        print(hash1, tmp1.name)
        print(hash2, tmp2.name)
        if hash1 != hash2:
            print("bitstreams differ", file=sys.stderr)
            return 1
        return 0
    finally:
        for tmp in (tmp1.name, tmp2.name):
            try:
                Path(tmp).unlink()
            except FileNotFoundError:
                pass


if __name__ == '__main__':
    sys.exit(main())
