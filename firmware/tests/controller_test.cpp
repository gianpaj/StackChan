/* All turn-control bodies come from controller.cpp; only external boundaries are mocked. */
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_FINISHED 0x10c
#define ESP_ERROR_CHECK(value) assert((value)==ESP_OK)
#define EXT_RAM_BSS_ATTR
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#include "itofs_service.h"
#include "oido_recognizer.h"
#include "controller.h"
using TaskHandle_t = void*;
using StackType_t = uint32_t;
struct StaticTask_t { int unused; };
static int64_t now_us;
static int notifications, recognize_calls, resume_calls, return_calls, speak_calls;
static int session_begins, session_feeds, session_finishes, session_closes;
static int feed_error;
static bool fed_during_capture;
static int stop_error, resume_error, return_error, recognition_error;
static bool lease_owned, late_cancel, fail_allocation, fail_task;
static int heap_blocks, allocation_calls;
static bool capture_start_ok=true, capture_end_ok=true;
static int capture_starts, capture_ends, capture_reads;
static unsigned char hot_pool[256], psram_pool[512];
static std::string output;
static size_t heap_caps_get_free_size(int) { return 1000000; }
static size_t heap_caps_get_largest_free_block(int) { return 500000; }
static void* heap_caps_malloc(size_t size,int caps) {
    allocation_calls++;
    if (fail_allocation) return nullptr;
    if (size==12288) assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    void* p=std::malloc(size);assert(p);heap_blocks++;return p;
}
static void heap_caps_free(void* p) { if (p) { heap_blocks--;std::free(p); } }
static int64_t esp_timer_get_time() { return now_us; }
static int esp_task_wdt_reset() { return ESP_OK; }
static int esp_task_wdt_add(void*) { return ESP_OK; }
static void vTaskDelay(int) { now_us+=1000; local_conversation::poll(); }
static int ulTaskNotifyTake(int,int) { return 0; }
static void xTaskNotifyGive(TaskHandle_t) { notifications++; }
static unsigned uxTaskGetStackHighWaterMark(void*) { return 1000; }
static TaskHandle_t xTaskCreateStaticPinnedToCore(void(*)(void*),const char*,size_t bytes,void*,int,
                                                StackType_t* storage,StaticTask_t* control,int) {
    assert(bytes==12288 && storage && control);
    return fail_task ? nullptr : reinterpret_cast<void*>(1);
}
static const char* esp_err_to_name(int) { return "mock error"; }
namespace local_ito_board {
static bool start_capture() { capture_starts++;return capture_start_ok; }
static bool read_capture_mono(int16_t* pcm,size_t n) { capture_reads++;std::memset(pcm,0,n*2);return true; }
static bool end_capture() { capture_ends++;return capture_end_ok; }
}
extern "C" esp_err_t itofs_service_stop(uint32_t) { return stop_error; }
extern "C" esp_err_t itofs_service_borrow_scratch(itofs_workspace_t* workspace) {
    assert(!lease_owned);lease_owned=true;
    *workspace={hot_pool,sizeof hot_pool,psram_pool,sizeof psram_pool};return ESP_OK;
}
extern "C" esp_err_t itofs_service_return_memory(const itofs_workspace_t* workspace) {
    assert(lease_owned && workspace->hot==hot_pool && workspace->psram==psram_pool);
    return_calls++;
    if (!return_error) lease_owned=false;
    return return_error;
}
extern "C" esp_err_t itofs_service_resume() { assert(!lease_owned);resume_calls++;return resume_error; }
extern "C" esp_err_t itofs_service_command(const char* command) {
    assert(!lease_owned && std::strncmp(command,"text ",5)==0);speak_calls++;return ESP_OK;
}
namespace oido_recognizer {
Session::~Session() { }
esp_err_t Session::begin(const itofs_workspace_t&, const Callbacks&) {
    assert(lease_owned);owns_run_=true;result_=ESP_OK;session_begins++;return result_;
}
esp_err_t Session::feed(const int16_t*,size_t count) {
    assert(lease_owned && count && count<=320);session_feeds++;
    fed_during_capture |= local_conversation::poll();
    return feed_error;
}
esp_err_t Session::finish(char* text,size_t bytes) {
    assert(lease_owned && bytes>=6);session_finishes++;std::strcpy(text,"hello");return recognition_error;
}
esp_err_t Session::close(Metrics*) { owns_run_=false;session_closes++;return result_; }
esp_err_t recognize(const itofs_workspace_t&,const int16_t*,size_t,char* text,size_t bytes,const Callbacks& callbacks,Metrics*) {
    assert(lease_owned && bytes>=6);recognize_calls++;
    if (callbacks.cancelled && callbacks.cancelled()) return kCancelled;
    std::strcpy(text,"hello");return recognition_error;
}
}
static int capture_printf(const char* format,...) {
    if (late_cancel && std::strncmp(format,"TRANSCRIPT",10)==0) {
        late_cancel=false;
        assert(local_conversation::command("cancel"));
    }
    char buffer[4096];va_list arguments;va_start(arguments,format);
    const int n=std::vsnprintf(buffer,sizeof buffer,format,arguments);va_end(arguments);
    assert(n>=0 && size_t(n)<sizeof buffer);output+=buffer;return n;
}
#define printf capture_printf
#include "production_controller.inc"
#undef printf
static void reset_case() {
    using namespace local_conversation;
    phase.store(Phase::idle);cancelled=false;submitted=false;
    transcript[0]=0;samples=limit=0;microphone=false;
    lease_owned=late_cancel=false;
    notifications=recognize_calls=resume_calls=return_calls=speak_calls=0;
    session_begins=session_feeds=session_finishes=session_closes=0;feed_error=0;fed_during_capture=false;
    capture_start_ok=capture_end_ok=true;capture_starts=capture_ends=capture_reads=0;
    stop_error=resume_error=return_error=recognition_error=0;output.clear();now_us=0;
}
static void complete_upload() {
    assert(local_conversation::command("pcm begin 2"));
    assert(local_conversation::command("pcm data 0100ffff"));
    assert(local_conversation::command("send"));
}
int main() {
    using namespace local_conversation;
    assert(!start() && !pcm && !worker && heap_blocks==0);
    fail_allocation=true;assert(!reserve_worker_stack() && !worker_storage && !heap_blocks);
    fail_allocation=false;assert(reserve_worker_stack() && heap_blocks==1);
    const int calls=allocation_calls;auto* reserved=worker_storage;
    assert(reserve_worker_stack() && worker_storage==reserved && allocation_calls==calls);
    fail_task=true;assert(!start() && !pcm && !worker && heap_blocks==1);
    fail_task=false;assert(start() && worker_storage==reserved && heap_blocks==2);
    std::puts("PASS missing/reservation/task failures preserve the early stack and clean PCM; reservation is idempotent");
    reset_case();
    for (const char* invalid : {"pcm begin 0","pcm begin 160001","pcm begin -1","listen 0","listen 11"}) {
        assert(command(invalid));assert(phase.load()==Phase::idle && !notifications);
    }
    assert(command("pcm begin 2"));assert(phase.load()==Phase::uploading);
    for (const char* invalid : {"pcm data 000","pcm data 000x","pcm data 000000000000"}) {
        assert(command(invalid));assert(samples==0 && !notifications);
    }
    assert(command("send"));assert(phase.load()==Phase::uploading && !notifications);
    assert(command("pcm data 0100ffff"));assert(samples==2 && pcm[0]==1 && pcm[1]==-1);
    assert(command("send"));assert(notifications==1 && phase.load()==Phase::preparing);
    assert(command("cancel"));run_turn();
    assert(!recognize_calls && return_calls==1 && resume_calls==1 && !lease_owned);
    assert(phase.load()==Phase::idle && !transcript[0]);
    assert(output.find("TRANSCRIPT")==std::string::npos);
    std::puts("PASS actual controller rejects malformed/bounded PCM and cancels before worker recognition");

    reset_case();complete_upload();late_cancel=true;run_turn();
    assert(cancelled && phase.load()==Phase::idle && !lease_owned);
    assert(command("echo"));assert(!speak_calls);
    std::puts("PASS cancel during completion cannot be replayed by echo");

    reset_case();complete_upload();resume_error=ESP_FAIL;run_turn();
    assert(return_calls==1 && resume_calls==1 && !lease_owned && phase.load()==Phase::fault);
    assert(!transcript[0] && output.find("CONVERSATION READY")==std::string::npos);
    assert(command("pcm begin 1"));assert(notifications==1 && phase.load()==Phase::fault);
    std::puts("PASS restore failure closes admission without a false READY");

    reset_case();complete_upload();return_error=ESP_FAIL;run_turn();
    assert(lease_owned && return_calls==1 && !resume_calls && phase.load()==Phase::fault);
    std::puts("PASS failed lease return prevents resume and preserves ownership");

    reset_case();complete_upload();recognition_error=ESP_FAIL;run_turn();
    assert(!lease_owned && return_calls==1 && resume_calls==1 && phase.load()==Phase::idle && !transcript[0]);
    std::puts("PASS recognizer failure restores Ito before admitting another turn");
    reset_case();capture_start_ok=false;assert(command("listen 1"));run_turn();
    assert(capture_starts==1 && capture_ends==1 && !capture_reads && !recognize_calls);
    assert(return_calls==1 && resume_calls==1 && !lease_owned && phase.load()==Phase::idle);
    assert(!transcript[0] && output.find("ERROR recognition")!=std::string::npos);
    assert(output.find("CONVERSATION READY")!=std::string::npos);
    std::puts("PASS failed RX setup with verified cleanup restores Ito and idle error recovery");

    reset_case();capture_start_ok=capture_end_ok=false;assert(command("listen 1"));run_turn();
    assert(capture_starts==1 && capture_ends==1 && !capture_reads && !recognize_calls);
    assert(return_calls==1 && !resume_calls && !lease_owned && phase.load()==Phase::fault);
    assert(!transcript[0] && output.find("ERROR conversation recovery")!=std::string::npos);
    assert(output.find("CONVERSATION READY")==std::string::npos);
    std::puts("PASS failed RX cleanup prevents Ito resume and enters fault without READY");

    reset_case();assert(command("listen 1"));run_turn();
    assert(session_begins==1 && session_feeds==50 && session_finishes==1 && session_closes==1);
    assert(fed_during_capture && capture_reads==50 && capture_ends==1);
    assert(return_calls==1 && resume_calls==1 && !lease_owned && phase.load()==Phase::idle);
    assert(published==16000 && consumed==16000 && capture_done);
    assert(command("echo"));assert(speak_calls==1);
    std::puts("PASS microphone producer and streaming consumer finish before restoring Ito and echo");

    reset_case();feed_error=ESP_FAIL;assert(command("listen 1"));run_turn();
    assert(session_feeds==1 && !session_finishes && session_closes==1 && capture_done);
    assert(capture_ends==1 && return_calls==1 && resume_calls==1 && !lease_owned);
    assert(!transcript[0] && phase.load()==Phase::idle);
    std::puts("PASS failed streaming feed joins capture and closes the session before restoring Ito");

    heap_caps_free(pcm);pcm=nullptr;heap_caps_free(worker_storage);worker_storage=nullptr;assert(!heap_blocks);
    return 0;
}
