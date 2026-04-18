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
