#!/usr/bin/env python3
import hashlib
import shlex
import subprocess
import sys
from pathlib import Path
import tempfile


def main():
    if len(sys.argv) != 5:
        print("usage: test_decapp_buffer.py <dec_app> <dec_app_buffer> <enc_app> <data_dir>")
        return 1
    dec_app, dec_app_buffer, enc_app, data_dir = sys.argv[1:5]
    data_dir = Path(data_dir)
    dec_cfg = data_dir / "dec.cfg"
    enc_cfg = data_dir / "enc.cfg"
    yuv = data_dir / "sample.yuv"
    bitstream = data_dir / "sample.jxs"
    if not yuv.exists():
        yuv.write_bytes(b"\x00" * 384)
    if not bitstream.exists():
        enc_args = shlex.split(enc_cfg.read_text())
        subprocess.run([enc_app, '-i', str(yuv), *enc_args, '-b', str(bitstream)], check=True)
    dec_args = shlex.split(dec_cfg.read_text())
    tmp1 = tempfile.NamedTemporaryFile(delete=False)
    tmp2 = tempfile.NamedTemporaryFile(delete=False)
    tmp1.close(); tmp2.close()
    try:
        subprocess.run([dec_app, '-i', str(bitstream), '-o', tmp1.name, *dec_args], check=True)
        subprocess.run([dec_app_buffer, '-i', str(bitstream), '-o', tmp2.name, *dec_args], check=True)
        hash1 = hashlib.sha256(Path(tmp1.name).read_bytes()).hexdigest()
        hash2 = hashlib.sha256(Path(tmp2.name).read_bytes()).hexdigest()
        print(hash1, tmp1.name)
        print(hash2, tmp2.name)
        if hash1 != hash2:
            print("decoded outputs differ", file=sys.stderr)
            return 1
        return 0
    finally:
        Path(tmp1.name).unlink(missing_ok=True)
        Path(tmp2.name).unlink(missing_ok=True)


if __name__ == '__main__':
    sys.exit(main())
