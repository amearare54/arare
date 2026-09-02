> [!CAUTION]
> ここに公開している3Dデータは個人使用のみにとどめてください。
> 再配布・商用利用はご遠慮ください。

# arare 3Dプリントデータ

ケース・プレート・ノブ等の3Dデータです。**基板データ（KiCad/ガーバー）とキーキャップは非公開です。**

## ファイル一覧（case/）

| ファイル | 内容 |
|---|---|
| top plate L / R | トッププレート（Rはトラックボールケース一体） |
| middle plate L 1〜3 / R 1〜2 | ミドルプレート（キースイッチホルダー、厚1.2mm）。**エンコーダの有る無しなどに合わせて選べます** |
| bottom plate L / R | ボトムプレート（磁石ポケットΦ6×3・ゴム足座つき） |
| gasket mount L / R | ガスケットマウント |
| vertical rotary encoder 1 / 2 | 左手・縦型エンコーダ（EC12）用ノブまわり |
| horizontal rotary encoder 1 | 右手・水平エンコーダ（CKW12）用なぞりホイール |
| parts 1〜4 | 小物（タクトSWキャップ・押し棒・電源SWツマミ等） |

## ロゴ（logo/）

「arare＝霰」に掛けて、文字ごとに和の気象モチーフ（霞・雨・波紋・しずく）が入っています。

| ファイル | 内容 |
|---|---|
| arare_logo.svg | ★マスター。ベクター・無次元（`fill="currentColor"`） |
| arare_logo_黒 / 白.png | ラスタ版（6788×1928px・背景透過） |
| arare_logo_{R 13.324mm / L 16.179mm}.svg / .dxf | 基板実寸のベクター（DXFはCAD用・y上向き） |
| arare_logo_{R/L}_*_emboss0.6mm.stl | 3Dプリント用エンボス（厚0.6mm・watertight） |

## 印刷設定の目安（Bambu Lab A1 mini / eSUN PLA+ で実績）

- **ケース・プレート類（このフォルダの大物）: 0.4mmノズル**、壁4ライン、Arachne有効
- 小物（parts・エンコーダノブ）: 0.2mmノズルだと細部（0.5mm級フィーチャ）の再現性が上がる
- ビルドプレートは Cold PEI 想定
