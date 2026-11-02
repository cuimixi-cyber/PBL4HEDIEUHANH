#ifndef NETMONITOR_H
#define NETMONITOR_H

/* NetMonitor: module MỚI, thuần C++17, KHÔNG phụ thuộc Qt và KHÔNG sửa code gốc
 * trong src/. Tách riêng khỏi giao diện để có thể test độc lập bằng g++.
 *
 * Nguồn dữ liệu (đều là cơ chế chuẩn của Linux):
 *   - /proc/net/dev            : byte cộng dồn của từng card mạng -> tốc độ thật (chính xác)
 *   - /proc/net/{tcp,tcp6,udp,udp6} : danh sách socket (địa chỉ, cổng, trạng thái, inode)
 *   - thu muc /proc/[pid]/fd   : symlink "socket:[inode]" -> biết socket thuộc tiến trình nào
 *   - lệnh `ss -tinp`          : byte đã gửi/nhận của TỪNG kết nối TCP (ước lượng theo ứng dụng)
 *
 * Giới hạn trung thực (cần nêu khi bảo vệ đồ án):
 *   - Muốn thấy socket của tiến trình thuộc user khác phải chạy bằng root.
 *   - Tốc độ theo từng ứng dụng chỉ tính được cho TCP (UDP/QUIC không có bộ đếm byte
 *     theo socket trong /proc). Tốc độ TỔNG theo card mạng thì chính xác cho mọi giao thức. */

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace netmon {

enum class AddrScope { Unknown, Any, Loopback, LinkLocal, Multicast, Private, Public };
const char *scopeToString(AddrScope s);

struct InterfaceStat {          // số liệu cộng dồn đọc từ /proc/net/dev
    std::string name;
    uint64_t rxBytes = 0, txBytes = 0;
    uint64_t rxPackets = 0, txPackets = 0;
    uint64_t rxErrors = 0, txErrors = 0;
    uint64_t rxDropped = 0, txDropped = 0;
};

struct InterfaceRate {          // tốc độ tức thời của 1 card mạng
    std::string name;
    double rxRate = 0.0, txRate = 0.0;      // byte/giây
    uint64_t rxTotal = 0, txTotal = 0;      // tổng từ lúc boot
    bool isLoopback = false;
};

struct Connection {
    std::string proto;                      // tcp, tcp6, udp, udp6
    std::string localIp;  uint16_t localPort = 0;
    std::string remoteIp; uint16_t remotePort = 0;
    std::string state;                      // ESTABLISHED, LISTEN, TIME_WAIT, UNCONN...
    uint64_t inode = 0;
    unsigned uid = 0;
    int pid = -1;                           // -1: không xác định (thiếu quyền, hoặc socket của kernel)
    std::string procName;
    AddrScope remoteScope = AddrScope::Unknown;
    uint64_t bytesSent = 0, bytesRecv = 0;  // cộng dồn của kết nối (TCP, từ ss)
    bool hasTraffic = false;
};

struct SsEntry {                        // 1 kết nối TCP đọc từ output của `ss -tinp`
    int pid = -1;
    std::string name;
    std::string local, remote;
    uint64_t bytesSent = 0, bytesRecv = 0;
};

struct ProcessNetUsage {                // tổng hợp theo TIẾN TRÌNH
    int pid = 0;                        // 0 = không xác định
    std::string name;
    int total = 0, established = 0, listening = 0, udp = 0;
    double sendRate = 0.0, recvRate = 0.0;      // byte/giây, chỉ TCP
    uint64_t sendTotal = 0, recvTotal = 0;
    bool hasTraffic = false;
    bool hasInternet = false;           // có kết nối tới địa chỉ Internet (public)
};

class NetMonitor {
public:
    NetMonitor() = default;

    /* Nhẹ (đọc 1 file): gọi mỗi chu kỳ làm mới. */
    void sampleInterfaces();
    /* Nặng hơn (quét /proc/[pid]/fd của mọi tiến trình + chạy ss): chỉ gọi khi cần hiển thị. */
    void sampleConnections(bool withTraffic = true);

    const std::vector<InterfaceRate> &interfaces() const { return ifaceRates_; }
    double totalRxRate() const { return totalRx_; }     // không tính loopback
    double totalTxRate() const { return totalTx_; }
    uint64_t sessionRxBytes() const { return sessionRx_; }
    uint64_t sessionTxBytes() const { return sessionTx_; }
    double peakRxRate() const { return peakRx_; }
    double peakTxRate() const { return peakTx_; }

    const std::vector<Connection> &connections() const { return conns_; }
    const std::vector<ProcessNetUsage> &processUsage() const { return usage_; }
    bool trafficAvailable() const { return trafficAvailable_; }
    int unresolvedSocketCount() const { return unresolved_; }

    // ---- Các hàm thuần (static) để test bằng dữ liệu giả lập ----
    static std::vector<InterfaceStat> parseNetDev(const std::string &text);
    static std::vector<Connection> parseProcNet(const std::string &text, const std::string &proto);
    static std::vector<SsEntry> parseSsOutput(const std::string &text);
    static AddrScope classifyAddress(const std::string &ip);
    static std::string serviceName(uint16_t port);
    static std::string normalizeEndpoint(const std::string &ssEndpoint);

private:
    // interfaces
    std::vector<InterfaceStat> prevIface_;
    bool hasPrevIface_ = false;
    std::chrono::steady_clock::time_point prevIfaceTime_;
    std::vector<InterfaceRate> ifaceRates_;
    double totalRx_ = 0.0, totalTx_ = 0.0, peakRx_ = 0.0, peakTx_ = 0.0;
    uint64_t sessionRx_ = 0, sessionTx_ = 0;

    // connections
    std::vector<Connection> conns_;
    std::vector<ProcessNetUsage> usage_;
    bool trafficAvailable_ = false;
    int unresolved_ = 0;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> prevSs_; // key -> (sent, recv)
    bool hasPrevSs_ = false;
    std::chrono::steady_clock::time_point prevConnTime_;
};

} // namespace netmon

#endif
