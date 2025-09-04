#!/usr/bin/env bash
set -euo pipefail
DEC_APP="$1"
DEC_APP_BUFFER="$2"
ENC_APP="$3"
DATA_DIR="$4"
CFG="$DATA_DIR/dec.cfg"
ENC_CFG="$DATA_DIR/enc.cfg"
YUV="$DATA_DIR/sample.yuv"
BITSTREAM="$DATA_DIR/sample.jxs"
TMP1="$(mktemp)"
TMP2="$(mktemp)"
trap 'rm -f "$TMP1" "$TMP2"' EXIT

if [ ! -f "$YUV" ]; then
    dd if=/dev/zero of="$YUV" bs=384 count=1 &>/dev/null
fi
if [ ! -f "$BITSTREAM" ]; then
    "$ENC_APP" -i "$YUV" $(cat "$ENC_CFG") -b "$BITSTREAM"
fi

$DEC_APP -i "$BITSTREAM" -o "$TMP1" $(cat "$CFG")
$DEC_APP_BUFFER -i "$BITSTREAM" -o "$TMP2" $(cat "$CFG")
sha256sum "$TMP1" "$TMP2"
cmp "$TMP1" "$TMP2"
