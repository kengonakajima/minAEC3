# minAEC : Minimal AEC3 for Education

WebRTC の Acoustic Echo Canceller 3 (AEC3) を教育目的に最小化したものです。
アルゴリズムの理解・解説に焦点を当て、移植性・可搬性・多機能性は排除しています。
最小化の作業は、codex (GPT-5 High) が行いました。
AECの実態はC++で実装されていますが、WASM経由で、Node.jsでも実行可能です。

minAECでは、以下を前提に、AEC3のコードを大幅に簡素化しています。

- 対応プラットフォーム: macOS のみ
- サンプリング周波数: 16 kHz 固定
- チャネル数: モノラル固定
- ノイズサプレッション,VAD,AGCを削除
- 速度最適化,高音質,多バンド処理を削除
- 拡張性, 柔軟性, 信頼性を削減

## 不要なこと

本プロジェクトの目的はエコーキャンセルアルゴリズムの実装をするためなので、
できるだけ、必要不可欠なアルゴリズムそのもの以外の要素を排除したい。
それらはつまり、以下のようなものである:

将来必要になりそうなことすべて。
念のためにやったほうがよさそうなことすべて。
ボイラープレートや、多プラットフォーム対応、拡張性、柔軟性、
セキュリティ、エラー処理、例外処理、高負荷対策、最高性能の追求、国際化、
再利用性などは一切必要がない。
複数のサンプリング周波数に対応する必要はない。
古いC++(17とか)に対応する必要はない。
macOSだけで動けば良い。
メモリ消費が多くても良い。
実行が遅くても良い。
Apple Siliconの macOS専用でよい。それいがいには一切対応しなくていい。
macOSのバージョンは手元のmacbookだけでいい。
unittestは不要。

音質は、キャンセルができていれば、多少低くて構わない。ただしフィルタは収束して、エコーキャンセルが実際に機能している必要はある。

エコーキャンセルがいったいどのような技術によって支えられているのかの説明ができればよく、それ以外は必要がない。
モノラルで十分、16KHz固定でOK,マルチチャネル不要、
拡張性不要、柔軟性不要。
教育目的に必要ない機能自体を削除し、結果としてコード量を削減する。

型を固定することでテンプレートの利用も削減したい。
テンプレートはエコーキャンセルの仕組みを説明するためには不要。
デバッグ用の機能も不要。

安全性のための機能も不要。
とにかくエコーキャンセルの根本的な仕組みを解説するため以外のコード部分は一切不要。
ソースを1行でも減らす。


## 残したいところ
以下の部分は残したい。

自走できるエコーキャンセラを実装したいので、
また、PortAudioを用いて実際に動かして試せることも必須。
それ以外の要素はすべて必要がない。
ライブラリとして利用する必要がないので、いろいろなオブジェクトを複数生成したり破棄したりする必要もない。
サンプルは一度動けば良い。
複数スレッドに対応する必要もない。


## 変更点の概要

- 16 kHz モノラル固定
- `tools/echoback` はデバイス SR を 16 kHz に固定し、EchoCanceller3 を直接使用

- AudioBuffer の単一帯域化
  - 周波数帯分割（Three-band）を廃止し、常に 1 バンド（フルバンド）として処理
  - レサンプラ依存を除去（同一レート前提の単純コピー）
  - `SplitIntoFrequencyBands()`/`MergeFrequencyBands()` は no-op

- 機能削減（物理削除含む）
  - JSON関連: `api/*echo_canceller3_config_json.*` を削除
  - Metrics/FieldTrial: `*metrics*` および `system_wrappers/include/metrics.h` 等を削除
  - 帯域分割: `modules/audio_processing/splitting_filter.*`、`three_band_filter_bank.*`、`common_audio/signal_processing/splitting_filter_c.c` を削除
  - レサンプラ: `common_audio/resampler/` 配下一式を削除
  - 文字列ユーティリティ: `rtc_base/strings/string_builder.*` を削除
  - RTP/Units API: `api/rtp_*`, `api/units/*` を削除
  - WebAssembly/Node 系: `node_modules/`, `ts/`, `*.wasm*`, 生成 JS などを削除（.gitignore で除外）
  - 多チャネル補助の削除: `common_audio/channel_buffer.*` を撤去し、AudioBuffer を 1ch専用のシンプル実装に置換
  - ミキシング/配列変換系ユーティリティ削除: `common_audio/include/audio_util.h` から multi-channel 用テンプレート群を物理削除
  - `alignment_mixer.*` を撤去（モノラルでは未使用）

## ビルド

macOS C++版 (clang, Xcode SDK が前提)

```
make
```

WASM版

```
make wasm
```


生成物:
- `libaec3.a`
- `echoback` (PortAudio を用いたローカル・エコーバック動作)
- `cancel_file` (PortAudio を用いたローカル・エコーバック動作)

## 実行 (デモ)

ローカル・エコーバック（PortAudio 使用、16 kHz 固定、64サンプルブロック）

```
./echoback [latency_ms=200]
./cancel_file counting16kLong.wav playRecCounting16kLong.wav
node echoback.js
node cancel_file.js counting16kLong.wav playRecCounting16kLong.wav
```









