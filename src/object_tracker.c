#include "common.h"

// Mang luu tru trang thai cua cac doi tuong dang duoc tracking
static TrackedTarget g_targets[MAX_TRACKED_TARGETS] = {0};
static int g_next_id = 1; // Bien tang tu dong de cap Tracking ID duy nhat

// Ham tinh toán ty le dien tich trung lap IOU giua 2 bounding box
static float compute_iou(const Det* a, const Det* b) {
    if (a->cls != b->cls) return 0.0f; // Chi track neu cung loai doi tuong
    
    // Tim toa do vung giao nhau (Intersection)
    float x0 = a->x > b->x ? a->x : b->x;
    float y0 = a->y > b->y ? a->y : b->y;
    float x1 = a->r < b->r ? a->r : b->r;
    float y1 = a->b < b->b ? a->b : b->b;
    
    float iw = x1 - x0;
    float ih = y1 - y0;
    if (iw <= 0 || ih <= 0) return 0.0f; // Khong co vung trung nhau
    
    // Tinh dien tich vung giao va vung hop (Union)
    float inter_area = iw * ih;
    float area_a = (a->r - a->x) * (a->b - a->y);
    float area_b = (b->r - b->x) * (b->b - b->y);
    float union_area = area_a + area_b - inter_area;
    
    return union_area > 0 ? (inter_area / union_area) : 0.0f;
}

// Ham cap nhat thuat toan tracking tu ket qua detect moi cua NPU
int update_tracker(const Det* new_dets, int num_dets, Det* tracked_outputs) {
    int matched_dets[MAX_DETS] = {0};
    int matched_targets[MAX_TRACKED_TARGETS] = {0};

    // 1. Ghep cap giua target cu dang active voi box moi qua IOU cao nhat
    for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
        if (!g_targets[i].active) continue;

        int best_det_idx = -1;
        float best_iou = 0.30f; // Nguong IOU toi thieu de nhan dang ghep cap

        for (int j = 0; j < num_dets; j++) {
            if (matched_dets[j]) continue; // Box nay da duoc ghep cap truoc do
            
            float iou = compute_iou(&g_targets[i].bbox, &new_dets[j]);
            if (iou > best_iou) {
                best_iou = iou;
                best_det_idx = j;
            }
        }

        // 2. Neu tim duoc mat matching phu hop, cap nhat lai toa do moi
        if (best_det_idx != -1) {
            g_targets[i].bbox = new_dets[best_det_idx];
            g_targets[i].missing_count = 0; 
            g_targets[i].age++;
            matched_dets[best_det_idx] = 1;
            matched_targets[i] = 1;
        }
    }

    // 3. Xu ly cac target cu khong tim thay box moi o frame nay
    for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
        if (!g_targets[i].active) continue;
        
        if (!matched_targets[i]) {
            g_targets[i].missing_count++; // Tang bien dem mat dau
            if (g_targets[i].missing_count > MAX_AGE_FRAMES) {
                g_targets[i].active = 0; // Xoa doi tuong neu mat dau qua lau
            }
        }
    }

    // 4. Dang ky cac box detect moi hoan toan vao o nho trong
    for (int j = 0; j < num_dets; j++) {
        if (matched_dets[j]) continue;

        // Tim o nho dang trong trong mang g_targets
        for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
            if (!g_targets[i].active) {
                g_targets[i].bbox = new_dets[j];
                g_targets[i].id = g_next_id++; // Cap ID moi duy nhat
                g_targets[i].age = 1;
                g_targets[i].missing_count = 0;
                g_targets[i].active = 1;
                break;
            }
        }
    }

    // 5. Day toan bo danh sach doi tuong dang active ra bo dem output de ve len web
    int output_count = 0;
    for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
        // Chi lay các doi tuong dang active va khong bi mat dau o frame hien tai
        if (g_targets[i].active && g_targets[i].missing_count == 0) {
            tracked_outputs[output_count] = g_targets[i].bbox;
            tracked_outputs[output_count].cls = g_targets[i].bbox.cls; 
            
            output_count++;
        }
    }

    return output_count;
}