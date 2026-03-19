# Host-FPGA Acquisition Reference

作成日: 2026-03-12
関連チェックリスト: [2026-03-12-host-fpga-acquisition-reference-checklist.md](./2026-03-12-host-fpga-acquisition-reference-checklist.md)
関連プロセスログ: [2026-03-12-host-fpga-acquisition-reference-log.md](./process_log/2026-03-12-host-fpga-acquisition-reference-log.md)

## 目的

ホスト側 acquisition 実装を変更するときに、以下を毎回ゼロから掘り直さなくて済むようにする。

- 初期実装 `aebf296adf72a8ac5a8355f7f539dca87521f724` が実際にどういう取得フローを取っていたか
- `FPGA_ST` / `WAVE_WR_CNT` / `WAVE_RD_CNT` が FPGA 内部で何を意味しているか
- 仕様書と VHDL のどこを source of truth として参照すべきか

## 先に見るべき source of truth

- 仕様書: [`FPGA_FW/シスメックス株式会社様向け広帯域波形処理システム開発_FPGA設計仕様書_Rev1.0_241220.docx`](../FPGA_FW/シスメックス株式会社様向け広帯域波形処理システム開発_FPGA設計仕様書_Rev1.0_241220.docx)
- FPGA top wiring: [`FPGA_TOP.vhd`](../FPGA_FW/ANA_20250129_restored/RTL/FPGA_TOP.vhd)
- measurement state machine: [`MEAS_CTRL.vhd`](../FPGA_FW/ANA_20250129_restored/RTL/MEAS_CTRL/MEAS_CTRL.vhd)
- DDR write control: [`DDR3_WCTRL.vhd`](../FPGA_FW/ANA_20250129_restored/RTL/DDR3_WCTRL/DDR3_WCTRL.vhd)
- DDR read control: [`DDR3_RCTRL.vhd`](../FPGA_FW/ANA_20250129_restored/RTL/DDR3_RCTRL/DDR3_RCTRL.vhd)
- register exposure: [`REG_CTRL.vhd`](../FPGA_FW/ANA_20250129_restored/RTL/REG_CTRL/REG_CTRL.vhd)

仕様書では特に以下の節を先に読む。

- `8.2.3 000004h FPGA_STレジスタ`
- `5.10 MEAS_CTRL部機能詳細`
- `5.15 DDR3_WCTRL部機能詳細`
- `5.16 DDR3_RCTRL部機能詳細`
- `5.5.2 波形データフロー(EP6)`

## `aebf296` の host acquisition フロー

初期実装 `aebf296adf72a8ac5a8355f7f539dca87521f724` の `LoopTestProcessThread_EP6_GetData` は、概ね次の流れで動いていた。

1. manual mode の場合は `MANUAL_MEAS_ON=1` を EP2 で送って sampling を開始する。
2. EP4 polling で `FPGA_ST[4]` (`SampleStartSt`) を待ち、`FPGA start sampling.` を出す。
3. 取得ループでは、`DDR_WR_END==0` の間は `WAVE_WR_CNT` を見ながら「今読めそうなサイズ」を都度計算し、その分だけ EP6 を読む。
4. `DDR_WR_END==1` を見た時点で `WAVE_WR_CNT + 32` を `MaxDDRBytes` として固定し、それ以上増えない前提で EP6 読み出しを継続する。
5. `SaveDDRBytes >= MaxDDRBytes` になったら EP6 読み出しループを抜ける。
6. その後に別の wait loop で再度 `DDR_WR_END==1` を確認し、manual mode の場合は `MANUAL_MEAS_ON=0` を送って停止する。

重要なのは、初期実装も **完了判定の中心に `DDR_WR_END` を置いていた** ことと、`DDR_RD_END` を取得完了条件として使っていなかったこと。

## FPGA 側での実際の意味

### `FPGA_ST[2] = DDR3_WR_END` は最終完了ではない

`FPGA_TOP.vhd` では `REG_CTRL` に公開される `DDR_WR_END` が `s_samp_end` に接続されている。

- `WAVE_WR_CNT => s_limit_addr_ep6`
- `WAVE_RD_CNT => s_av_mm_ep6_addr`
- `DDR_RD_END => s_DDR_RD_END`
- `DDR_WR_END => s_samp_end`

一方、`DDR3_RCTRL` に渡される write-end 相当の入力は `s_ddr_wstop` であり、host に見えている `s_samp_end` とは別物。

### `s_samp_end` は `RD_WAIT` 中ずっと 1

`MEAS_CTRL.vhd` では `P_MCTRL_RD_WAIT` に入ると `r_SAMP_END <= '1'` になる。`RD_WAIT` は「DDR リード完了待ち」状態であり、`DDR_RD_END` が立つまで `IDLE` に戻らない。

つまり host から見える `DDR3_WR_END=1` は、

- sampling が止まった
- read/drain phase に入った

ことは示しても、

- DDR write path が完全に安定した
- EP6 で読み切ってよい最終サイズが確定した
- acquisition cycle 全体が完了した

ことまでは示さない。

### 実際の write stop は `DDR_WSTOP`

`DDR3_WCTRL.vhd` では `DDR_WSTOP <= r_stop_trans or r_mem_full` で生成され、`r_stop_trans` は `s_wc_end=1` と遅延済み `SAMP_END` を見た後に立つ。

つまり FPGA 内部では、

- `s_samp_end`: coarse な sampling end / drain entry
- `s_ddr_wstop`: write controller が認めた stop

の 2 段階になっている。

### 真の完了は `DDR_RD_END`

`DDR3_RCTRL.vhd` では `DDR_RD_END <= '1' when (SAMP_END = '1' and r_ddr_addr >= LIMIT_ADDR)` で定義される。ここでの `SAMP_END` は `FPGA_TOP.vhd` 上 `s_ddr_wstop` に接続されている。

したがって host 側で acquisition completion を考えるときの意味合いは次の通り。

- `DDR_WR_END`: sampling/draining への移行ヒント
- `DDR_RD_END`: DDR から読むべき範囲を読み切った完了信号

## stale status について

仕様書の `FPGA_ST` 説明では `DDR3_WR_END` / `DDR3_RD_END` はどちらも `Measure Start で Clear` と書かれている。つまり stale status がサイクル跨ぎで残るのは仕様と整合する。

`REG_CTRL.vhd` でも reset 直後の `r_FPGA_ST` 初期値は `x"000C"` で、bit2/bit3 が 1 の状態から始まる。

このため host 側で `DDR_WR_END==1` を見た瞬間に完了扱いする実装は危険で、少なくとも「新サイクル開始を観測したか」「`DDR_RD_END` まで見たか」を分けて扱う必要がある。

## 今後の host 実装ルール

- acquisition 完了条件は `DDR_RD_END` を主に使い、`DDR_WR_END` は sampling stop / draining entry のヒントとして扱う。
- `WAVE_WR_CNT` / `WAVE_RD_CNT` の意味を変える修正を入れる前に、`REG_CTRL.vhd` と `FPGA_TOP.vhd` の配線元まで必ず辿る。
- stale status 対策を入れるときは「bit が clear するまでの poll 回数」ではなく、「新サイクル開始を本当に観測できたか」を基準に設計する。
- unit test / simulation では、`DDR_WR_END=1` と `DDR_RD_END=0` が同時に成立する `RD_WAIT` 状態を必ず再現する。
- `aebf296` と同系統のロジックを再利用するときは、「たまたま動いた legacy flow」なのか「FPGA 仕様に合っている flow」なのかを分けてレビューする。

## 調査開始チェックリスト

acquisition まわりを触る前に最低限これを確認する。

1. 仕様書の `FPGA_ST` / `MEAS_CTRL` / `DDR3_WCTRL` / `DDR3_RCTRL` を読む。
2. `FPGA_TOP.vhd` で host-visible register bit がどの内部信号に配線されているか確認する。
3. host 側コードで `DDR_WR_END` / `DDR_RD_END` / `WAVE_WR_CNT` / `WAVE_RD_CNT` をどこで完了条件に使っているか検索する。
4. field log で `DDR_WR_END` と `DDR_RD_END` がどう並んでいるか確認する。
5. simulation / unit test が `RD_WAIT` と stale status を再現しているか確認する。
