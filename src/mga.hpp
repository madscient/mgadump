#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

// MGA通信エラー
class MgaError : public std::runtime_error {
public:
    explicit MgaError(const std::string& msg) : std::runtime_error(msg) {}
};

// シリアルポート + MGAプロトコル層
class Mga {
public:
    // ボーレートはデフォルト115200
    explicit Mga(const std::string& port, int baud = 115200);
    ~Mga();

    // 接続・切断
    void open();
    void close();
    bool isOpen() const { return fd_ >= 0; }

    // --- 基本コマンド ---
    // デバイス情報取得
    std::string hardwareVersion();   // HVER
    std::string hardwareInfo();      // HINF

    // スロット制御
    void slotPowerOn();              // SPON
    void slotPowerOff();             // SPOFF
    void slotReset();                // SRST
    void slotSelect(int slot);       // SSEL

    // カセット検出
    std::string slotCheck();         // SCHK

    // --- ダンプ用高レベルAPI ---
    // スロット→バッファ→PC転送（カートリッジ読み出し）
    // address: スロットアドレス, length: バイト数, slot: 1 or 2
    std::vector<uint8_t> dumpSlot(uint16_t address, uint16_t length, int slot = 1);

    // スロット1バイト読み出し
    uint8_t readByte(uint16_t address, int slot = 1);  // SMRD

    // スロット1バイト書き込み（バンクスイッチレジスタ操作に使用）
    void writeSlotByte(uint16_t address, uint8_t data, int slot = 1);  // SMWR

    // バッファ内容をバイナリで取得
    std::vector<uint8_t> bufferReceive(uint16_t bufAddr, uint16_t length);  // BSND

    // バッファクリア
    void bufferClear(uint16_t bufAddr = 0, uint16_t length = 0, uint8_t fill = 0xFF);  // BCLR

    // スロット→バッファ転送
    void slotToBuffer(uint16_t slotAddr, uint16_t length, uint16_t bufAddr = 0, int slot = 1);  // SMTR

    // タイムアウト設定（ms）
    void setTimeout(int ms) { timeoutMs_ = ms; }

private:
    std::string port_;
    int baud_;
    int fd_ = -1;
    int timeoutMs_ = 5000;

    // 低レベル送受信
    void sendLine(const std::string& cmd);
    std::string recvLine();
    std::vector<uint8_t> recvBinary(size_t length);

    // コマンド送信→OK/FAIL確認
    // レスポンス行（OKの前の行）を返す
    std::vector<std::string> sendCommand(const std::string& cmd);

    // バイナリ受信後にOK/FAILを確認
    void expectOk();
};
