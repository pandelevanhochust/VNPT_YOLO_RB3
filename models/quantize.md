# Huớng dẫn quantize model YOLOv8

Quá trình quantize được thực hiện thông qua Qualcomm AI Hub.

## 1. Cài đặt thư viện

```bash
pip install -r requirements.txt

#Cấu hình AI Hub
qai-hub configure --api_token <API_TOKEN>

```

## 2. Chạy các file sau theo thứ tự

```bash
#Tải model xuống (ở đây lấy qua ultraulytics, có thể lấy trên Qualcomm AI Hub)
# Convert model ra dạng onnx
python 1_export_yolo_v8_onnx.py

#Lấy dataset cho calibration của yolo
python 2_download_coco_dataset.py

#Tiến hành quantize model yolo sang INT8
python 3_quantization_aihub.py --onnx yolov8n.onnx

#Quantize xong sẽ có model yolov8_int8.bin
```
