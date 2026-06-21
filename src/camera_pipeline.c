#include "common.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include "stb_image_write.h"

// Struct quan ly bo dem dung de ghi file anh vao RAM
typedef struct { unsigned char* d; size_t n, cap; } membuf;

// Ham callback ho tro ghi du lieu vao bo dem RAM
static void mem_w(void* ctx, void* p, int n){ 
    membuf* m = ctx;
    if(m->n + n > m->cap){ 
        m->cap = (m->n + n) * 2 + 1024; 
        m->d = realloc(m->d, m->cap);
    } 
    memcpy(m->d + m->n, p, n); 
    m->n += n; 
}

// Ham lay thoi gian hien tai theo miligiay
static double now_ms(void){ 
    struct timespec ts; 
    clock_gettime(CLOCK_MONOTONIC, &ts); 
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; 
}

// Kich khong pipeline GStreamer camera va tao ong FIFO pipe
static int start_camera(void){
    unlink(FIFO_PATH);
    if(mkfifo(FIFO_PATH, 0666) != 0 && errno != EEXIST){ 
        LOGE("mkfifo: %s", strerror(errno)); 
        return -1; 
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "gst-launch-1.0 -q qtiqmmfsrc camera=0 ! "
        "video/x-raw,width=1280,height=720,format=NV12 ! "
        "videoconvert ! videoscale ! video/x-raw,format=RGB,width=%d,height=%d ! "
        "filesink location=%s buffer-mode=2 > /tmp/gst_cam.log 2>&1 &",
        CAP_W, CAP_H, FIFO_PATH);
    LOGI("Khoi dong camera: %s", cmd);
    if(system(cmd) != 0){ LOGE("system(gst) loi"); }
    
    int fd = open(FIFO_PATH, O_RDONLY); // Block cho den khi camera bat dau ghi
    if(fd < 0){ LOGE("open fifo: %s", strerror(errno)); return -1; }
    return fd;
}

// Doc du so luong byte cua 1 frame anh, tranh mat mat du lieu socket
static int read_full(int fd, unsigned char* buf, size_t n){
    size_t got = 0;
    while(got < n){ 
        ssize_t r = read(fd, buf + got, n - got);
        if(r < 0){ if(errno == EINTR) continue; return -1; }
        if(r == 0) return -1;
        got += r; 
    }
    return 0;
}

// Luong xu ly chinh: Doc camera -> Chay NPU -> Tracking -> Day Web
void* capture_thread(void* arg){
    (void)arg;
    int fd = start_camera();
    if(fd < 0){ g_run = 0; return NULL; }
    
    unsigned char* rgb = malloc(CAP_W * CAP_H * 3);
    Det dets[MAX_DETS];
    Det tracked_dets[MAX_DETS];
    
    double last = now_ms(); 
    int fcount = 0;
    
    while(g_run){
        // 1. Doc du 1 frame RGB tu camera (GStreamer push vao bể chứa FIFO liên tục)
        if(read_full(fd, rgb, CAP_W * CAP_H * 3) != 0){ 
            LOGE("doc camera that bai/EOF"); 
            break; 
        }
        
        // 2. Tien xu ly anh ve dang planar NCHW va luong tu hoa
        detector_preprocess(rgb);
        
        // 3. Chay model YOLOv8 tang toc tren Hexagon NPU (Song song tren DSP cứng)
        if(qnn_execute() != 0){ 
            LOGE("graphExecute loi"); 
            continue; 
        }
        
        // 4. Hau xu ly va chay NMS de loc box trung lap
        int nd = detector_postprocess(dets, MAX_DETS, 0.40f);
        
        // 5. Thuc hien cap nhat dinh danh thong qua IOU Tracker
        int num_tracked = update_tracker(dets, nd, tracked_dets);
        
        // 6. Ve hop thong tin doi tuong da duoc tracking len anh
        for(int i = 0; i < num_tracked; i++) {
            draw_rect(rgb, CAP_W, CAP_H, (int)tracked_dets[i].x, (int)tracked_dets[i].y, (int)tracked_dets[i].r, (int)tracked_dets[i].b, 2);
        }
        
        // 7. Nen anh RGB sang JPEG vao RAM (Ha chat luong xuong 50 de giai phong CPU nhanh hon)
        membuf mb = {0}; 
        stbi_write_jpg_to_func(mem_w, &mb, CAP_W, CAP_H, 3, rgb, 50); // Ha tu 80 xuong 50 de cuu FPS
        
        // 8. Tinh toan thong so FPS theo tan suat tiep nhan pipeline cua luong
        fcount++; 
        double t = now_ms(); 
        if(t - last >= 1000.0){ 
            g_fps = fcount * 1000.0 / (t - last); 
            fcount = 0; 
            last = t; 
        }
        
        // 9. Dong bo vung nho chung (Lock cực ngắn, chi copy con tro va nhan tinh hieu)
        pthread_mutex_lock(&g_lock);
        free(g_jpeg); 
        g_jpeg = mb.d; 
        g_jpeg_sz = mb.n;
        
        // Copy nhanh mang box da qua tracking
        memcpy(g_dets, tracked_dets, sizeof(Det) * num_tracked); 
        g_ndet = num_tracked;
        
        g_frame_id++;
        pthread_cond_broadcast(&g_cond);
        pthread_mutex_unlock(&g_lock);
    }
    
    // Giai phong tai nguyen khi dung luong
    free(rgb); 
    close(fd);
    g_run = 0;
    return NULL;
}