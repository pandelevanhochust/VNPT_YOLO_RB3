#!/usr/bin/env python3
"""
compile_yolo_aihub.py — Tao QNN context binary (w8a8) cho QCS6490 tu mot ONNX,
dung Qualcomm AI Hub (quantize + compile) phoi hop voi thu muc calib_data.
"""
import argparse, glob, os, sys
import numpy as np

def load_calib(calib_dir, w, h, input_name):
    files = sorted(glob.glob(os.path.join(calib_dir, "*.rgb")))
    if not files:
        sys.exit(f"[ERR] khong tim thay *.rgb trong thu muc: {calib_dir}\nHay chay file prepare_calib.py truoc!")
    samples = []
    for f in files:
        raw = np.fromfile(f, dtype=np.uint8)
        if raw.size != w * h * 3:
            print(f"    bo qua {f} (size {raw.size} != {w*h*3})")
            continue
        # Chuyen tu HWC uint8 ve mang planar CHW float32 tu 0.0 -> 1.0 theo dung pipeline loader
        chw = np.transpose(raw.reshape(h, w, 3), (2, 0, 1)).astype(np.float32) / 255.0
        samples.append(chw[None, ...])  # Tao batch shape [1, 3, H, W]
    if not samples:
        sys.exit("[ERR] khong co mau calib hop le")
    print(f"[INFO] Quy trinh Nap calib: Da san sang {len(samples)} mau anh dang {samples[0].shape}")
    return {input_name: samples}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True, help="ONNX dau vao (co the dung external data)")
    # Mac dinh trỏ den thu muc calib_data do file prepare_calib.py tao ra
    ap.add_argument("--calib-dir", default="./calib_data", help="thu muc chua *.rgb dung de luong tu hoa")
    ap.add_argument("--device", default="Dragonwing RB3 Gen 2 Vision Kit")
    ap.add_argument("--input-name", default="images") # Match voi input chuoi anh cua YOLOv8
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=640)
    ap.add_argument("--out", default="../models/yolov8_w8a8.bin")
    ap.add_argument("--runtime", default="qnn_context_binary", help="qnn_context_binary | qnn_dlc")
    args = ap.parse_args()

    import onnx
    import qai_hub as hub

    # 1) Gop external data tranh loi thieu file khi upload len cloud
    single = args.onnx
    m = onnx.load(args.onnx, load_external_data=True)
    single = os.path.splitext(args.onnx)[0] + "_single.onnx"
    onnx.save_model(m, single, save_as_external_data=False)
    print(f"[INFO] Consolidated ONNX -> {single}")

    dev = hub.Device(args.device)
    calib = load_calib(args.calib_dir, args.width, args.height, args.input_name)

    model = hub.upload_model(single)
    print(f"[INFO] Uploaded model ID thành công: {model.model_id}")

    # 2) Giu nguyen toan bo cau hinh thuat toan luong tu hoa tinh INT8 (w8a8)
    print("[INFO] Submitting quantize job to Qualcomm cloud...")
    qjob = hub.submit_quantize_job(
        model, calibration_data=calib,
        weights_dtype=hub.QuantizeDtype.INT8,
        activations_dtype=hub.QuantizeDtype.INT8,
        name="yolo_w8a8",
    )
    print(f"[INFO] Quantize job link: {qjob.url}")
    qjob.wait()
    if not qjob.get_status().success:
        sys.exit(f"[ERR] Quantize FAILED: {qjob.get_status()}")
    qmodel = qjob.get_target_model()

    # 3) Compile ra file context binary chay hardware NPU
    print("[INFO] Submitting compile job for Qualcomm HTP Backend...")
    cjob = hub.submit_compile_job(
        qmodel, device=dev,
        name="yolo_w8a8_compile",
        options=f"--target_runtime {args.runtime} --quantize_io",
    )
    print(f"[INFO] Compile job link: {cjob.url}")
    cjob.wait()
    if not cjob.get_status().success:
        sys.exit(f"[ERR] Compile FAILED: {cjob.get_status()}")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    cjob.get_target_model().download(args.out)
    print(f"[OK] COMPLETE! Downloaded artifact to: {args.out}")

if __name__ == "__main__":
    main()