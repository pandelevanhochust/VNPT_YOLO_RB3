#include "common.h"

/* Khởi tạo vùng nhớ thực tế cho các biến hệ thống */
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
unsigned char* g_jpeg = NULL;
size_t g_jpeg_sz = 0;
uint64_t g_frame_id = 0;
Det g_dets[MAX_DETS];
int g_ndet = 0;
volatile int g_run = 1;
double g_fps = 0;