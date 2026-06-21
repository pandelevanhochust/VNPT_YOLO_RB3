# Download cocodataset for calibration

#!/usr/bin/env python3
import os
import shutil
import glob
import cv2
import numpy as np
from pathlib import Path
from ultralytics.utils.downloads import download

def main():
    # 1. Tai dataset COCO128 tu Ultralytics assets
    print("[1/3] Downloading COCO128 dataset...")
    url = 'https://github.com/ultralytics/assets/releases/download/v0.0.0/coco128.zip'
    download(url, dir=Path('.'))
    
    # Thư mục ảnh sau khi giải nén mặc định của Ultralytics
    img_dir = os.path.join("coco128", "images", "train2017")
    jpg_files = glob.glob(os.path.join(img_dir, "*.jpg"))
    if not jpg_files:
        raise FileNotFoundError(f"Khong tim thay anh .jpg tai {img_dir}")
        
    # 2. Tao thu muc dau ra calib_data
    out_dir = "./calib_data"
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    print(f"[2/3] Found {len(jpg_files)} images. Preparing target: '{out_dir}'...")

    # 3. Doc anh, chuyen doi dinh dang ve dung uint8 width*height*3 bytes
    W, H = 640, 640
    count = 0
    
    for img_path in jpg_files:
        # Doc anh dang BGR tiêu chuẩn
        img = cv2.imread(img_path)
        if img is None:
            continue
            
        # Chuyen đổi khong gian mau sang RGB
        img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        
        # Resize ve kich thuoc ma hoa tinh cho NPU (640x640)
        img_resized = cv2.resize(img_rgb, (W, H), interpolation=cv2.INTER_LINEAR)
        
        # Lay ten file cu va tao file .rgb moi
        base_name = os.path.splitext(os.path.basename(img_path))[0]
        out_path = os.path.join(out_dir, f"{base_name}.rgb")
        
        # Ghi truc tiep mang byte uint8 xuong dia thit
        img_resized.tofile(out_path)
        count += 1

    print(f"[3/3] Done! Transformed {count} images into binary static format inside '{out_dir}'.")

if __name__ == "__main__":
    main()