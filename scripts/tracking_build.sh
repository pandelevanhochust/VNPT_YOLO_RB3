#!/usr/bin/env bash
set -e

# Thiet lap cac thu muc goc va bien moi truong
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC=${TC:-/tmp/aarch64--glibc--stable-2021.11-1}
SDK=${SDK:-/home/ubuntu/snpe-sdk}
CC="$TC/bin/aarch64-linux-gcc"
OUT="$ROOT/build"
INC="-I$SDK/include/QNN -I$ROOT/third_party -I$ROOT/src"
CFLAGS="-O2 -std=gnu11 -Wall -DSTB_IMAGE_WRITE_IMPLEMENTATION"

mkdir -p "$OUT"
echo "CC   = $CC"
echo "SDK  = $SDK"
echo "OUT  = $OUT"

echo "  CC Modules -> Processing Compilation..."

# Bien dich ket hop tat ca cac module nguon cung luc
"$CC" $CFLAGS $INC \
    "$ROOT/src/shared_buffer.c" \
    "$ROOT/src/qnn_detector.c" \
    "$ROOT/src/camera_pipeline.c" \
    "$ROOT/src/object_tracker.c" \
    "$ROOT/src/web_server.c" \
    "$ROOT/src/main.c" \
    -o "$OUT/cam_yolo_web" \
    -ldl -lm -lpthread

echo "Bien dich thanh cong binary aarch64 -> $OUT/cam_yolo_web"
ls -la "$OUT"