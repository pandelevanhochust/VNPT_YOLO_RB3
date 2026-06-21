#include "common.h"
#include <math.h>
#include <dlfcn.h>
#include "QnnInterface.h"
#include "System/QnnSystemInterface.h"

typedef Qnn_ErrorHandle_t (*GetIfaceFn)(const QnnInterface_t***, uint32_t*);
typedef Qnn_ErrorHandle_t (*GetSysFn)(const QnnSystemInterface_t***, uint32_t*);

static QnnInterface_t        g_qi;
static QnnSystemInterface_t  g_si;
static Qnn_BackendHandle_t   g_be = NULL;
static Qnn_DeviceHandle_t    g_dev = NULL;
static Qnn_ContextHandle_t   g_ctx = NULL;
static Qnn_GraphHandle_t     g_graph = NULL;
static QnnSystemContext_GraphInfoV1_t* g_gi = NULL;

static Qnn_Tensor_t* g_inputs  = NULL;
static Qnn_Tensor_t* g_outputs = NULL;
static void** g_outbufs = NULL;
static unsigned char* g_inbuf  = NULL;

static const char* COCO[80] = {
"person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light",
"fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
"elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
"skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle",
"wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange",
"broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed",
"dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven",
"toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"};

const char* class_name(int c){ return (c>=0&&c<80)? COCO[c] : "obj"; }
static uint32_t t_rank(const Qnn_Tensor_t* t){ return t->version==QNN_TENSOR_VERSION_2? t->v2.rank : t->v1.rank; }
static const uint32_t* t_dims(const Qnn_Tensor_t* t){ return t->version==QNN_TENSOR_VERSION_2? t->v2.dimensions : t->v1.dimensions; }
static Qnn_DataType_t t_dtype(const Qnn_Tensor_t* t){ return t->version==QNN_TENSOR_VERSION_2? t->v2.dataType : t->v1.dataType; }

static void t_set_rawbuf(Qnn_Tensor_t* t, void* d, uint32_t s){
    if(t->version==QNN_TENSOR_VERSION_2){ t->v2.memType=QNN_TENSORMEMTYPE_RAW; t->v2.clientBuf.data=d; t->v2.clientBuf.dataSize=s; }
    else { t->v1.memType=QNN_TENSORMEMTYPE_RAW; t->v1.clientBuf.data=d; t->v1.clientBuf.dataSize=s; }
}
static uint32_t t_numel(const Qnn_Tensor_t* t){ uint32_t n=1,r=t_rank(t); const uint32_t* d=t_dims(t); for(uint32_t i=0;i<r;i++) n*=d[i]; return n; }

static void t_scale_offset(const Qnn_Tensor_t* t, float* s, int32_t* o){
    const Qnn_QuantizeParams_t* q = t->version==QNN_TENSOR_VERSION_2? &t->v2.quantizeParams : &t->v1.quantizeParams;
    *s=1.0f; *o=0;
    if(q->quantizationEncoding==QNN_QUANTIZATION_ENCODING_SCALE_OFFSET){ *s=q->scaleOffsetEncoding.scale; *o=q->scaleOffsetEncoding.offset; }
    else if(q->quantizationEncoding==QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET){ *s=q->bwScaleOffsetEncoding.scale; *o=q->bwScaleOffsetEncoding.offset; }
}

// Nap thu vien va khoi tao QNN context tu file .bin
int qnn_init(const char* bin_path){
    void* htp=dlopen("libQnnHtp.so",RTLD_NOW|RTLD_GLOBAL);
    void* sys=dlopen("libQnnSystem.so",RTLD_NOW|RTLD_GLOBAL);
    if(!htp||!sys){ LOGE("dlopen QNN: %s", dlerror()); return -1; }
    GetIfaceFn gi=(GetIfaceFn)dlsym(htp,"QnnInterface_getProviders");
    GetSysFn   gs=(GetSysFn)dlsym(sys,"QnnSystemInterface_getProviders");
    if(!gi||!gs){ LOGE("dlsym getProviders"); return -1; }
    const QnnInterface_t** ip=NULL; uint32_t n=0; int ok=0;
    if(gi(&ip,&n)!=QNN_SUCCESS) return -1;
    for(uint32_t i=0;i<n;i++) if(ip[i]->apiVersion.coreApiVersion.major==QNN_API_VERSION_MAJOR){ g_qi=*ip[i]; ok=1; break; }
    if(!ok){ LOGE("no backend provider"); return -1; }
    const QnnSystemInterface_t** sp=NULL; uint32_t m=0; ok=0;
    if(gs(&sp,&m)!=QNN_SUCCESS) return -1;
    for(uint32_t i=0;i<m;i++) if(sp[i]->systemApiVersion.major==QNN_SYSTEM_API_VERSION_MAJOR){ g_si=*sp[i]; ok=1; break; }
    if(!ok){ LOGE("no system provider"); return -1; }

    if(g_qi.QNN_INTERFACE_VER_NAME.backendCreate(NULL,NULL,&g_be)!=QNN_SUCCESS){ LOGE("backendCreate"); return -1; }
    if(g_qi.QNN_INTERFACE_VER_NAME.deviceCreate) g_qi.QNN_INTERFACE_VER_NAME.deviceCreate(NULL,NULL,&g_dev);

    FILE* f=fopen(bin_path,"rb"); if(!f){ LOGE("open bin %s", bin_path); return -1; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* buf=malloc(sz);
    if(fread(buf,1,sz,f)!=(size_t)sz){ LOGE("read bin"); free(buf); fclose(f); return -1; } fclose(f);

    QnnSystemContext_Handle_t sc=NULL;
    g_si.QNN_SYSTEM_INTERFACE_VER_NAME.systemContextCreate(&sc);
    const QnnSystemContext_BinaryInfo_t* bi=NULL; Qnn_ContextBinarySize_t bisz=0;
    if(g_si.QNN_SYSTEM_INTERFACE_VER_NAME.systemContextGetBinaryInfo(sc,buf,sz,&bi,&bisz)!=QNN_SUCCESS){ LOGE("getBinaryInfo"); free(buf); return -1; }
    uint32_t ng=0; QnnSystemContext_GraphInfo_t* graphs=NULL;
    if(bi->version==QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1){ ng=bi->contextBinaryInfoV1.numGraphs; graphs=bi->contextBinaryInfoV1.graphs; }
    else if(bi->version==QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2){ ng=bi->contextBinaryInfoV2.numGraphs; graphs=bi->contextBinaryInfoV2.graphs; }
    else { ng=bi->contextBinaryInfoV3.numGraphs; graphs=bi->contextBinaryInfoV3.graphs; }
    if(ng<1){ LOGE("no graph in binary"); free(buf); return -1; }
    g_gi=&graphs[0].graphInfoV1;

    if(g_qi.QNN_INTERFACE_VER_NAME.contextCreateFromBinary(g_be,g_dev,NULL,buf,sz,&g_ctx,NULL)!=QNN_SUCCESS){ LOGE("contextCreateFromBinary"); free(buf); return -1; }
    free(buf);
    if(g_qi.QNN_INTERFACE_VER_NAME.graphRetrieve(g_ctx,g_gi->graphName,&g_graph)!=QNN_SUCCESS){ LOGE("graphRetrieve"); return -1; }
    LOGI("Graph '%s' inputs=%u outputs=%u", g_gi->graphName, g_gi->numGraphInputs, g_gi->numGraphOutputs);

    g_inputs=malloc(sizeof(Qnn_Tensor_t)*g_gi->numGraphInputs);
    g_outputs=malloc(sizeof(Qnn_Tensor_t)*g_gi->numGraphOutputs);
    g_outbufs=calloc(g_gi->numGraphOutputs,sizeof(void*));
    g_inbuf=malloc(t_numel(&g_gi->graphInputs[0]));
    for(uint32_t i=0; i<g_gi->numGraphInputs; i++){
        g_inputs[i]=g_gi->graphInputs[i];
        t_set_rawbuf(&g_inputs[i], g_inbuf, t_numel(&g_inputs[i]));
    }
    for(uint32_t i=0; i<g_gi->numGraphOutputs; i++){
        g_outputs[i]=g_gi->graphOutputs[i];
        uint32_t nb=t_numel(&g_outputs[i]);
        g_outbufs[i]=malloc(nb);
        t_set_rawbuf(&g_outputs[i], g_outbufs[i], nb);
    }
    return 0;
}

// Chay thuc thi model tren NPU
int qnn_execute(void){
    return g_qi.QNN_INTERFACE_VER_NAME.graphExecute(
        g_graph, g_inputs, g_gi->numGraphInputs, g_outputs, g_gi->numGraphOutputs, NULL, NULL)==QNN_SUCCESS ? 0 : -1;
}

// Tinh toán IoU giua hai box
static float box_iou(const Det* a,const Det* b){
    float x0=a->x>b->x?a->x:b->x, y0=a->y>b->y?a->y:b->y;
    float x1=a->r<b->r?a->r:b->r, y1=a->b<b->b?a->b:b->b;
    float iw=x1-x0, ih=y1-y0; if(iw<=0||ih<=0) return 0;
    float inter=iw*ih, aa=(a->r-a->x)*(a->b-a->y), bb=(b->r-b->x)*(b->b-b->y);
    float u=aa+bb-inter; return u>0?inter/u:0;
}

// Lọc box trung lap (NMS) - Da sua format thut le sua warning
static int nms(Det* d,int n,float thr){
    for(int i=0;i<n;i++){ 
        int bi=i; 
        for(int j=i+1;j<n;j++) if(d[j].score>d[bi].score) bi=j;
        if(bi!=i){ Det t=d[i]; d[i]=d[bi]; d[bi]=t; } 
    }
    int sup[MAX_DETS]; for(int i=0;i<n;i++) sup[i]=0;
    Det keep[MAX_DETS]; int k=0;
    for(int i=0;i<n;i++){ 
        if(sup[i]) continue; 
        keep[k++]=d[i];
        for(int j=i+1;j<n;j++) if(!sup[j]&&d[j].cls==d[i].cls&&box_iou(&d[i],&d[j])>thr) sup[j]=1; 
    }
    for(int i=0;i<k;i++) d[i]=keep[i];
    return k;
}

// Chuyen anh ve NCHW va luong tu hoa truoc khi nap vao NPU
void detector_preprocess(const unsigned char* rgb){
    float s; int32_t o; t_scale_offset(&g_inputs[0],&s,&o);
    const int HW = CAP_W*CAP_H;
    for(int y=0;y<CAP_H;y++){ 
        for(int x=0;x<CAP_W;x++){
            int p=(y*CAP_W+x);
            for(int c=0;c<3;c++){
                float real = rgb[p*3+c]/255.0f;
                int q = (int)(real/s + 0.5f) - o;
                if(q<0) q=0; 
                if(q>255) q=255;
                g_inbuf[c*HW + p] = (unsigned char)q;
            }
        }
    }
}

// Trích xuat output, giai luong tu va goi nms de lay ket qua detect cuoi
int detector_postprocess(Det* out,int maxd,float threshold){
    unsigned char *bx=NULL,*sc=NULL,*ci=NULL;
    float bx_s=1,sc_s=1; int bx_o=0,sc_o=0; int N=0;
    for(uint32_t i=0;i<g_gi->numGraphOutputs;i++){
        uint32_t r=t_rank(&g_outputs[i]); const uint32_t* d=t_dims(&g_outputs[i]);
        float s; int32_t o; t_scale_offset(&g_outputs[i],&s,&o);
        if(r==3 && d[r-1]==4){ bx=g_outbufs[i]; bx_s=s; bx_o=o; N=d[1]; }
        else if(r==2 && t_dtype(&g_outputs[i])==QNN_DATATYPE_UINT_8){ ci=g_outbufs[i]; }
        else if(r==2){ sc=g_outbufs[i]; sc_s=s; sc_o=o; if(!N)N=d[1]; }
    }
    if(!bx||!sc||!ci||N<=0) return 0;
    int nd=0;
    for(int i=0;i<N && nd<maxd;i++){
        float score=sc_s*((float)sc[i]+sc_o);
        if(score<threshold) continue;
        int cls=ci[i];
        out[nd].x = bx_s*((float)bx[i*4+0]+bx_o);
        out[nd].y = bx_s*((float)bx[i*4+1]+bx_o);
        out[nd].r = bx_s*((float)bx[i*4+2]+bx_o);
        out[nd].b = bx_s*((float)bx[i*4+3]+bx_o);
        out[nd].score=score; out[nd].cls=cls;
        nd++;
    }
    return nms(out,nd,0.45f);
}

// Ve khung chu nhat len ma tran anh RGB
void draw_rect(unsigned char* img,int w,int h,int x0,int y0,int x1,int y1,int thick){
    if(x0>x1){int t=x0;x0=x1;x1=t;} if(y0>y1){int t=y0;y0=y1;y1=t;}
    for(int t=0;t<thick;t++){
        for(int x=x0;x<=x1;x++){ 
            int a=y0+t,b=y1-t;
            if(x>=0&&x<w&&a>=0&&a<h){img[(a*w+x)*3]=0;img[(a*w+x)*3+1]=255;img[(a*w+x)*3+2]=0;}
            if(x>=0&&x<w&&b>=0&&b<h){img[(b*w+x)*3]=0;img[(b*w+x)*3+1]=255;img[(b*w+x)*3+2]=0;} 
        }
        for(int y=y0;y<=y1;y++){ 
            int a=x0+t,b=x1-t;
            if(y>=0&&y<h&&a>=0&&a<w){img[(y*w+a)*3]=0;img[(y*w+a)*3+1]=255;img[(y*w+a)*3+2]=0;}
            if(y>=0&&y<h&&b>=0&&b<w){img[(y*w+b)*3]=0;img[(y*w+b)*3+1]=255;img[(y*w+b)*3+2]=0;} 
        }
    }
}