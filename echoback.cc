// Echoback (C++): 最小構成のローカル・エコーバック + AEC3
// 使い方:
//   ./echoback [--passthrough] [--no-linear] [--no-nonlinear] [--latency-ms=N] [--loopback-delay-ms=N]
//   体感用のモード:
//     --passthrough     : AEC無効（素通し）
//     --no-linear       : 線形フィルタ無効（非線形のみ）
//     --no-nonlinear    : 非線形抑圧無効（線形のみ）
//   ショートハンド:
//     --linear-only     : = --no-nonlinear
//     --nonlinear-only  : = --no-linear
// 前提:
//   - 16 kHz モノラル固定, 16-bit I/O (PortAudio デフォルトデバイス)
//   - AEC3 は 64 サンプルのブロック長
//   - 参照信号は直前に処理した出力ブロック（ループバック相当）

#include "all.h"

#include "portaudio.h"

struct State {
  int dev_sr = 16000;              // 16k固定
  int block_size = 64;             // ブロック長（PortAudio framesPerBuffer と一致）
  // 1ch 固定（PortAudioデバイス設定も1ch）
  int latency_ms = 0;               // ローカルエコー用ジッタバッファの目標遅延（ms）
  size_t latency_samples = 0;       // dev_sr*latency_ms/1000 で再計算される予定値
  int loopback_delay_ms = 150;      // 遠端へ送る経路に加える遅延（ms）
  size_t loopback_delay_samples = 0;

  // デバイスサンプリング周波数 (dev_sr) で動作するFIFO群
  std::deque<int16_t> rec_dev;     // マイクから収音したサンプル
  std::deque<int16_t> out_dev;     // スピーカーへ送るサンプル

  // AEC3 ドメイン = デバイスドメイン（同一サンプリング）。64 サンプル単位で処理
  std::deque<int16_t> ref_fifo;    // 直前に処理したブロック（ローカルエコー参照）
  std::deque<int16_t> loopback_delay_line; // 遠端に送るまで保持する遅延ライン

  // ジッタバッファ（デバイスドメイン）。ローカルエコーパスを蓄積しスピーカーへ混ぜる
  std::deque<int16_t> jitter;      // ループバック遅延を模擬するため処理済み音を蓄積
  bool need_jitter = true;

  // MatchedFilter による推定遅延の前回ログ値（ブロック数）。
  // -2: 未初期化（最初の非負値でログする） / -1: 未推定（ログしない）
  int last_logged_delay_blocks = -2;

  // --passthrough: AEC を行わず素通し再生
  bool passthrough = false;

  // 1秒平均キャンセル量（ERLE相当）の集計
  double erle_in_energy_accum = 0.0;
  double erle_out_energy_accum = 0.0;
  int erle_blocks_accum = 0;  // 250ブロック ≒ 1秒

  // AEC3 (EchoCanceller3 直結) — 単一インスタンス固定
  EchoCanceller3 aec;
  Block ref_block;
  Block cap_block;
};

static void aec3_init_at_sr(State& s){
  // 16k固定、64サンプル統一（値メンバはデフォルト構築でOK）
  s.block_size = 64;
}

static size_t pop_samples(std::deque<int16_t>& q, int16_t* dst, size_t n){
  size_t m = q.size()<n ? q.size() : n;
  for (size_t i=0;i<m;i++){ dst[i]=q.front(); q.pop_front(); }
  return m;
}

static void push_block(std::deque<int16_t>& q, const int16_t* src, size_t n){
  for (size_t i=0;i<n;i++) q.push_back(src[i]);
}

static void process_available_blocks(State& s){
  // 可能なかぎり多くの 64 サンプルブロックを処理する
  while (s.rec_dev.size() >= (size_t)s.block_size) {
    std::vector<int16_t> rec(s.block_size), ref(s.block_size), out(s.block_size);
    // 入力（rec）ブロックを取り出す
    pop_samples(s.rec_dev, rec.data(), s.block_size);
    // 参照（ref）ブロックを取り出す（不足時はゼロ埋め）
    if (s.ref_fifo.size() >= (size_t)s.block_size) {
      pop_samples(s.ref_fifo, ref.data(), s.block_size);
    } else {
      std::memset(ref.data(), 0, s.block_size*sizeof(int16_t));
    }
    if (s.passthrough) {
      // AEC を通さず、そのまま出力へ
      std::memcpy(out.data(), rec.data(), s.block_size * sizeof(int16_t));
    } else {
      // AEC3 直結: Render/ Capture を渡して処理
      s.cap_block.CopyFromPcm16(rec.data());
      s.ref_block.CopyFromPcm16(ref.data());
      s.aec.ProcessBlock(&s.cap_block, &s.ref_block);
      s.cap_block.CopyToPcm16(out.data());

      // MatchedFilter による推定遅延が変化したら1行だけ通知。
      // BlockProcessor は推定遅延（ブロック数）を public 成員に保持している。
      int cur = s.aec.block_processor_.estimated_delay_blocks_;
      if (cur >= 0 && cur != s.last_logged_delay_blocks) {
        int ms = cur * 1000 / kNumBlocksPerSecond; // 1ブロック=4ms
        std::fprintf(stderr, "[AEC3] 推定遅延が変化: %d ブロック (約 %d ms)\n", cur, ms);
        s.last_logged_delay_blocks = cur;
      }

      // 1秒に1回、キャンセル量（ERLE近似）を出力（dB）。
      {
        double ein = 0.0, eout = 0.0;
        for (int i = 0; i < s.block_size; ++i) {
          const double x = static_cast<double>(rec[i]);
          const double y = static_cast<double>(out[i]);
          ein += x * x;
          eout += y * y;
        }
        s.erle_in_energy_accum += ein;
        s.erle_out_energy_accum += eout;
        s.erle_blocks_accum += 1;
        if (s.erle_blocks_accum >= static_cast<int>(kNumBlocksPerSecond)) {
          constexpr double kEps = 1e-9;
          const double ratio = (s.erle_in_energy_accum + kEps) / (s.erle_out_energy_accum + kEps);
          const double erle_db = 10.0 * std::log10(ratio);
          std::fprintf(stderr, "[AEC3] 1秒平均キャンセル量: %.1f dB\n", erle_db);
          s.erle_in_energy_accum = 0.0;
          s.erle_out_energy_accum = 0.0;
          s.erle_blocks_accum = 0;
        }
      }
    }

    // echoback.js と同様に、次フレーム以降の参照として保存（処理後データを再利用）
    push_block(s.ref_fifo, out.data(), s.block_size);

    // ループバック遅延ラインを通して遠端へ送る信号を取得
    std::vector<int16_t> looped(s.block_size);
    push_block(s.loopback_delay_line, out.data(), s.block_size);
    size_t forwarded = pop_samples(s.loopback_delay_line, looped.data(), s.block_size);
    if (forwarded < static_cast<size_t>(s.block_size)) {
      std::memset(looped.data() + forwarded, 0,
                  (s.block_size - forwarded) * sizeof(int16_t));
    }

    // ネットワークの代わりにローカル蓄積へ積む（エコーバック）
    push_block(s.jitter, looped.data(), s.block_size);
    if (s.need_jitter && s.jitter.size() > s.latency_samples) {
      s.need_jitter = false; // jitter満了
    }

    // 受信（ローカル蓄積）を混ぜて再生用フレームを生成
    std::vector<int16_t> mixed(s.block_size);
    if (!s.need_jitter && s.jitter.size() >= (size_t)s.block_size) {
      pop_samples(s.jitter, mixed.data(), s.block_size);
    } else {
      std::memset(mixed.data(), 0, s.block_size*sizeof(int16_t));
    }
    push_block(s.out_dev, mixed.data(), s.block_size);
  }
}

static int pa_callback(const void* inputBuffer,
                       void* outputBuffer,
                       unsigned long blockSize,
                       const PaStreamCallbackTimeInfo* /*timeInfo*/,
                       PaStreamCallbackFlags /*statusFlags*/,
                       void* userData){
  State* st = reinterpret_cast<State*>(userData);
  const int16_t* in = reinterpret_cast<const int16_t*>(inputBuffer);
  int16_t* out = reinterpret_cast<int16_t*>(outputBuffer);
  const unsigned long n = blockSize; // モノラルのフレーム数（framesPerBuffer）
  // Single-threaded実行のため終了フラグは不要

  // 1) キャプチャデータをキューへ格納
  if (in) { for (unsigned long i=0;i<n;i++) st->rec_dev.push_back(in[i]); }

  // 2) 準備が整っている分だけ AEC3 を実行
  process_available_blocks(*st);

  // 3) 出力を生成
  for (unsigned long i=0;i<n;i++){
    if (!st->out_dev.empty()){ out[i]=st->out_dev.front(); st->out_dev.pop_front(); }
    else { out[i]=0; }
  }
  return paContinue;
}

int main(int argc, char** argv){
  State s;
  // 16k固定
  s.dev_sr = 16000; s.block_size = 64;

  // 引数パース
  bool no_linear = false;
  bool no_nonlinear = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i] ? argv[i] : "");
    if (arg == "--passthrough" || arg == "-p") {
      s.passthrough = true;
    } else if (arg == "--no-linear") {
      no_linear = true;
    } else if (arg == "--no-nonlinear") {
      no_nonlinear = true;
    } else if (arg == "--linear-only") {
      no_nonlinear = true; no_linear = false;
    } else if (arg == "--nonlinear-only") {
      no_linear = true; no_nonlinear = false;
    } else if (arg == "--help" || arg == "-h") {
      std::fprintf(stderr,
                   "Usage: %s [--passthrough] [--no-linear] [--no-nonlinear] [--latency-ms=N] [--loopback-delay-ms=N]\n",
                   argv[0]);
      return 0;
    } else if (arg == "--latency-ms") {
      if (i + 1 < argc) {
        ++i;
        const char* value = argv[i];
        char* endp = nullptr;
        long v = std::strtol(value, &endp, 10);
        if (endp && *endp == '\0' && v >= 0 && v < 10000) {
          s.latency_ms = static_cast<int>(v);
        } else {
          std::fprintf(stderr, "Invalid --latency-ms value: %s (ignored)\n", value);
        }
      } else {
        std::fprintf(stderr, "--latency-ms requires a value (ignored)\n");
      }
    } else if (arg.rfind("--latency-ms=", 0) == 0) {
      const char* value = arg.c_str() + std::strlen("--latency-ms=");
      char* endp = nullptr;
      long v = std::strtol(value, &endp, 10);
      if (endp && *endp == '\0' && v >= 0 && v < 10000) {
        s.latency_ms = static_cast<int>(v);
      } else {
        std::fprintf(stderr, "Invalid --latency-ms value: %s (ignored)\n", value);
      }
    } else if (arg == "--loopback-delay-ms") {
      if (i + 1 < argc) {
        ++i;
        const char* value = argv[i];
        char* endp = nullptr;
        long v = std::strtol(value, &endp, 10);
        if (endp && *endp == '\0' && v >= 0 && v < 10000) {
          s.loopback_delay_ms = static_cast<int>(v);
        } else {
          std::fprintf(stderr, "Invalid --loopback-delay-ms value: %s (ignored)\n", value);
        }
      } else {
        std::fprintf(stderr, "--loopback-delay-ms requires a value (ignored)\n");
      }
    } else if (arg.rfind("--loopback-delay-ms=", 0) == 0) {
      const char* value = arg.c_str() + std::strlen("--loopback-delay-ms=");
      char* endp = nullptr;
      long v = std::strtol(value, &endp, 10);
      if (endp && *endp == '\0' && v >= 0 && v < 10000) {
        s.loopback_delay_ms = static_cast<int>(v);
      } else {
        std::fprintf(stderr, "Invalid --loopback-delay-ms value: %s (ignored)\n", value);
      }
    } else {
      std::fprintf(stderr, "Unknown arg: %s (ignored)\n", arg.c_str());
    }
  }
  s.latency_samples = (size_t)((long long)s.dev_sr * s.latency_ms / 1000);
  s.loopback_delay_samples = (size_t)((long long)s.dev_sr * s.loopback_delay_ms / 1000);
  if (s.loopback_delay_samples > 0) {
    s.loopback_delay_line = std::deque<int16_t>(s.loopback_delay_samples, 0);
  }
  // AECモード設定（passthrough時は意味なし）
  if (!s.passthrough) {
    s.aec.SetProcessingModes(!no_linear, !no_nonlinear);
  }
  const char* mode = s.passthrough ? "passthrough" :
                     (no_linear && !no_nonlinear) ? "nonlinear-only" :
                     (!no_linear && no_nonlinear) ? "linear-only" :
                     "aec3";
  std::fprintf(stderr,
                "echoback (16k mono): mode=%s, latency_ms=%d (samples=%zu), loopback_delay_ms=%d (samples=%zu)\n",
                mode, s.latency_ms, s.latency_samples,
                s.loopback_delay_ms, s.loopback_delay_samples);
  aec3_init_at_sr(s);

  PaError err = Pa_Initialize();
  if (err!=paNoError){ std::fprintf(stderr, "Pa_Initialize error %s\n", Pa_GetErrorText(err)); return 1; }

  PaStream* stream=nullptr; PaStreamParameters inP{}, outP{};
  inP.device = Pa_GetDefaultInputDevice();
  outP.device = Pa_GetDefaultOutputDevice();
  if (inP.device<0 || outP.device<0){ std::fprintf(stderr, "No default device.\n"); Pa_Terminate(); return 1; }
  inP.channelCount = 1; inP.sampleFormat = paInt16;
  inP.suggestedLatency = Pa_GetDeviceInfo(inP.device)->defaultLowInputLatency;
  outP.channelCount = 1; outP.sampleFormat = paInt16;
  outP.suggestedLatency = Pa_GetDeviceInfo(outP.device)->defaultLowOutputLatency;

  err = Pa_OpenStream(&stream, &inP, &outP, s.dev_sr, s.block_size, paClipOff, pa_callback, &s);
  if (err!=paNoError){ std::fprintf(stderr, "Pa_OpenStream error %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
  err = Pa_StartStream(stream);
  if (err!=paNoError){ std::fprintf(stderr, "Pa_StartStream error %s\n", Pa_GetErrorText(err)); Pa_CloseStream(stream); Pa_Terminate(); return 1; }

  std::fprintf(stderr, "Running... Ctrl-C to stop.\n");
  while (Pa_IsStreamActive(stream)==1) {
    Pa_Sleep(100);
  }
  Pa_StopStream(stream); Pa_CloseStream(stream); Pa_Terminate();
  std::fprintf(stderr, "stopped.\n");
  return 0;
}
