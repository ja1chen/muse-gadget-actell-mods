/* SPDX-License-Identifier: Apache-2.0 */
/* The test inserts the actual patched SDK sender at ACTUAL_SENDER below. */
#include "actell_image_stream.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#define CONFIG_ACTELL_BRIDGE 1
#define CTRL_BODY_CHUNK_MAX 8192
#define WS_BUF_SIZE 16384
using noise_ctrl_session_generation_t = uint64_t;
struct esp_tls_t {};
struct ConstByteSpan { const uint8_t *p; size_t n; ConstByteSpan(const uint8_t *p,size_t n):p(p),n(n){} };
struct ByteSpan { ByteSpan(uint8_t *,size_t){} };
struct BodyChunkView { ConstByteSpan data{nullptr,0}; bool end_body=false; };
enum class ServiceType { Daemon };
static const int CTRL_STREAM_ID=1,OUT_SVC_SCRATCH=12288,OUT_ENV_SCRATCH=12288;
static bool dma=true,fail_send=false;
static bool noise_tx_has_dma_headroom(void *){return dma;}
static bool noise_tx_has_contiguous_dma_headroom(){return dma;}
static int64_t esp_timer_get_time(){static int64_t n=1;return n++;}
static const char *TAG="synthetic";
static void log_stub(const char *,const char *,...){ }
#define ESP_LOGE log_stub
struct Status {const char *str(){return "synthetic";}};
struct SendStatus {bool ok(){return true;} Status status(){return {};}};
struct ClientSession {
 std::vector<uint8_t> staged,wire;
 SendStatus StartOutboundBodyChunk(ServiceType,int,const BodyChunkView &chunk,ByteSpan,ByteSpan){
  staged.assign(chunk.data.p,chunk.data.p+chunk.data.n);return {};
 }
};
static bool flush_outbound(esp_tls_t *,ClientSession &s,uint8_t *){
 if(fail_send)return false;
 s.wire.insert(s.wire.end(),s.staged.begin(),s.staged.end());return true;
}
// ACTUAL_SENDER
static std::vector<uint8_t> load(const char *path){
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
 std::vector<uint8_t> bytes((size_t)n);assert(fread(bytes.data(),1,bytes.size(),f)==bytes.size());fclose(f);return bytes;
}
int main(int argc,char **argv){
 assert(argc==3);auto jpeg=load(argv[1]),expected=load(argv[2]);
 const char *metadata="{\"method\":\"link.result\",\"payload\":{\"format\":\"jpeg-base64\",\"data_base64\":\"\",\"width\":2}}";
 pending_json_body body;
 body.json=strdup(metadata);body.image.data=(unsigned char*)malloc(jpeg.size());assert(body.image.data);
 memcpy(body.image.data,jpeg.data(),jpeg.size());body.image.length=jpeg.size();
 assert(actell_image_layout_init(&body.image_layout,body.json,jpeg.size(),360*1024));
 body.json_len=body.image_layout.wire_length;
 ClientSession session;uint8_t ws[WS_BUF_SIZE],svc[OUT_SVC_SCRATCH],env[OUT_ENV_SCRATCH];esp_tls_t tls;
 dma=false;
 assert(send_json_body_chunk(&tls,session,body,svc,env,ws)==json_send_status::Pending);
 assert(!body.json_offset&&!body.prefix_sent&&session.wire.empty());dma=true;
 while(true){
  auto status=send_json_body_chunk(&tls,session,body,svc,env,ws);assert(status!=json_send_status::Failed);
  if(status==json_send_status::Complete)break;
  size_t offset=body.json_offset,wire_bytes=session.wire.size();dma=false;
  assert(send_json_body_chunk(&tls,session,body,svc,env,ws)==json_send_status::Pending);
  assert(body.json_offset==offset&&session.wire.size()==wire_bytes);dma=true;
 }
 assert(session.wire.size()==expected.size()+4);
 uint32_t length=0;for(unsigned i=0;i<4;i++)length|=(uint32_t)session.wire[i]<<(8*i);
 assert(length==expected.size());assert(std::equal(expected.begin(),expected.end(),session.wire.begin()+4));
 assert(body.chunks==(expected.size()+4+8191)/8192);
 clear_pending_json_body(body);assert(!body.json&&!body.image.data&&!body.json_len);
 /* Disconnect/failure must release both metadata and the retained JPEG. */
 body.json=strdup(metadata);body.image.data=(unsigned char*)malloc(jpeg.size());body.image.length=jpeg.size();
 memcpy(body.image.data,jpeg.data(),jpeg.size());assert(actell_image_layout_init(&body.image_layout,body.json,jpeg.size(),360*1024));
 body.json_len=body.image_layout.wire_length;fail_send=true;
 assert(send_json_body_chunk(&tls,session,body,svc,env,ws)==json_send_status::Failed);
 clear_pending_json_body(body);return 0;
}
