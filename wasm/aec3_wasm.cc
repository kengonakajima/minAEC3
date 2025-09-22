// Minimal C API wrapper for EchoCanceller3 to compile with Emscripten.
// - 16kHz mono, 64-sample blocks only
// - Exposes create/destroy, mode set, analyze (render), process (capture)

#include <cstdint>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define KEEPALIVE
#endif

#include "all.h"

struct Aec3Handle {
  EchoCanceller3 aec;
  Block render_block;
  Block capture_block;
};

extern "C" {

KEEPALIVE void* aec3_create() {
  static_assert(kBlockSize == 64, "This wrapper assumes 64-sample blocks");
  auto* h = new Aec3Handle();
  return reinterpret_cast<void*>(h);
}

KEEPALIVE void aec3_destroy(void* handle) {
  if (!handle) return;
  delete reinterpret_cast<Aec3Handle*>(handle);
}

KEEPALIVE void aec3_set_modes(void* handle, int enable_linear, int enable_nonlinear) {
  if (!handle) return;
  auto* h = reinterpret_cast<Aec3Handle*>(handle);
  h->aec.SetProcessingModes(enable_linear != 0, enable_nonlinear != 0);
}

// Analyze a 64-sample render block (reference)
KEEPALIVE void aec3_analyze(void* handle, const int16_t* ref64) {
  if (!handle || !ref64) return;
  auto* h = reinterpret_cast<Aec3Handle*>(handle);
  h->render_block.CopyFromPcm16(ref64);
  h->aec.render_transfer_queue_.push_back(h->render_block);
  if (h->aec.render_transfer_queue_.size() > 100) {
    h->aec.render_transfer_queue_.pop_front();
  }
}

// Process a 64-sample capture block and write 64 samples to out64
KEEPALIVE void aec3_process(void* handle, const int16_t* cap64, int16_t* out64) {
  if (!handle || !cap64 || !out64) return;
  auto* h = reinterpret_cast<Aec3Handle*>(handle);
  h->capture_block.CopyFromPcm16(cap64);
  h->aec.ProcessBlock(&h->capture_block, nullptr);
  h->capture_block.CopyToPcm16(out64);
}

// Optional: expose the current estimated delay (in blocks), -1 if not available
KEEPALIVE int aec3_get_estimated_delay_blocks(void* handle) {
  if (!handle) return -1;
  auto* h = reinterpret_cast<Aec3Handle*>(handle);
  return h->aec.block_processor_.estimated_delay_blocks_;
}

}

