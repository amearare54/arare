# zmk-config-arare

arare（左右分割・完全無線・charlieplex・トラックボール＋ジョイスティック）のZMK設定。

## 構成（2026-08-31 全面書き直し）

| | 左手 (peripheral) | 右手 (**central**) |
|---|---|---|
| ボード | seeeduino_xiao_ble | seeeduino_xiao_ble |
| マトリクス | charlieplex 6線 (M0=D0 M1=D1 M2=D6 M3=D7 M4=D8 M5=D9) | charlieplex 6線 (M0..M5=D0..D5) |
| エンコーダ | EC12縦 (D2/D3) 押込=Shift+Cmd+V | CKW12横 (D6/D7) 押込=左クリック |
| ポインタ | ジョイスティック押込=D10直結キー（アナログXYは未実装※） | PAW3222 (spi1: SCK=P1.13 SDIO=P1.14 CS=P1.15 MOTION=P0.09) |

- ZMK v0.3.0 / PAW3222ドライバ = sekigon-gonnoc/zmk-driver-paw3222 (torabo-tsukiブランチ)
- ZMK Studio対応（central=右手にUSB接続して https://zmk.studio/ ）。アンロックキー＝各手内側のタクトSW
- タクトSW外側＝その手のリセット
- ピン割当・マトリクス定義は基板実データ（arare_matrix.json / .kicad_pcb）と機械照合済み

## ビルド

pushするとGitHub Actionsがビルド（`arare_R`=Studio snippet付き / `arare_L` / `settings_reset`）。
ArtifactsのUF2を、XIAOのBOOTモード（リセット2度押し）で書き込む。

## ※フェーズ2（未実装）

- **ジョイスティックのアナログXY**: badjeff/zmk-analog-input-driver を使う予定だが、
  ZMK本体のバッテリー計測が SAADC oversampling=4 をハードコードしており併用すると約1分でハングする
  既知問題がある（ZMKフォークで oversampling=0 にするか、左手の電池報告を無効にする必要あり）。
- **DYA Studio フル対応**: キーマップ編集は公式ZMK Studio対応のままDYA Studioからも使える。
  トラックボール調整等のフル機能は cormoran/zmk フォーク＋モジュール4点が必要（experimental宣言あり）。
- 深いスリープ（CONFIG_ZMK_SLEEP）はcharlieplexが割込を持てず復帰不能のため無効のまま。電源は物理SW。
