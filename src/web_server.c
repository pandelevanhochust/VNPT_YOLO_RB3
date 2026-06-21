#include "common.h"
#include <sys/socket.h>
#include <netinet/in.h>

static int send_all(int fd, const void* buf, size_t n) {
    const char* p = buf; 
    size_t s = 0;
    while (s < n) { 
        ssize_t w = send(fd, p + s, n - s, MSG_NOSIGNAL); 
        if (w <= 0) return -1; 
        s += w; 
    } 
    return 0;
}

static const char* HTML_PAGE =
"<!doctype html><html><head><meta charset=utf-8><title>RB3 NPU Detection</title>"
"<style>"
"  html,body{margin:0;padding:0;width:100%;height:100%;background:#111;color:#eee;"
"            font-family:sans-serif;overflow:hidden}"
"  /* Grid/Flex chinh ep view fit dung inside 100% cua cua so trinh duyet */"
"  #main-container{display:flex;flex-direction:column;width:100vw;height:100vh;"
"                  box-sizing:border-box;padding:12px 16px 16px 16px}"
"  h2{margin:0 0 8px 0;font-size:1.1rem;font-weight:normal;color:#aaa;flex-shrink:0}"
"  "
"  /* Vung wrap chiem het cho trong con lai nhung khong duoc phep tu phinh to ra */"
"  #wrap{flex:1;display:flex;flex-direction:column;justify-content:space-between;min-height:0}"
"  "
"  /* Khong che container chua video chi lay phan cho trong cua no, can giua anh */"
"  .video-container{flex:1;width:100%;min-height:0;display:flex;justify-content:center;align-items:center}"
"  "
"  /* Giai phap cot loi: Chuyen anh ve max-width va max-height 100% de fit khit vao container */"
"  img{border:2px solid #2d2;border-radius:6px;max-width:100%;max-height:100%;"
"      object-fit:contain;display:block}"
"  "
"  /* Thanh log co dinh duoi cung, khong bao gio bi anh chen lan */"
"  #side{background:#181818;padding:10px 14px;border-radius:6px;border:1px solid #222;"
"        display:flex;align-items:center;gap:20px;font-size:0.85rem;font-family:monospace;"
"        margin-top:12px;flex-shrink:0}"
"  #dets{color:#2d2;display:flex;flex-wrap:wrap;gap:10px}"
"  .d-item{background:#222;padding:2px 6px;border-radius:4px;border:1px solid #333}"
"  #fps{color:#2d2;font-weight:bold}"
"</style></head><body>"
"<div id=\"main-container\">"
"  <h2>Qualcomm Rb3 - Hello VNPT — NPU (HTP V68) — YOLOv8 Object Detection</h2>"
"  <div id=\"wrap\">"
"    <div class=\"video-container\"><img src=\"/stream\"></div>"
"    <div id=\"side\">"
"      <div>FPS: <span id=\"fps\">-</span></div>"
"      <div style=\"color:#888\">| Detections:</div>"
"      <div id=\"dets\"></div>"
"    </div>"
"  </div>"
"</div>"
"<script>"
"setInterval(async()=>{"
"  try{"
"    let r=await fetch('/dets');let j=await r.json();"
"    document.getElementById('fps').textContent=j.fps.toFixed(1);"
"    document.getElementById('dets').innerHTML=j.dets.map((d,i)=>"
"      `<span class=d-item>#${i+1} <b>${d.cls}</b>: ${(d.score*100).toFixed(0)}%</span>`).join('')||'<span style=\"color:#555\">none</span>';"
"  }catch(e){}"
"},400);"
"</script></body></html>";

static void handle_client(int cfd) {
    char req[2048]; 
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(cfd); return; }
    req[n] = 0;

    char method[8] = {0}, path[256] = {0};
    ssize_t scanned = sscanf(req, "%7s %255s", method, path);
    if (scanned < 2) { close(cfd); return; }

    if (strcmp(path, "/") == 0) {
        char hdr[256]; 
        int len = strlen(HTML_PAGE);
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", len);
        send_all(cfd, hdr, strlen(hdr)); 
        send_all(cfd, HTML_PAGE, len); 
        close(cfd); 
        return;
    }

    if (strcmp(path, "/dets") == 0) {
        char body[8192]; 
        int p = 0;
        pthread_mutex_lock(&g_lock);
        p += snprintf(body + p, sizeof(body) - p, "{\"fps\":%.2f,\"dets\":[", g_fps);
        for (int i = 0; i < g_ndet; i++) {
            p += snprintf(body + p, sizeof(body) - p,
                "%s{\"cls\":\"%s\",\"score\":%.3f,\"x\":%.1f,\"y\":%.1f,\"r\":%.1f,\"b\":%.1f}",
                i ? "," : "", class_name(g_dets[i].cls), g_dets[i].score, g_dets[i].x, g_dets[i].y, g_dets[i].r, g_dets[i].b);
        }
        p += snprintf(body + p, sizeof(body) - p, "]}");
        pthread_mutex_unlock(&g_lock);

        char hdr[256];
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", p);
        send_all(cfd, hdr, strlen(hdr)); 
        send_all(cfd, body, p); 
        close(cfd); 
        return;
    }

    if (strcmp(path, "/stream") == 0) {
        const char* hdr = "HTTP/1.1 200 OK\r\nConnection: close\r\n"
                          "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
        if (send_all(cfd, hdr, strlen(hdr)) != 0) { close(cfd); return; }
        
        uint64_t last = 0;
        while (g_run) {
            pthread_mutex_lock(&g_lock);
            while (g_run && g_frame_id == last) {
                pthread_cond_wait(&g_cond, &g_lock);
            }
            last = g_frame_id;
            
            size_t sz = g_jpeg_sz; 
            unsigned char* cpy = NULL;
            if (sz) { 
                cpy = malloc(sz); 
                memcpy(cpy, g_jpeg, sz); 
            }
            pthread_mutex_unlock(&g_lock);
            
            if (!cpy) continue;
            
            char part[128];
            int ph = snprintf(part, sizeof(part), "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", sz);
            if (send_all(cfd, part, ph) != 0 || send_all(cfd, cpy, sz) != 0 || send_all(cfd, "\r\n", 2) != 0) { 
                free(cpy); 
                break; 
            }
            free(cpy);
        }
        close(cfd); 
        return;
    }

    const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(cfd, nf, strlen(nf)); 
    close(cfd);
}

void* client_thread(void* a) { 
    int fd = (int)(intptr_t)a; 
    handle_client(fd); 
    return NULL; 
}