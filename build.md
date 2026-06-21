# Hướng dẫn compile trước khi deployment

Trên board xác định sẵn trước SDK version của board:

```bash
cd /opt/qcom/qairt-latest/bin
snpe-net-run --version
```

Board em đang sử dụng có version v2.43.0.260128, do đó đó em sẽ phải QAIRT SDK đúng version khi compile

Tiếp theo do board không thể tự compile app nên phải sử dụng Cross-compiler, do đó em đã tải về toolchain tương ứng với board ở bước này:

```bash
cd /tmp
curl -LO https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/aarch64--glibc--stable-2021.11-1.tar.bz2
tar xf aarch64--glibc--stable-2021.11-1.tar.bz2
```

Trên cross-compiler:

# 1. Khai báo lại môi trường nếu cần

```bash
export TC=/tmp/aarch64--glibc--stable-2021.11-1
export SDK=path tới sdk

cd scripts
./tracking_build.sh

```

Hoặc chạy trực tiếp:

```bash
export TC=/tmp/aarch64--glibc--stable-2021.11-1
export SDK=path tới sdksdk

$TC/bin/aarch64-linux-gcc -O2 -std=gnu11 -Wall \
    -DSTB_IMAGE_WRITE_IMPLEMENTATION \
    -I$SDK/include/QNN -I./third_party -I./src \
 src/shared_buffer.c \
 src/qnn_detector.c \
 src/camera_pipeline.c \
 src/object_tracker.c \
 src/web_server.c \
 src/main.c \
 -o build/cam_yolo_web_tracking \
 -ldl -lm -lpthread
```

# 3. Kiểm tra thành quả

```
ls -la build/
```

Nếu đã có file build thành công thì scp và chuyển sang bước deploy lên board
