# 削除検討時の判断メモ

deletion.md に挙げた削除候補のうち、実際に試したあとに「完全削除ではなく一部を残す」と判断したものの記録。

## `FilteringQualityAnalyzer` を完全には消さなかった理由 (2026-04-18)

### 試したこと
- `FilteringQualityAnalyzer` を丸ごと削除して `UsableLinearEstimate()` を常に `true` に固定した。

### 起きた問題
- echoback で **起動直後のハウリングが目立って悪化**した。
- 理由: AEC3 起動直後は線形フィルタ $H_p[k]$ がまだ学習中で、残差 $E_b[k]$ にエコー成分が大きく残る。`UsableLinearEstimate=true` だと、このまだ消せていない残差を出力として loopback に流してしまう。ループバック経路で増幅されてハウリングになる。
- cancel_file(オフライン)なら loopback がないので問題は出ないが、echoback では致命的。

### 判断
- 構造体ごとの削除はやめ、**単純なブロックカウンタ 1 つ + 閾値 1 つ** の極小ウォームアップだけを残す。
- 元の複雑な state(3 つのカウンタ + `convergence_seen_`、active_render 判定、`sufficient_data_to_converge_at_reset` の二段判定)はすべて廃止。
- 残すロジックは「起動後 N ブロック経過したら線形モードを有効化する」だけ。

### 教育的な意図
- 「線形フィルタは収束前は信じない」という判断ロジック自体は AEC3 の本質なので、最小形で残したほうが教材価値が高い。
- ウォームアップが `UsableLinearEstimate` を介してどう抑圧モードの切り替え(`E` vs `Y`、`S2_linear` vs `R2`)に影響するかを、1 つのカウンタで追える形にする。

## `SuppressionGain::GetMinGain`/`GetMaxGain` を消さなかった理由 (2026-04-18)

### 試したこと
- `SuppressionGain` から `last_gain_` / `last_nearend_` / `last_echo_` の履歴メンバと、定数 `kMaxIncFactor` (=2.0f) / `kMaxDecFactorLf` (=0.25f) と、`GetMinGain` / `GetMaxGain` を削除。
- `LowerBandGain` を「`GainToNoAudibleEcho` の結果を [0,1] にクランプして平方根を取るだけ」に簡素化。

### 起きた問題
- echoback でキャンセル性能が下がり、**エコーが2回ぐらい聞こえる**ようになった (revert 前は 0 回)。
- 理由: `GetMaxGain` が「今ブロックのゲインは前ブロックのゲイン `last_gain_[k]` の `kMaxIncFactor` (=2.0) 倍まで」という上限クランプをかけていた。これを外すと、あるブロックでゲインが一気に 1.0 まで跳ね上がれる。
- 結果、抑圧されるべきエコー帯域で瞬間的にゲインが 1.0 になり、エコーがそのまま通過 → loopback 経路で増幅されて二次エコーとして聞こえる。
- `GetMinGain` も「低域では `last_gain_[k] * kMaxDecFactorLf` を下回らない」下限クランプを提供していて、急激なゲイン低下(近端音声の立ち上がり喰い)を防ぐ役割がある。

### 判断
- 履歴メンバ 3 本と `GetMinGain`/`GetMaxGain` は**消さずに残す**。
- 「前ブロックのゲインを参照して増減を滑らかに制限する」ロジックは、loopback ありの echoback では明確に聴感に効く。

### 教育的な意図
- ゲイン計算は「今ブロックのスペクトルだけ見る瞬時判定」だけでは不十分で、**時間方向のヒステリシス (前ブロックとの連続性) が必要** という点は AEC3 の非自明な知見。
- ゲインの急増を許すとエコーが漏れ、ゲインの急減を許すと近端音声のアタックが削れる、という非対称な制約の両方を、`GetMaxGain`/`GetMinGain` という対の関数で表現している構造も教材価値が高い。
