#include "output.hpp"
#include <stdexcept>
#include <cstring>
#include <cctype>
#include <iomanip>
#include <sstream>

void writeHexDump(const std::vector<uint8_t>& data, FILE* out, uint32_t baseAddr) {
    // xxd互換形式: "00000000: 4d5a 9000 ...  MZ....."
    constexpr size_t bytesPerLine = 16;

    for (size_t i = 0; i < data.size(); i += bytesPerLine) {
        // アドレス
        fprintf(out, "%08x: ", static_cast<unsigned>(baseAddr + i));

        // hex部
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < data.size()) {
                fprintf(out, "%02x", data[i + j]);
            } else {
                fprintf(out, "  ");
            }
            // 2バイトごとにスペース
            if (j % 2 == 1) fprintf(out, " ");
        }

        fprintf(out, " ");

        // ASCII部
        for (size_t j = 0; j < bytesPerLine && i + j < data.size(); ++j) {
            uint8_t c = data[i + j];
            fprintf(out, "%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        }

        fprintf(out, "\n");
    }
}

void writeOutput(const std::vector<uint8_t>& data,
                 OutputFormat fmt,
                 const std::string& path) {
    if (fmt == OutputFormat::Binary) {
        // バイナリ出力
        FILE* f = path.empty() ? stdout : fopen(path.c_str(), "wb");
        if (!f)
            throw std::runtime_error("Cannot open output file: " + path
                                     + " (" + strerror(errno) + ")");

        size_t written = fwrite(data.data(), 1, data.size(), f);
        if (!path.empty()) fclose(f);

        if (written != data.size())
            throw std::runtime_error("Write error: wrote "
                                     + std::to_string(written) + " of "
                                     + std::to_string(data.size()) + " bytes");
    } else {
        // hexダンプ出力（テキスト）
        FILE* f = path.empty() ? stdout : fopen(path.c_str(), "w");
        if (!f)
            throw std::runtime_error("Cannot open output file: " + path
                                     + " (" + strerror(errno) + ")");

        writeHexDump(data, f);
        if (!path.empty()) fclose(f);
    }
}
