# USB Acquisition Stability Field Reference

対象プラン: [USB データ取得・書き込み安定性改善プラン](./2026-03-02-usb-acquisition-stability.md)

この文書は field gate / failure signature / next session bundle をまとめる。日々の進行判定は checklist を参照する。

## Known Failure Signatures

### Type A

- active acquisition 中の `EP6 timeout`
- cycle end でも `DDR_WR_END=0`, `DDR_RD_END=0`
- `Wait fpga write completed. Timeout` または disconnect を伴うことがある

Representative logs:

- `logs/0.1.4r/logs/2603161450.log`
- `logs/0.1.4r/logs/2603161801.log`
- `logs/0.1.4r3/logs/2603171504.log`

### Type B

- drain hint 後の `EP6 timeout`
- cycle end は `DDR_WR_END=1`, `DDR_RD_END=0`
- `Wait acquisition completed. Timeout` や EP4 fail / disconnect を伴う

Representative logs:

- `logs/0.1.4r/logs/2603161450.log`
- `logs/0.1.4r2/logs/2603161532.log`

### Type C

- normal completion
- `ep6Timeouts=0`
- `DDR_WR_END=1`, `DDR_RD_END=1`

Representative logs:

- `logs/0.1.4r2/logs/2603161532.log`
- `logs/0.1.4r2/logs/2603161546.log`
- `logs/0.1.4r7/logs/2603181422.log`

## Key Field Milestones

### `0.1.4r7` Phase 2 pre-gate close

- `water -> low x3 -> mid x3 -> high x3`
- low-density 3 run (`1426`, `1429`, `1431`) はすべて Type C
- mid/high は既知の Type A / Type B と整合
- startup stale complete regression は見えなかった

### `0.1.4r4` preview session

- preview を見ながら 12 run
- preview 常用で新規 regression は見えなかった
- publish-failure non-fatal path 自体は実機未踏

### `0.1.4r6` burst-cap observation

- file appearance burst は以前より小さくなった
- retained `.tmp` 観測は取れた
- high-density timeout は unresolved のまま

## Next Field Session Bundle

1. low-density smoke 1-3 cycle
   - `saved wave count 0` / immediate exit が消えたことを先に確認する
   - `DDR_RD_END=1`, `ep6Timeouts=0`, empty capture 0 を見る

2. direct marker check
   - `[PR04][ENGINE_ENTER]`
   - `[PR04][ENGINE_EXIT]`
   - `[PR04][STARTUP_EP4]`
   - `[PR01][CYCLE]`

3. 100-cycle gate
   - release-track binary で 100 cycle 以上
   - stop / corruption / missing pair がないこと

4. preview / consumer session
   - `.tmp` non-public
   - pair index monotonic
   - incomplete pair が見えないこと

## Win11 / New Driver Notes

- Win10 では high-density でも `ep6Timeouts=0` の観測あり
- Win11 では `EP6 timeout` が出る
- new driver 更新時は `logs/0.1.4r7/logs/2603191025.log` で `Get ep4 register data failed.` が intermittent に出て acquisition が悪化した
- これは release-track field gate とは分けて、parallel compatibility track で扱う

## Related References

- active tasks: [USB acquisition stability checklist](./2026-03-02-usb-acquisition-stability-checklist.md)
- release-track next task: [baseline_next.md](./baseline_next.md)
- lab support next task: [lab_next.md](./lab_next.md)
- evidence history: [process log](../../process_log/2026-03-02-usb-acquisition-stability-log.md)
