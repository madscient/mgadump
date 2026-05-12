#pragma once
#include "mga.hpp"
#include "profile.hpp"
#include <functional>

// 進捗コールバック型: (完了バンク数, 全バンク数, 受信バイト数, 総バイト数)
using ProgressCallback = std::function<void(size_t done, size_t total,
                                            size_t bytes, size_t totalBytes)>;

// プロファイルに従ってカートリッジを全バンクダンプする
// 戻り値: totalSize バイトのバッファ（未読領域は 0xFF 埋め）
std::vector<uint8_t> dumpByProfile(Mga& mga,
                                   const CartProfile& profile,
                                   int slot = 1,
                                   ProgressCallback onProgress = nullptr);
