# minAEC3 最小化に向けた削除候補メモ

教育目的で、音声処理の本質に関係ない部分を削る計画。安全性・スケーラビリティ・将来拡張性は無視してよい。

## 実施結果 (2026-04-18 完走)

- step 1 ✅ `SubtractorOutputAnalyzer` 削除 (46fff92)
- step 2 ✅ `FilteringQualityAnalyzer` を単一カウンタに簡素化 (7b354cd) — 完全削除は echoback で起動直後ハウリングが出たため断念。詳細は deletion_memo.md
- step 3 ✅ 聴覚マスキング系削除 (a8a45a4)
- step 4 ❌ `GetMinGain`/`GetMaxGain` 削除は試したが revert。echoback でエコーが 2 回程度聞こえるようになった。詳細は deletion_memo.md (b869bcf)
- step 5 ✅ `ResidualEchoEstimator` ノイズフロア推定削除 (37c27a5)
- step 6 ✅ `FilterUpdateGain` のウォームアップのうち `poor_excitation_counter_` だけ削除 (9e1ce00) — `call_counter_` は PFDAF 履歴満タン待ちとして残した(削るとハウリング発散)

## 公開 API(echoback.cc / cancel_file.cc から呼ばれているもの)

以下は壊してはいけない:

- `EchoCanceller3::ProcessBlock(Block* capture, const Block* render)`
- `EchoCanceller3::SetProcessingModes(bool enable_linear, bool enable_nonlinear)`
- `EchoCanceller3::GetLastMetrics()` (echoback から読む。ただし `linear_usable` が常に true になる程度は許容)

---

## 削除候補(教育的価値の低い順)

### 最優先で削れるもの(フェーズ 1)

#### (1) `SubtractorOutputAnalyzer` 構造体ごと

- ファイル: `subtractor_output_analyzer.h`
- 中身は `e2 < 0.5·y2` という 1 行の閾値判定フラグだけ
- 使い先は `AecState::Update` で `any_filter_converged` を作るためだけ
- 対応: `AecState::Update` の中でインライン化して、この構造体を丸ごと消す

#### (2) `FilteringQualityAnalyzer`(AecState のネスト構造体)

- ファイル: `aec_state.h:46-81`
- 起動後 0.4 秒 + リセット後 0.2 秒のウォームアップで `UsableLinearEstimate()` を遅延起動するだけ
- 対応: `UsableLinearEstimate()` を `const` メソッドで常に `true` を返すように固定。`convergence_seen_`, `filter_update_blocks_since_reset_`, `filter_update_blocks_since_start_`, `overall_usable_linear_estimates_` 全廃
- 副作用: 起動直後でも線形モードが選ばれる(収束前は残差 E にエコーが混じるが、教育版なら許容)

#### (3) 聴覚マスキング系(suppression_gain.h)

- `WeightEchoForAudibilitySpan`, `ApplyAudibilityWeight`(約 45 行)
- `ApplyLowFrequencyLimit`, `ApplyHighFrequencyLimit`(約 15 行)
- 人間の聴覚心理を反映した高度な最適化。NLMS + ゲイン抑圧の教育には要らない
- 対応: `LowerBandGain` 内で `weighted_residual_echo = residual_echo` に置換し、低域/高域補正呼び出しを削除
- 関連する定数(`kFloorPower`, `kAudibilityLf/Mf/Hf` など)も削除

### 中優先(関数・メンバ単位)(フェーズ 2)

#### (4) ゲイン履歴平滑化 `GetMinGain` / `GetMaxGain`

- ファイル: `suppression_gain.h:147-176`
- 前フレームゲイン × 2(増加上限)/ × 0.25(減少下限)で変化を抑制
- 削ると急変は起きるが、ゲインの計算原理(enr/emr 比)は保たれる
- 連動して消せるメンバ: `last_gain_`, `last_nearend_`, `last_echo_`
- 連動して消せる定数: `kMaxIncFactor`, `kMaxDecFactorLf`

#### (5) `ResidualEchoEstimator` のノイズフロア推定

- ファイル: `residual_echo_estimator.h`
- `X2_noise_floor_`, `X2_noise_floor_counter_`
- `UpdateRenderNoisePower`, `ApplyNoiseGateToSpectrum`
- 関連定数: `kNoiseFloorHold`, `kMinNoiseFloorPower`, `kStationaryGateSlope`, `kNoiseGatePower`, `kNoiseGateSlope`
- 教育的には「R² = S²/ERLE」か「R² ≈ 遠端スペクトル」で十分

#### (6) `FilterUpdateGain` のウォームアップ

- ファイル: `filter_update_gain.h:42-46`
- `call_counter_`, `poor_excitation_counter_` で最初の P ブロック学習を止める部分
- 削ると初期数ブロックでゲインが暴れるが、式自体は変わらない
- 関連定数: `kPoorExcitationCounterInitial`

### 残すことを推奨(削らない)

- **`Constrain()` / `partition_to_constrain_`**: PFDAF の数値安定性の要。消すとフィルタが時間領域で 128 サンプルに漏れて発散する。**「なぜ必要か」を教えるほうが教育価値が高い**ので残す
- **`frequency_response_` / `ComputeErl`**: `update_gain` の `erl` 入力に使うので意味がある

---

## 段階的実施計画

各フェーズの終わりに必ずビルドして echoback / cancel_file が動くか確認する。

- **フェーズ 1**: (1)(2)(3) で約 150 行減
- **フェーズ 2**: (4)(5) で約 80 行減
- **フェーズ 3**: (6) で約 20 行減

合計: 約 250 行 / 全体 2531 行(約 10%)の削減。
