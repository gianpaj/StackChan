#include "oido_recognizer.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "tinyasr.h"
#include <algorithm>
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
bool nested = false, force_cancel = false;
bool is_cancelled() { return force_cancel || (cancel_at && ++checks >= cancel_at); }
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
    force_cancel = false;
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
    reset(); nested = true; assert(run() == ESP_OK);
    assert(output[0] && metrics.allocations == 59);
    assert(metrics.hot_used < workspace.hot_bytes && metrics.psram_used < workspace.psram_bytes);
    const std::string baseline = output;
    // The real scratch-only lease leaves this tail after copied Ito weights.
    workspace.psram_bytes = 1695168;
    reset();
    {
        oido_recognizer::Session session, blocked;
        assert(session.feed(pcm.data(), 1) == ESP_ERR_INVALID_STATE);
        assert(session.begin(workspace, callbacks) == ESP_OK);
        assert(mappings == 1 && tasr_alloc != alloc_hook);
        assert(session.begin(workspace) == ESP_ERR_INVALID_STATE);
        assert(blocked.begin(workspace) == ESP_ERR_INVALID_STATE);
        assert(blocked.close() == ESP_OK && mappings == 1);
        std::strcpy(output, "untouched");
        assert(session.finish(output, 32) == ESP_ERR_INVALID_SIZE);
        assert(!std::strcmp(output, "untouched"));
        assert(session.finish(static_cast<char*>(workspace.psram), sizeof output) == ESP_ERR_INVALID_ARG);
        assert(session.finish(output, sizeof output) == ESP_ERR_INVALID_STATE);
        assert(session.feed(nullptr, 1) == ESP_ERR_INVALID_ARG);
        assert(session.feed(pcm.data(), 0) == ESP_ERR_INVALID_ARG);
        assert(session.feed(pcm.data(), 321) == ESP_ERR_INVALID_ARG);
        assert(session.feed(static_cast<int16_t*>(workspace.psram), 320) == ESP_ERR_INVALID_ARG);
        int16_t chunk[320];
        for (size_t offset = 0; offset < pcm.size(); offset += 320) {
            const size_t count = std::min(size_t(320), pcm.size() - offset);
            std::memcpy(chunk, pcm.data() + offset, count * sizeof *chunk);
            assert(session.feed(chunk, count) == ESP_OK);
            // Consumer may reuse the producer's chunk as soon as feed returns.
            std::memset(chunk, 0xa5, sizeof chunk);
        }
        assert(session.finish(output, sizeof output) == ESP_OK);
        assert(baseline == output);
        assert(session.feed(pcm.data(), 1) == ESP_ERR_INVALID_STATE);
        assert(session.finish(output, sizeof output) == ESP_ERR_INVALID_STATE);
        assert(session.close(&metrics) == ESP_OK);
        assert(metrics.setup_us > 0 && metrics.feed_us > 0 && metrics.finish_us > 0);
        assert(metrics.psram_used <= workspace.psram_bytes && metrics.hot_used <= workspace.hot_bytes);
        const auto used = metrics.psram_used;
        assert(session.close(&metrics) == ESP_OK && metrics.psram_used == used);
        std::printf("streaming scratch: hot=%zu psram=%zu bytes\n", metrics.hot_used, metrics.psram_used);
        assert(mappings == 0 && tasr_alloc == alloc_hook && tasr_free == free_hook);
        assert(tasr_parallel == parallel_hook && tasr_par_mask == mask && tasr_wpf == wpf);
    }
    for (int chunk_frames : {4, 6}) {
        reset();
        oido_recognizer::Session session;
        for (int frames : {0, 1, 7, 16, -4})
            assert(session.begin(workspace, callbacks, frames) == ESP_ERR_INVALID_ARG && mappings == 0);
        assert(session.begin(workspace, callbacks, chunk_frames) == ESP_OK);
        for (size_t offset = 0; offset < pcm.size(); offset += 320)
            assert(session.feed(pcm.data() + offset, std::min(size_t(320), pcm.size() - offset)) == ESP_OK);
        assert(session.finish(output, sizeof output) == ESP_OK && output[0]);
        assert(session.close(&metrics) == ESP_OK);
        assert(metrics.psram_used <= workspace.psram_bytes && metrics.hot_used <= workspace.hot_bytes);
        std::printf("chunk%d scratch: hot=%zu psram=%zu bytes; chunk8_text_match=%s\n%s\n",
                    chunk_frames, metrics.hot_used, metrics.psram_used, baseline == output ? "yes" : "no", output);
        if (chunk_frames == 4) assert(baseline == output);
        else assert(!std::strcmp(output,
            "this is best done in the spring when disturbed plants can recover"));
    }
    // Cancellation during a live session cannot strand global hooks or mapping.
    reset();
    {
        oido_recognizer::Session session;
        assert(session.begin(workspace, callbacks) == ESP_OK);
        for (size_t offset = 0; offset < 25600; offset += 320)
            assert(session.feed(pcm.data() + offset, 320) == ESP_OK);
        force_cancel = true;
        assert(session.feed(pcm.data(), 320) == oido_recognizer::kCancelled);
        std::strcpy(output, "stale");
        assert(session.finish(output, sizeof output) == oido_recognizer::kCancelled && !output[0]);
        assert(session.close(&metrics) == oido_recognizer::kCancelled);
        assert(metrics.setup_us > 0 && metrics.feed_us > 0 && metrics.encoder_frames > 0);
    }
    reset();
    {
        oido_recognizer::Session session;
        assert(session.begin(workspace) == ESP_OK);
        int16_t silence[320]{};
        for (int index = 0; index < 500; ++index) assert(session.feed(silence, 320) == ESP_OK);
        assert(session.feed(silence, 1) == ESP_ERR_INVALID_ARG);
        // Scope exit must close an unfinished/capped session.
    }
    reset();
    {
        oido_recognizer::Session session;
        auto invalid = workspace;
        invalid.psram_bytes = 1280 * 1024 - 1;
        assert(session.begin(invalid) == ESP_ERR_NO_MEM && mappings == 0);
        invalid = workspace; invalid.hot_bytes = 224 * 1024 - 1;
        assert(session.begin(invalid) == ESP_ERR_NO_MEM && mappings == 0);
        invalid = workspace; invalid.psram = workspace.hot;
        assert(session.begin(invalid) == ESP_ERR_INVALID_ARG && mappings == 0);
        invalid = workspace; invalid.psram = static_cast<unsigned char*>(workspace.psram) + 1;
        assert(session.begin(invalid) == ESP_ERR_INVALID_ARG && mappings == 0);
        // Verify the admitted lower bound with the actual engine allocations.
        invalid = workspace; invalid.psram_bytes = 1280 * 1024;
        assert(session.begin(invalid) == ESP_OK);
        assert(session.close(&metrics) == ESP_OK && metrics.psram_used <= invalid.psram_bytes);
    }
    assert(mappings == 0 && tasr_alloc == alloc_hook && tasr_free == free_hook);
    assert(tasr_parallel == parallel_hook && tasr_par_mask == mask && tasr_wpf == wpf);
    assert(delays > 0);
    std::printf("adapter: batch/stream parity, scratch bounds, cancellation, cap and cleanup pass\n%s\n", baseline.c_str());
    std::free(workspace.hot); std::free(workspace.psram);
}
