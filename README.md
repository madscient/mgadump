# mgadump

MSXPLAYer Game Adapter (MGA) を使ってMSXカートリッジのROMをダンプするLinux用CLIツールです。

カートリッジのマッパータイプに応じたプロファイルを `mga_profiles.json` で定義し、バンク切り替えを含む自動ダンプを行います。

## 必要なもの

- Linux (Ubuntu 22.04以降推奨)
- CMake 3.16以上
- g++ (C++17対応)
- [nlohmann/json](https://github.com/nlohmann/json) の `json.hpp`（single-header版）

## ビルド

1. nlohmann/json の `json.hpp` を取得して配置します:

```bash
mkdir -p include/nlohmann
# https://github.com/nlohmann/json/releases から json.hpp をダウンロード
curl -Lo include/nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/latest/download/json.hpp
```

2. ビルドします:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## ディレクトリ構成

```
mgadump/
├── src/
│   ├── main.cpp        CLIエントリポイント
│   ├── mga.hpp/cpp     MGA通信層（COMポート + プロトコル）
│   ├── profile.hpp/cpp プロファイルJSON読み込み
│   ├── dumper.hpp/cpp  プロファイルに従うダンプエンジン
│   └── output.hpp/cpp  バイナリ/hexダンプ出力
├── include/
│   └── nlohmann/
│       └── json.hpp    ← 別途配置
├── mga_profiles.json   カートリッジプロファイル定義
└── CMakeLists.txt
```

## 使い方

```
mgadump [options]

Options:
  -p, --port <dev>        COMポートデバイス        (default: /dev/ttyUSB0)
  -b, --baud <rate>       ボーレート                (default: 115200)
  -s, --slot <n>          スロット番号 1 or 2      (default: 1)
  -m, --mapper <name>     マッパープロファイル名   (default: linear32)
  -P, --profiles <file>   プロファイルJSONファイル (default: mga_profiles.json)
  -o, --output <file>     出力ファイル              (省略時: stdout)
  -f, --format <fmt>      出力形式: bin, hex        (default: bin)
      --list-profiles     プロファイル一覧を表示して終了
      --info              デバイス情報を表示して終了
      --check             カセット挿入状態を確認して終了
  -t, --timeout <ms>      タイムアウト(ms)          (default: 5000)
  -v, --verbose           詳細ログをstderrへ出力
  -h, --help              このヘルプを表示
```

## 使用例

```bash
# デバイス情報を確認
mgadump --info -p /dev/ttyUSB0

# カセット挿入状態を確認
mgadump --check -p /dev/ttyUSB0

# 利用可能なプロファイルを一覧表示
mgadump --list-profiles

# 32KB フラットROM をダンプ（Nemesis初代など）
mgadump -p /dev/ttyUSB0 -m linear32 -o game.rom

# Konami SCC 256KB カートリッジをダンプ（Nemesis2など）
mgadump -p /dev/ttyUSB0 -m konami_scc -o game.rom

# ASCII16 256KB カートリッジをスロット2からダンプ
mgadump -p /dev/ttyUSB0 -s 2 -m ascii16 -o game.rom

# hexダンプで内容を確認（stdoutへ）
mgadump -p /dev/ttyUSB0 -m linear32 -f hex | less

# カスタムJSONでプロファイルを指定
mgadump -P ~/my_profiles.json -m my_mapper -o game.rom
```

## プロファイル

カートリッジのマッパータイプ・アドレス範囲・出力サイズは `mga_profiles.json` で定義します。
`-P` オプションで任意のJSONファイルを指定できます。

### ビルトインプロファイル（mga_profiles.json）

| プロファイル名 | サイズ  | 説明                                              |
|---------------|---------|---------------------------------------------------|
| `linear16`    |  16 KB  | フラットROM (0x4000–0x7FFF)、マッパーなし         |
| `linear32`    |  32 KB  | フラットROM (0x4000–0xBFFF)、マッパーなし         |
| `linear48`    |  48 KB  | フラットROM (0x0000–0xBFFF)、マッパーなし         |
| `konami128`   | 128 KB  | Konami 8KBマッパー (SCCなし)                      |
| `konami`      | 256 KB  | Konami 8KBマッパー (SCCなし)、Metal Gear, Usasなど|
| `konami_scc`  | 256 KB  | Konami SCCマッパー、Nemesis2, Space Manbowなど    |
| `ascii8`      | 256 KB  | ASCII 8KBマッパー                                 |
| `ascii16`     | 256 KB  | ASCII 16KBマッパー                                |

### プロファイルのJSON形式

カスタムプロファイルを追加したい場合は `mga_profiles.json` を編集するか、
別のJSONファイルを `-P` で指定してください。

```json
{
  "profiles": [
    {
      "name": "my_mapper",
      "description": "説明文",
      "total_size": 131072,
      "banks": [
        {
          "switches": [
            { "reg_addr": "0x6000", "value": 0 }
          ],
          "slot_addr": "0x4000",
          "length":    "0x2000",
          "output_offset": "0x0000"
        }
      ]
    }
  ]
}
```

| フィールド          | 型             | 必須 | 説明                                        |
|--------------------|----------------|------|---------------------------------------------|
| `name`             | string         | ✓    | プロファイル名（`-m` で指定する名前）       |
| `description`      | string         |      | 説明文                                      |
| `total_size`       | int/hex string | ✓    | 出力ファイルの総バイト数                    |
| `banks`            | array          | ✓    | バンクごとのダンプ指示リスト                |
| `banks[].switches` | array          |      | バンク切り替えレジスタ書き込みリスト（省略可）|
| `banks[].slot_addr`| hex string     | ✓    | スロット読み出し開始アドレス                |
| `banks[].length`   | hex string     | ✓    | 読み出しバイト数                            |
| `banks[].output_offset` | hex string | ✓  | 出力バッファ上のオフセット                  |

`switches` を省略するとバンク切り替えなし（フラットROM）として扱います。
アドレス値は `"0x4000"` のような16進文字列または整数で記述できます。

## 通信プロトコル

[MSXPLAYer Game Adapter コマンド仕様](https://github.com/v9938/MSXPLAYer_GameAdapter/blob/main/com_command.md) に準拠。

バンク切り替えありのダンプフロー：

```
SPON                         スロット電源ON
  SMWR,reg_addr,value,slot   バンクレジスタ書き込み（バンク切り替え）
  SMTR,slot_addr,length,0,slot  スロット→内部バッファ転送
  BSND,0,length              バッファ→PC バイナリ送信
  ... バンク数分繰り返し
SPOFF                        スロット電源OFF
```

## ライセンス

AGPL-3.0-or-later
