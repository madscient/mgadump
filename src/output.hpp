#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <cstdio>

enum class OutputFormat {
    Binary,   // バイナリそのまま
    HexDump,  // xxd形式のhexダンプ
};

// 出力先（ファイルまたはstdout）へデータを書き出す
// path が空の場合は stdout へ書く
void writeOutput(const std::vector<uint8_t>& data,
                 OutputFormat fmt,
                 const std::string& path = "");

// hexダンプをストリームへ書く（デバッグ・表示用）
void writeHexDump(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr = 0);
