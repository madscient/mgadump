#include "output.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// OutputFormat のパース
// ---------------------------------------------------------------------------
OutputFormat parseFormat(const std::string& name) {
    if (name == "bin")  return OutputFormat::Binary;
    if (name == "hex")  return OutputFormat::IntelHex;
    if (name == "text") return OutputFormat::TextDump;
    if (name == "mot")  return OutputFormat::Motorola;
    throw std::runtime_error(
        "Unknown format: '" + name + "' (use bin, hex, text, mot)");
}

// ---------------------------------------------------------------------------
// インテルHEXフォーマット (Intel HEX / .hex)
//
// レコード構成:
//   :LLAAAATT[DD...]CC
//   LL   バイト数
//   AAAA アドレス（16ビット）
//   TT   レコードタイプ
//   DD   データ
//   CC   チェックサム（LL+AAAA+TT+DDの総和の2の補数の下位1バイト）
//
// レコードタイプ:
//   00  データ
//   01  EOF
//   04  拡張リニアアドレス（上位16ビット、32ビット空間対応）
//
// baseAddr が 0x10000 以上の場合や途中で上位16ビットが変わる場合は
// タイプ04レコードを挿入する。
// ---------------------------------------------------------------------------
static uint8_t ihex_checksum(const uint8_t* buf, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += buf[i];
    return static_cast<uint8_t>((~sum + 1) & 0xFF);
}

static void ihex_write_record(FILE* out, uint8_t type,
                              uint16_t addr, const uint8_t* data, uint8_t len) {
    // チェックサム用バッファ: [len, addrH, addrL, type, data...]
    uint8_t buf[4 + 255];
    buf[0] = len;
    buf[1] = static_cast<uint8_t>(addr >> 8);
    buf[2] = static_cast<uint8_t>(addr & 0xFF);
    buf[3] = type;
    for (uint8_t i = 0; i < len; ++i) buf[4 + i] = data[i];
    uint8_t cs = ihex_checksum(buf, 4 + len);

    fprintf(out, ":%02X%04X%02X", len, static_cast<unsigned>(addr), type);
    for (uint8_t i = 0; i < len; ++i) fprintf(out, "%02X", data[i]);
    fprintf(out, "%02X\n", cs);
}

void writeIntelHex(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr) {
    constexpr size_t DATA_PER_RECORD = 16;

    uint16_t currentUpper = 0xFFFF;  // 初回は必ず04レコードを出力させるための初期値

    for (size_t i = 0; i < data.size(); i += DATA_PER_RECORD) {
        uint32_t addr32 = baseAddr + static_cast<uint32_t>(i);
        uint16_t upper  = static_cast<uint16_t>(addr32 >> 16);
        uint16_t lower  = static_cast<uint16_t>(addr32 & 0xFFFF);

        // 上位アドレスが変わったタイミングで拡張リニアアドレスレコード(04)を挿入
        if (upper != currentUpper) {
            uint8_t ext[2] = {
                static_cast<uint8_t>(upper >> 8),
                static_cast<uint8_t>(upper & 0xFF)
            };
            ihex_write_record(out, 0x04, 0x0000, ext, 2);
            currentUpper = upper;
        }

        // データレコード(00)
        size_t chunk = std::min(DATA_PER_RECORD, data.size() - i);
        ihex_write_record(out, 0x00, lower, &data[i], static_cast<uint8_t>(chunk));
    }

    // EOFレコード(01)
    ihex_write_record(out, 0x01, 0x0000, nullptr, 0);
}

// ---------------------------------------------------------------------------
// テキストダンプ（3カラム形式）
//   アドレス: 8桁 hex
//   hex部: 1行16バイト、各バイトをスペース区切り
//   ASCII部: 0x20〜0x7E のみ表示、コントロールコード・0x80以上は '.'
//
// 例:
//   00000000  4d 5a 90 00 03 00 00 00 04 00 00 00 ff ff 00 00  MZ..............
// ---------------------------------------------------------------------------
void writeTextDump(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr) {
    constexpr size_t W = 16;
    for (size_t i = 0; i < data.size(); i += W) {
        fprintf(out, "%08x  ", static_cast<unsigned>(baseAddr + i));

        for (size_t j = 0; j < W; ++j) {
            if (i + j < data.size())
                fprintf(out, "%02x", data[i + j]);
            else
                fprintf(out, "  ");
            fprintf(out, " ");
        }

        fprintf(out, " ");

        for (size_t j = 0; j < W && i + j < data.size(); ++j) {
            uint8_t c = data[i + j];
            fprintf(out, "%c", (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '.');
        }
        fprintf(out, "\n");
    }
}

// ---------------------------------------------------------------------------
// モトローラSレコード形式
//
// レコード構成:
//   S0: ヘッダレコード
//   S1: データレコード（16ビットアドレス）
//   S2: データレコード（24ビットアドレス、0xFFFF超の場合）
//   S5: レコード数
//   S9: 終端レコード（S1対応）/ S8（S2対応）
//
// チェックサム: バイト数+アドレス+データの総和の1の補数の下位1バイト
// ---------------------------------------------------------------------------
static uint8_t srec_checksum(const uint8_t* buf, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += buf[i];
    return static_cast<uint8_t>(~sum & 0xFF);
}

void writeMotorolaSRecord(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr) {
    constexpr size_t DATA_PER_RECORD = 32;

    const uint32_t endAddr = baseAddr + static_cast<uint32_t>(data.size()) - 1;
    const bool use24bit    = (endAddr > 0xFFFF);
    const char dataType    = use24bit ? '2' : '1';
    const size_t addrBytes = use24bit ? 3 : 2;

    // S0: ヘッダレコード
    {
        const char* module = "mgadump";
        size_t mlen = strlen(module);
        uint8_t byte_count = static_cast<uint8_t>(2 + mlen + 1);
        uint8_t buf[64];
        size_t bi = 0;
        buf[bi++] = byte_count;
        buf[bi++] = 0x00;
        buf[bi++] = 0x00;
        for (size_t i = 0; i < mlen; ++i) buf[bi++] = static_cast<uint8_t>(module[i]);
        uint8_t cs = srec_checksum(buf, bi);
        fprintf(out, "S0%02X0000", byte_count);
        for (size_t i = 2; i < bi; ++i) fprintf(out, "%02X", buf[i]);
        fprintf(out, "%02X\n", cs);
    }

    // S1/S2: データレコード
    uint32_t recordCount = 0;
    for (size_t i = 0; i < data.size(); i += DATA_PER_RECORD) {
        size_t chunk = std::min(DATA_PER_RECORD, data.size() - i);
        uint32_t addr = baseAddr + static_cast<uint32_t>(i);
        uint8_t byte_count = static_cast<uint8_t>(addrBytes + chunk + 1);

        uint8_t buf[4 + DATA_PER_RECORD];
        size_t bi = 0;
        buf[bi++] = byte_count;
        if (use24bit) buf[bi++] = static_cast<uint8_t>((addr >> 16) & 0xFF);
        buf[bi++] = static_cast<uint8_t>((addr >>  8) & 0xFF);
        buf[bi++] = static_cast<uint8_t>( addr        & 0xFF);
        for (size_t j = 0; j < chunk; ++j) buf[bi++] = data[i + j];
        uint8_t cs = srec_checksum(buf, bi);

        fprintf(out, "S%c%02X", dataType, byte_count);
        if (use24bit) fprintf(out, "%02X", (addr >> 16) & 0xFF);
        fprintf(out, "%02X%02X", (addr >> 8) & 0xFF, addr & 0xFF);
        for (size_t j = 0; j < chunk; ++j) fprintf(out, "%02X", data[i + j]);
        fprintf(out, "%02X\n", cs);

        ++recordCount;
    }

    // S5: レコード数
    if (recordCount <= 0xFFFF) {
        uint8_t buf[3] = {
            0x03,
            static_cast<uint8_t>((recordCount >> 8) & 0xFF),
            static_cast<uint8_t>( recordCount       & 0xFF)
        };
        uint8_t cs = srec_checksum(buf, 3);
        fprintf(out, "S503%04X%02X\n", static_cast<unsigned>(recordCount), cs);
    }

    // S8/S9: 終端レコード
    if (use24bit) {
        uint8_t buf[4] = { 0x04, 0x00, 0x00, 0x00 };
        uint8_t cs = srec_checksum(buf, 4);
        fprintf(out, "S804000000%02X\n", cs);
    } else {
        uint8_t buf[3] = { 0x03, 0x00, 0x00 };
        uint8_t cs = srec_checksum(buf, 3);
        fprintf(out, "S9030000%02X\n", cs);
    }
}

// ---------------------------------------------------------------------------
// writeOutput: フォーマット・出力先を統括
// ---------------------------------------------------------------------------
void writeOutput(const std::vector<uint8_t>& data,
                 OutputFormat fmt,
                 const std::string& path) {
    const bool isBinary = (fmt == OutputFormat::Binary);
    const char* mode    = isBinary ? "wb" : "w";

    FILE* f = path.empty() ? stdout : fopen(path.c_str(), mode);
    if (!f)
        throw std::runtime_error("Cannot open output file: " + path
                                 + " (" + strerror(errno) + ")");

    switch (fmt) {
        case OutputFormat::Binary: {
            size_t written = fwrite(data.data(), 1, data.size(), f);
            if (written != data.size())
                throw std::runtime_error("Write error");
            break;
        }
        case OutputFormat::IntelHex: writeIntelHex       (data, f); break;
        case OutputFormat::TextDump: writeTextDump        (data, f); break;
        case OutputFormat::Motorola: writeMotorolaSRecord (data, f); break;
    }

    if (!path.empty()) fclose(f);
}
