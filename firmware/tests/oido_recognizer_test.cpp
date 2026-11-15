#include "oido_recognizer.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "tinyasr.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int tasr_wpf;
namespace {
std::vector<unsigned char> blob;
esp_partition_t partition;
int mappings = 0, delays = 0, checks = 0, cancel_at = 0;
bool missing = false, map_failure = false, bad_digest = false;
int64_t now = 0;
itofs_workspace_t workspace;
std::vector<int16_t> pcm;
bool nested = false;
bool is_cancelled() { return cancel_at && ++checks >= cancel_at; }
void on_progress()
{
    ++delays;
    if (!nested) return;
    nested = false;
    char output[2048] = "untouched";
    oido_recognizer::Metrics metrics;
    metrics.hot_used = 123;
    assert(oido_recognizer::recognize(workspace, pcm.data(), pcm.size(), output,
                                    sizeof output, {}, &metrics) == ESP_ERR_INVALID_STATE);
    assert(!std::strcmp(output, "untouched") && metrics.hot_used == 123);
}
void reset()
{
    assert(mappings == 0);
    missing = map_failure = bad_digest = false;
    checks = cancel_at = 0;
    std::memset(workspace.hot, 0xa5, workspace.hot_bytes);
    std::memset(workspace.psram, 0xa5, workspace.psram_bytes);
}
}
int64_t esp_timer_get_time() { return now += 10000; }
void vTaskDelay(unsigned) { ++delays; }
const esp_partition_t* esp_partition_find_first(int, int, const char* label)
{ assert(!std::strcmp(label, "oido")); return missing ? nullptr : &partition; }
esp_err_t esp_partition_mmap(const esp_partition_t*, size_t offset, size_t bytes,
                            int, const void** pointer, unsigned* handle)
{
    assert(offset == 0 && bytes == blob.size());
    if (map_failure) return ESP_FAIL;
    *pointer = blob.data(); *handle = 1; ++mappings; return ESP_OK;
}
void esp_partition_munmap(unsigned handle) { assert(handle == 1 && mappings == 1); --mappings; }
void mbedtls_sha256_init(mbedtls_sha256_context* hash) { hash->bytes = 0; }
int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context* hash, const unsigned char*, size_t bytes)
{ hash->bytes += bytes; return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context* hash, unsigned char* output)
{
    assert(hash->bytes == blob.size());
    const unsigned char digest[] = {0x9e,0x8b,0x40,0x91,0x3f,0x53,0x2f,0xb4,
        0x4b,0x71,0x6a,0x1f,0x88,0x2f,0x38,0xb3,0xb2,0x1e,0x22,0x07,0x13,0xe4,
        0x17,0x89,0x9a,0xba,0x32,0xac,0xe8,0xc3,0x50,0x66};
    std::memcpy(output, digest, sizeof digest);
    if (bad_digest) output[0] ^= 1;
    return 0;
}
void mbedtls_sha256_free(mbedtls_sha256_context*) {}

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream model(argv[1], std::ios::binary), audio(argv[2], std::ios::binary);
    blob.assign(std::istreambuf_iterator<char>(model), {});
    std::vector<char> raw{std::istreambuf_iterator<char>(audio), {}};
    assert(blob.size() == 5931424 && raw.size() <= 320000);
    pcm.resize(raw.size() / 2);
    std::memcpy(pcm.data(), raw.data(), raw.size());
    partition.size = blob.size();
    workspace = {std::aligned_alloc(64, 239936), 239936,
                 std::aligned_alloc(64, 4891392), 4891392};
    assert(workspace.hot && workspace.psram);
    auto alloc_hook = tasr_alloc;
    auto free_hook = tasr_free;
    auto parallel_hook = tasr_parallel;
    const auto mask = tasr_par_mask;
    const auto wpf = tasr_wpf;
    char output[2048];
    oido_recognizer::Metrics metrics;
    const oido_recognizer::Callbacks callbacks{is_cancelled, on_progress};
    auto run = [&] {
        auto result = oido_recognizer::recognize(workspace, pcm.data(), pcm.size(),
                                               output, sizeof output, callbacks, &metrics);
        assert(mappings == 0 && tasr_alloc == alloc_hook && tasr_free == free_hook);
        assert(tasr_parallel == parallel_hook && tasr_par_mask == mask && tasr_wpf == wpf);
        if (result != ESP_OK) assert(output[0] == 0);
        return result;
    };
    std::strcpy(output, "untouched");
    metrics.hot_used = 123;
    assert(oido_recognizer::recognize(workspace, pcm.data(), pcm.size(), output,
                                    32, callbacks, &metrics) == ESP_ERR_INVALID_SIZE);
    assert(!std::strcmp(output, "untouched") && metrics.hot_used == 123);
    assert(oido_recognizer::recognize(workspace, pcm.data(), pcm.size(),
        static_cast<char*>(workspace.psram), 2048, callbacks) == ESP_ERR_INVALID_ARG);
    reset(); missing = true; assert(run() == ESP_ERR_NOT_FOUND);
    reset(); map_failure = true; assert(run() == ESP_FAIL);
    reset(); bad_digest = true; assert(run() == ESP_ERR_INVALID_CRC);
    reset(); cancel_at = 1; assert(run() == oido_recognizer::kCancelled);
    reset(); cancel_at = 3; assert(run() == oido_recognizer::kCancelled);
    reset(); cancel_at = 100; assert(run() == oido_recognizer::kCancelled);
    assert(metrics.allocations > 0);
    reset(); cancel_at = 130; assert(run() == oido_recognizer::kCancelled);
    assert(metrics.encoder_frames > 0);
    std::string baseline;
    for (int cycle = 0; cycle < 3; ++cycle) {
        reset(); nested = true; assert(run() == ESP_OK);
        assert(output[0] && metrics.allocations == 59);
        assert(metrics.hot_used < workspace.hot_bytes && metrics.psram_used < workspace.psram_bytes);
        if (cycle == 0) baseline = output;
        else assert(baseline == output);
    }
    assert(delays > 0);
    std::printf("adapter: cleanup, cancellation, reentry and 3 overwrite cycles pass\n%s\n", output);
    std::free(workspace.hot); std::free(workspace.psram);
}
