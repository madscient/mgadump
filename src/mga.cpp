#include "mga.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

// ボーレート値→termios定数変換
static speed_t toBaudRate(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:
            throw MgaError("Unsupported baud rate: " + std::to_string(baud));
    }
}

Mga::Mga(const std::string& port, int baud)
    : port_(port), baud_(baud) {}

Mga::~Mga() {
    close();
}

void Mga::open() {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
        throw MgaError("Cannot open port: " + port_ + " (" + strerror(errno) + ")");

    // ブロッキングモードに戻す
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0)
        throw MgaError("tcgetattr failed: " + std::string(strerror(errno)));

    speed_t spd = toBaudRate(baud_);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;  // no hardware flow control

    // VMIN=0, VTIME=0: 完全非ブロッキング（selectで制御）
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        throw MgaError("tcsetattr failed: " + std::string(strerror(errno)));

    tcflush(fd_, TCIOFLUSH);
}

void Mga::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// --- 低レベル ---

void Mga::sendLine(const std::string& cmd) {
    std::string line = cmd + "\r\n";
    ssize_t written = ::write(fd_, line.c_str(), line.size());
    if (written < 0)
        throw MgaError("Write failed: " + std::string(strerror(errno)));
}

// 1行（\nまで）を受信。タイムアウト対応。
std::string Mga::recvLine() {
    std::string line;
    auto deadline = timeoutMs_;  // ms残量（簡易実装）

    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);

        struct timeval tv{ deadline / 1000, (deadline % 1000) * 1000 };
        int ret = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0)
            throw MgaError("select failed: " + std::string(strerror(errno)));
        if (ret == 0)
            throw MgaError("Timeout waiting for response");

        char c;
        ssize_t n = ::read(fd_, &c, 1);
        if (n <= 0) continue;

        if (c == '\r') continue;  // CR無視
        if (c == '\n') break;     // LFで行終端
        line += c;
    }
    return line;
}

// 指定バイト数のバイナリを受信
std::vector<uint8_t> Mga::recvBinary(size_t length) {
    std::vector<uint8_t> buf(length);
    size_t received = 0;

    while (received < length) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        struct timeval tv{ timeoutMs_ / 1000, (timeoutMs_ % 1000) * 1000 };
        int ret = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0)
            throw MgaError("select failed: " + std::string(strerror(errno)));
        if (ret == 0)
            throw MgaError("Timeout receiving binary data (got "
                           + std::to_string(received) + "/" + std::to_string(length) + " bytes)");

        ssize_t n = ::read(fd_, buf.data() + received, length - received);
        if (n > 0) received += n;
    }
    return buf;
}

// コマンド送信→OK/FAIL確認。OK前の行をすべて返す。
std::vector<std::string> Mga::sendCommand(const std::string& cmd) {
    sendLine(cmd);
    std::vector<std::string> lines;

    while (true) {
        std::string line = recvLine();
        if (line == "OK") return lines;
        if (line == "FAIL")
            throw MgaError("Command failed: " + cmd);
        lines.push_back(line);
    }
}

void Mga::expectOk() {
    std::string line;
    // バイナリ後はOK/FAILが続く（displayFlag=true時）
    // 空行やゴミをスキップしてOK/FAILを待つ
    for (int i = 0; i < 10; ++i) {
        line = recvLine();
        if (line == "OK") return;
        if (line == "FAIL")
            throw MgaError("Command returned FAIL after binary transfer");
        // 空行などは無視して続ける
    }
    throw MgaError("No OK/FAIL received after binary transfer");
}

// --- 高レベルAPI ---

std::string Mga::hardwareVersion() {
    auto lines = sendCommand("HVER");
    std::ostringstream oss;
    for (auto& l : lines) oss << l << "\n";
    return oss.str();
}

std::string Mga::hardwareInfo() {
    auto lines = sendCommand("HINF");
    std::ostringstream oss;
    for (auto& l : lines) oss << l << "\n";
    return oss.str();
}

void Mga::slotPowerOn() {
    sendCommand("SPON");
}

void Mga::slotPowerOff() {
    sendCommand("SPOFF");
}

void Mga::slotReset() {
    sendCommand("SRST");
}

void Mga::slotSelect(int slot) {
    sendCommand("SSEL," + std::to_string(slot));
}

std::string Mga::slotCheck() {
    auto lines = sendCommand("SCHK");
    return lines.empty() ? "" : lines[0];
}

void Mga::bufferClear(uint16_t bufAddr, uint16_t length, uint8_t fill) {
    std::ostringstream cmd;
    cmd << "BCLR,"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << bufAddr << ","
        << std::setw(4) << length << ","
        << std::setw(2) << static_cast<unsigned>(fill);
    sendCommand(cmd.str());
}

void Mga::slotToBuffer(uint16_t slotAddr, uint16_t length, uint16_t bufAddr, int slot) {
    std::ostringstream cmd;
    cmd << "SMTR,"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << slotAddr << ","
        << std::setw(4) << length << ","
        << std::setw(4) << bufAddr << ","
        << std::dec << slot;
    sendCommand(cmd.str());
}

std::vector<uint8_t> Mga::bufferReceive(uint16_t bufAddr, uint16_t length) {
    std::ostringstream cmd;
    cmd << "BSND,"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << bufAddr << ","
        << std::setw(4) << length;
    sendLine(cmd.str());

    auto data = recvBinary(length);
    expectOk();
    return data;
}

void Mga::writeSlotByte(uint16_t address, uint8_t data, int slot) {
    std::ostringstream cmd;
    cmd << "SMWR,"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << address << ","
        << std::setw(2) << static_cast<unsigned>(data) << ","
        << std::dec << slot;
    sendCommand(cmd.str());
}

uint8_t Mga::readByte(uint16_t address, int slot) {
    std::ostringstream cmd;
    cmd << "SMRD,"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << address << ","
        << std::dec << slot;
    auto lines = sendCommand(cmd.str());
    // 応答: "XXXX : YY"
    if (lines.empty())
        throw MgaError("No data from SMRD");
    // "addr : data" を解析
    std::string& resp = lines[0];
    auto colon = resp.find(':');
    if (colon == std::string::npos)
        throw MgaError("Unexpected SMRD response: " + resp);
    std::string hexVal = resp.substr(colon + 2);
    // 空白除去
    hexVal.erase(std::remove_if(hexVal.begin(), hexVal.end(), ::isspace), hexVal.end());
    return static_cast<uint8_t>(std::stoul(hexVal, nullptr, 16));
}

// カートリッジ丸ごとダンプ（典型的なユースケース）
std::vector<uint8_t> Mga::dumpSlot(uint16_t address, uint16_t length, int slot) {
    slotPowerOn();
    try {
        slotToBuffer(address, length, 0x0000, slot);
        auto data = bufferReceive(0x0000, length);
        slotPowerOff();
        return data;
    } catch (...) {
        slotPowerOff();
        throw;
    }
}
