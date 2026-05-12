#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <cstdio>

enum class OutputFormat {
    Binary,     // バイナリそのまま              (-f bin,  拡張子 .rom/.bin)
    IntelHex,   // インテルHEXフォーマット        (-f hex,  拡張子 .hex)
    TextDump,   // アドレス+hex+ASCII 3カラム    (-f text, 拡張子 .txt)
    Motorola,   // モトローラSレコード形式        (-f mot,  拡張子 .mot)
};

// 出力先（ファイルまたはstdout）へデータを書き出す
// path が空の場合は stdout へ書く
void writeOutput(const std::vector<uint8_t>& data,
                 OutputFormat fmt,
                 const std::string& path = "");

// フォーマット名文字列 → OutputFormat（不明な場合は例外）
OutputFormat parseFormat(const std::string& name);

// 各フォーマットの内部実装（単体テスト・再利用用）
void writeIntelHex(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr = 0);
void writeTextDump(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr = 0);
void writeMotorolaSRecord(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr = 0);
