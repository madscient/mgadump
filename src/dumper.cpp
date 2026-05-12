#include "dumper.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>

std::vector<uint8_t> dumpByProfile(Mga& mga,
                                   const CartProfile& profile,
                                   int slot,
                                   ProgressCallback onProgress) {
    // 出力バッファを 0xFF で初期化（未読領域はFFが残る）
    std::vector<uint8_t> rom(profile.totalSize, 0xFF);

    mga.slotPowerOn();

    try {
        size_t totalBanks = profile.banks.size();
        size_t bytesRead  = 0;

        for (size_t i = 0; i < totalBanks; ++i) {
            const BankRegion& bank = profile.banks[i];

            // バンクスイッチ（SMWR で各レジスタに値を書き込む）
            for (auto& sw : bank.switches) {
                mga.writeSlotByte(sw.regAddr, sw.value, slot);
            }

            // スロット→内部バッファ転送（SMTR）
            mga.slotToBuffer(bank.slotAddr, bank.length, 0x0000, slot);

            // バッファ→PCバイナリ受信（BSND）
            auto chunk = mga.bufferReceive(0x0000, bank.length);

            // 出力バッファの正しいオフセットへコピー
            if (bank.outputOffset + chunk.size() > rom.size()) {
                // プロファイル設定ミスの場合はトリム
                size_t copyLen = rom.size() - bank.outputOffset;
                std::memcpy(rom.data() + bank.outputOffset, chunk.data(), copyLen);
            } else {
                std::memcpy(rom.data() + bank.outputOffset, chunk.data(), chunk.size());
            }

            bytesRead += chunk.size();

            if (onProgress) {
                onProgress(i + 1, totalBanks, bytesRead, profile.totalSize);
            }
        }

        mga.slotPowerOff();

    } catch (...) {
        mga.slotPowerOff();
        throw;
    }

    return rom;
}
