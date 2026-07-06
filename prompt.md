# Copyable /goal prompts

Use `goal.md` as the detailed execution policy. This file is only for copy/paste
startup prompts.

## Primary Prompt

/goal goal.md の方針に従い、docs/plans/260703-analogboard-rebuild-plan.html の feature/win11-driver-compat 再作成（詳細範囲は docs/driver_next.md）を自律的に実行する。phase 内は小さな batch に分け、各 batch ごとに tasks/todo.md に記録し、実装、関連テスト、Review更新、tasks/todo_archive.md への移動まで完了する。phase 全体が完了したら phase-level focused verification、`.agent/refactor.md` に従う refactor pass、`.agent/review.md` に従う reviewer agent review pass、必要な修正反映、`$claude-review-fixer` による review、必要な修正反映、Claude 接続不可または長めの timeout（default 30 分、PR review は 60 分）後も返信がない場合は理由を tasks/todo.md に記録して review を skip、final diff inspection を行い、関連変更だけを1 commitにまとめて push する。無関係なユーザー変更は含めない。ブロッカー、破壊的操作、外部公開、仕様判断が必要な場合、および MSVC ビルド検証が実行できない環境で最終 checkpoint に達した場合だけ停止して確認する。

## Resume Prompt

/goal goal.md の方針に従い、現在の git 状態と tasks/todo.md を確認して、feature/win11-driver-compat 再作成の未完了 batch から再開する。完了済み batch は繰り返さず、停止条件に該当する場合だけ確認する。

## Regeneration Prompt

goal-prompt-generator skill と docs/plans/260703-analogboard-rebuild-plan.html を読み、feature/win11-driver-compat 再作成用の goal.md と prompt.md を再生成する。既存の goal.md / prompt.md が使用中なら上書きせず draft として出力する。
