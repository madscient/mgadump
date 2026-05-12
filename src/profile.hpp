#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

// ---------------------------------------------------------------------------
// バンク切り替えの1ステップ（SMWRで書き込む）
// ---------------------------------------------------------------------------
struct BankSwitch {
    uint16_t regAddr;
    uint8_t  value;
};

// ---------------------------------------------------------------------------
// 1バンク分のダンプ指示
// ---------------------------------------------------------------------------
struct BankRegion {
    std::vector<BankSwitch> switches;  // 空ならバンクスイッチなし
    uint16_t slotAddr;
    uint16_t length;
    uint32_t outputOffset;
};

// ---------------------------------------------------------------------------
// カートリッジプロファイル
// ---------------------------------------------------------------------------
struct CartProfile {
    std::string name;
    std::string description;
    uint32_t    totalSize;
    std::vector<BankRegion> banks;
};

// ---------------------------------------------------------------------------
// JSONファイルからプロファイルをロードする
//
// jsonPath: mga_profiles.json のパス
// 戻り値:  ファイル内の全プロファイルのリスト
//
// 失敗時は std::runtime_error:
//   - ファイルが開けない
//   - JSONパースエラー
//   - 必須フィールド欠落・型不正
// ---------------------------------------------------------------------------
std::vector<CartProfile> loadProfiles(const std::string& jsonPath);

// ---------------------------------------------------------------------------
// 名前でプロファイルを検索
//
// 見つからない場合は利用可能なプロファイル名を列挙したエラーメッセージを
// 含む std::runtime_error を投げる
// ---------------------------------------------------------------------------
const CartProfile& findProfile(const std::vector<CartProfile>& profiles,
                               const std::string& name);

// プロファイル一覧を stdout へ表示（--list-profiles 用）
void printProfiles(const std::vector<CartProfile>& profiles);
