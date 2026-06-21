#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

/* Cấu hình hệ thống */
#define CAP_W    640
#define CAP_H    640
#define MAX_DETS 256
#define JPEG_QUALITY 70
#define FIFO_PATH "/tmp/cam_fifo"

#define LOGI(...) do { fprintf(stdout,"[INFO] " __VA_ARGS__); fprintf(stdout,"\n"); fflush(stdout);} while(0)
#define LOGE(...) do { fprintf(stderr,"[ERR ] " __VA_ARGS__); fprintf(stderr,"\n"); } while(0)

/* Kiểu dữ liệu Bounding Box (ĐÃ ĐƯỢC ĐƯA LÊN TRƯỚC) */
typedef struct { float x, y, r, b, score; int cls; } Det;

/* Cấu trúc dữ liệu Tracking */
#define MAX_TRACKED_TARGETS 64
#define MAX_AGE_FRAMES 10

typedef struct {
    Det bbox;          
    int id;            
    int age;           
    int missing_count; 
    int active;        
} TrackedTarget;

/* Biến toàn cục dùng chung giữa các luồng */
extern pthread_mutex_t g_lock;
extern pthread_cond_t  g_cond;
extern unsigned char* g_jpeg;
extern size_t g_jpeg_sz;
extern uint64_t g_frame_id;
extern Det g_dets[MAX_DETS];
extern int g_ndet;
extern volatile int g_run;
extern double g_fps;

/* Các hàm xuất khẩu từ các module */
int qnn_init(const char* bin_path);
int qnn_execute(void);
void detector_preprocess(const unsigned char* rgb);
int detector_postprocess(Det* out, int maxd, float threshold);
void draw_rect(unsigned char* img, int w, int h, int x0, int y0, int x1, int y1, int thick);
const char* class_name(int c);

int update_tracker(const Det* new_dets, int num_dets, Det* tracked_outputs);
void* capture_thread(void* arg);
void* client_thread(void* a);

#endif