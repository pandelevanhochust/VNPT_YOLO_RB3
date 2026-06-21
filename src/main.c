#include "common.h"
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char** argv){
    // Kiem tra tham so dau vao (can it nhat file model .bin)
    if(argc < 2){ 
        printf("Usage: %s <model.bin> [port=8080]\n", argv[0]); 
        return 1; 
    }
    
    // Thiet lap cong port (mac dinh la 8080)
    int port = argc >= 3 ? atoi(argv[2]) : 8080;
    
    // Bo qua tin hieu SIGPIPE de tranh crash app khi user ngat ket noi web dot ngot
    signal(SIGPIPE, SIG_IGN);

    // Khoi tao NPU backend va nap model binary (.bin) cua Qualcomm
    if(qnn_init(argv[1]) != 0){ 
        LOGE("Khong khoi tao duoc QNN/DLC"); 
        return 1; 
    }

    // Tao luong ngam capture_thread de chay camera, NPU va Tracker liên tuc
    pthread_t cap; 
    pthread_create(&cap, NULL, capture_thread, NULL);

    // Khoi tao socket mang IPv4 thong qua giao thuc TCP (SOCK_STREAM)
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Cho phep tai su dung lai cong port ngay lap tuc ma khong can cho timeout
    int opt = 1; 
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Cau hinh dia chi IP (INADDR_ANY nhan moi card mang) va port cho server
    struct sockaddr_in addr = {0}; 
    addr.sin_family = AF_INET; 
    addr.sin_addr.s_addr = INADDR_ANY; 
    addr.sin_port = htons(port);
    
    // Gan cau hinh dia chi vao socket
    if(bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) != 0){ 
        LOGE("bind %d: %s", port, strerror(errno)); 
        close(sfd); 
        return 1; 
    }
    
    // Bat dau lang nghe ket noi mang voi hang cho toi da 8 client
    listen(sfd, 8);
    LOGI("==> Mo trinh duyet: http://<IP-thiet-bi>:%d/", port);

    // Vong lap chinh: Cho va chap nhan các ket noi tu trinh duyet web
    while(g_run){
        struct sockaddr_in ca; 
        socklen_t cl = sizeof(ca);
        
        // Block cho den khi co thiet bi truy cap vao web
        int cfd = accept(sfd, (struct sockaddr*)&ca, &cl);
        if(cfd < 0){ 
            if(errno == EINTR) continue; // Thử lai neu bi ngat boi system signal
            break; 
        }
        
        // Tao luong rieng cho moi client truy cap de server khong bi nghen
        pthread_t t; 
        pthread_create(&t, NULL, client_thread, (void*)(intptr_t)cfd); 
        pthread_detach(t); // Tu dong giai phong luong khi xử ly xong request HTML/Stream
    }
    
    // Dung he thong va thu hoi tai nguyen khi thoat vong lap
    g_run = 0; 
    pthread_join(cap, NULL); // Cho luong camera dung han
    close(sfd); // Dong socket cha
    return 0;
}