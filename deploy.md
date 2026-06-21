# Hướng dẫn deployment lên board

Sau khi build thành công, ta có thể deploy lên board bằng các bước sau:

1. SCP các file sau lên board

```bash
IP_BOARD = [IP_ADDRESS]
DEV = root@$IP_BOARD
scp -r build $DEV:/opt/YOLO8_VNPT/
scp models/yolov8_int8.bin $DEV:/opt/YOLO8_VNPT/
scp -r scripts $DEV:/opt/YOLO8_VNPT/

```

2. Chạy

```bash
 # skel V68 cho DSP
cd /opt/YOLO8_VNPT/
./scripts/run_yolo_web.sh
```

Xem luồng stream trên
http://[IP_ADDRESS]:8080/
