#!/usr/bin/env bash
set -euo pipefail
ENC_APP="$1"
ENC_APP_BUFFER="$2"
DATA_DIR="$3"
CFG="$DATA_DIR/enc.cfg"
YUV="$DATA_DIR/sample.yuv"
TMP1="$(mktemp)"
TMP2="$(mktemp)"
trap 'rm -f "$TMP1" "$TMP2"' EXIT

if [ ! -f "$YUV" ]; then
    dd if=/dev/zero of="$YUV" bs=384 count=1 &>/dev/null
fi
$ENC_APP -i "$YUV" $(cat "$CFG") -b "$TMP1"
$ENC_APP_BUFFER -i "$YUV" $(cat "$CFG") -b "$TMP2"
sha256sum "$TMP1" "$TMP2"
cmp "$TMP1" "$TMP2"
