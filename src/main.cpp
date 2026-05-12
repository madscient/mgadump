/*
 * mgadump - Dump MSX cartridge ROM via MGA (MSXPLAYer Game Adapter)
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2025 madscient
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
#include <unistd.h>
#include <getopt.h>

#include "mga.hpp"
#include "profile.hpp"
#include "dumper.hpp"
#include "output.hpp"

// デフォルトのプロファイルJSONパス（実行ファイルと同じディレクトリに置く想定）
static constexpr const char* DEFAULT_PROFILE_PATH = "mga_profiles.json";

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -p, --port <dev>        COMポートデバイス        (default: /dev/ttyUSB0)\n"
        "  -b, --baud <rate>       ボーレート                (default: 115200)\n"
        "  -s, --slot <n>          スロット番号 1 or 2      (default: 1)\n"
        "  -m, --mapper <name>     マッパープロファイル名   (必須 or default: linear32)\n"
        "  -P, --profiles <file>   プロファイルJSONファイル (default: mga_profiles.json)\n"
        "  -o, --output <file>     出力ファイル              (省略時: stdout)\n"
        "  -f, --format <fmt>      出力形式: bin, hex, text, mot (default: bin)\n"
        "      --list-profiles     プロファイル一覧を表示して終了\n"
        "      --info              デバイス情報を表示して終了\n"
        "      --check             カセット挿入状態を確認して終了\n"
        "  -t, --timeout <ms>      タイムアウト(ms)          (default: 5000)\n"
        "  -v, --verbose           詳細ログをstderrへ出力\n"
        "  -h, --help              このヘルプを表示\n"
        "\n"
        "Examples:\n"
        "  # 32KB フラットROM をダンプ\n"
        "  %s -p /dev/ttyUSB0 -m linear32 -o game.rom\n"
        "\n"
        "  # Konami SCCカートリッジ（256KB）をダンプ\n"
        "  %s -p /dev/ttyUSB0 -m konami_scc -o game.rom\n"
        "\n"
        "  # カスタムJSONを指定してプロファイル一覧を確認\n"
        "  %s -P /etc/mga/profiles.json --list-profiles\n",
        prog, prog, prog, prog);
}

static void showProgress(size_t done, size_t total, size_t bytes, size_t totalBytes) {
    int pct      = static_cast<int>(done * 100 / total);
    int barWidth = 40;
    int filled   = barWidth * pct / 100;

    fprintf(stderr, "\r  [");
    for (int i = 0; i < barWidth; ++i)
        fprintf(stderr, "%c", i < filled ? '#' : '.');
    fprintf(stderr, "] %3d%%  bank %zu/%zu  (%zu KB / %zu KB)",
            pct, done, total, bytes / 1024, totalBytes / 1024);
    fflush(stderr);

    if (done == total) fprintf(stderr, "\n");
}

int main(int argc, char* argv[]) {
    std::string  port        = "/dev/ttyUSB0";
    int          baud        = 115200;
    int          slot        = 1;
    std::string  mapperName  = "linear32";
    std::string  profilePath = DEFAULT_PROFILE_PATH;
    std::string  outFile     = "";
    OutputFormat fmt         = OutputFormat::Binary;
    int          timeout     = 5000;
    bool         verbose     = false;
    bool         doInfo      = false;
    bool         doCheck     = false;
    bool         doList      = false;

    static const struct option longOpts[] = {
        { "port",          required_argument, nullptr, 'p' },
        { "baud",          required_argument, nullptr, 'b' },
        { "slot",          required_argument, nullptr, 's' },
        { "mapper",        required_argument, nullptr, 'm' },
        { "profiles",      required_argument, nullptr, 'P' },
        { "output",        required_argument, nullptr, 'o' },
        { "format",        required_argument, nullptr, 'f' },
        { "timeout",       required_argument, nullptr, 't' },
        { "verbose",       no_argument,       nullptr, 'v' },
        { "help",          no_argument,       nullptr, 'h' },
        { "info",          no_argument,       nullptr, 'I' },
        { "check",         no_argument,       nullptr, 'C' },
        { "list-profiles", no_argument,       nullptr, 'L' },
        { nullptr,         0,                 nullptr,  0  },
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "p:b:s:m:P:o:f:t:vhICL", longOpts, &idx)) != -1) {
        switch (opt) {
            case 'p': port        = optarg; break;
            case 'b': baud        = std::stoi(optarg); break;
            case 's': slot        = std::stoi(optarg); break;
            case 'm': mapperName  = optarg; break;
            case 'P': profilePath = optarg; break;
            case 'o': outFile     = optarg; break;
            case 'f':
                try { fmt = parseFormat(optarg); }
                catch (const std::exception& e) { fprintf(stderr, "Error: %s\n", e.what()); return 1; }
                break;
            case 't': timeout     = std::stoi(optarg); break;
            case 'v': verbose     = true; break;
            case 'I': doInfo      = true; break;
            case 'C': doCheck     = true; break;
            case 'L': doList      = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default:  printUsage(argv[0]); return 1;
        }
    }

    // --- プロファイルをJSONからロード ---
    // --list-profiles / ダンプ実行ともにJSONが必要なのでここで一括ロード
    std::vector<CartProfile> profiles;
    try {
        if (verbose)
            fprintf(stderr, "[mgadump] Loading profiles from %s\n", profilePath.c_str());
        profiles = loadProfiles(profilePath);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    // --list-profiles: デバイス接続不要
    if (doList) {
        fprintf(stderr, "Profile file: %s\n\n", profilePath.c_str());
        printProfiles(profiles);
        return 0;
    }

    // バリデーション
    if (slot != 1 && slot != 2) {
        fprintf(stderr, "Error: slot must be 1 or 2\n");
        return 1;
    }

    try {
        // --- プロファイル検索（見つからなければ候補一覧付きエラー） ---
        const CartProfile& profile = findProfile(profiles, mapperName);

        // --info / --check はデバイス接続が必要だがダンプは不要
        Mga mga(port, baud);
        mga.setTimeout(timeout);

        if (verbose)
            fprintf(stderr, "[mgadump] Opening %s at %d baud\n", port.c_str(), baud);
        mga.open();

        if (doInfo) {
            fprintf(stderr, "=== Hardware Version ===\n");
            printf("%s", mga.hardwareVersion().c_str());
            fprintf(stderr, "=== Hardware Info ===\n");
            printf("%s", mga.hardwareInfo().c_str());
            return 0;
        }

        if (doCheck) {
            std::string status = mga.slotCheck();
            printf("Slot status (SLOT3210): %s\n", status.c_str());
            for (int i = 0; i < 4 && i < (int)status.size(); ++i)
                if (status[3 - i] == '1')
                    printf("  SLOT%d: cartridge inserted\n", i);
            return 0;
        }

        // --- ダンプ実行 ---
        fprintf(stderr, "[mgadump] profile=%-15s  size=%u KB  slot=%d  file=%s\n",
                profile.name.c_str(), profile.totalSize / 1024, slot,
                profilePath.c_str());

        ProgressCallback progress = isatty(fileno(stderr)) ? showProgress : nullptr;

        auto rom = dumpByProfile(mga, profile, slot, progress);

        writeOutput(rom, fmt, outFile);

        if (!outFile.empty())
            fprintf(stderr, "[mgadump] Written to %s (%zu bytes)\n",
                    outFile.c_str(), rom.size());

    } catch (const std::exception& e) {
        fprintf(stderr, "\nError: %s\n", e.what());
        return 1;
    }

    return 0;
}
