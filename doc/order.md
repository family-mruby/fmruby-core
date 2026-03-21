/home/kishima/fmrb/family-mruby/fmruby-core/doc/ に以下の3つのドキュメントがあります:

1. upstream_merge_plan.md - 本流PicoRuby統合の全体計画
2. current_patch_list.md - 現在適用中のパッチ一覧
3. mrb_tick_analysis.md - mrb_tick実装分析とレースコンディション問題

これらを読んで、以下の作業を進めてください:

【現在の状況】
- Step 1 (パッチ内容把握) は完了
- 引き継ぎ資料作成も完了

【次のステップ】
upstream_merge_plan.md の "Step 2: サブモジュール更新前の準備" から作業を開始してください。

具体的には:
1. 現在のサブモジュール状態確認とバックアップ
2. 本流の最新コミット調査
3. scheduler_lock等の機能がいつ追加されたか調査