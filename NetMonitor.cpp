#include "NetMonitor.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace netmon {

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------ tiện ích nhỏ

static std::string readFile(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

static bool allDigits(const char *s) {
    if (!*s) return false;
    for (; *s; s++) if (!std::isdigit((unsigned char)*s)) return false;
    return true;
}

static std::string readComm(int pid) {
    return trim(readFile("/proc/" + std::to_string(pid) + "/comm"));
}

// Chạy 1 lệnh shell, gom stdout. Trả về exit status (0 = thành công), -1 nếu không chạy được.
static int runCommand(const char *cmd, std::string &out) {
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    int st = pclose(p);
    if (st == -1) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

const char *scopeToString(AddrScope s) {
    switch (s) {
        case AddrScope::Any:       return "Bất kỳ (*)";
        case AddrScope::Loopback:  return "Loopback";
        case AddrScope::LinkLocal: return "Link-local";
        case AddrScope::Multicast: return "Multicast";
        case AddrScope::Private:   return "Mạng nội bộ (LAN)";
        case AddrScope::Public:    return "Internet";
        default:                   return "?";
    }
}

AddrScope NetMonitor::classifyAddress(const std::string &ipIn) {
    std::string ip = ipIn;
    std::transform(ip.begin(), ip.end(), ip.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (ip.empty()) return AddrScope::Unknown;

    if (ip.find(':') != std::string::npos) {                 // IPv6
        if (ip == "::") return AddrScope::Any;
        if (ip == "::1") return AddrScope::Loopback;
        if (ip.rfind("::ffff:", 0) == 0 && ip.find('.') != std::string::npos)
            return classifyAddress(ip.substr(7));            // IPv4-mapped: xét theo phần IPv4
        if (ip.rfind("fe8", 0) == 0 || ip.rfind("fe9", 0) == 0 ||
            ip.rfind("fea", 0) == 0 || ip.rfind("feb", 0) == 0) return AddrScope::LinkLocal;
        if (ip.rfind("ff", 0) == 0) return AddrScope::Multicast;
        if (ip.rfind("fc", 0) == 0 || ip.rfind("fd", 0) == 0) return AddrScope::Private;
        return AddrScope::Public;
    }

    unsigned a, b, c, d;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return AddrScope::Unknown;
    if (a == 0) return AddrScope::Any;
    if (a == 127) return AddrScope::Loopback;
    if (a == 10) return AddrScope::Private;
    if (a == 172 && b >= 16 && b <= 31) return AddrScope::Private;
    if (a == 192 && b == 168) return AddrScope::Private;
    if (a == 100 && b >= 64 && b <= 127) return AddrScope::Private;   // CGNAT / Tailscale
    if (a == 169 && b == 254) return AddrScope::LinkLocal;
    if (a >= 224 && a <= 239) return AddrScope::Multicast;
    return AddrScope::Public;
}

std::string NetMonitor::serviceName(uint16_t port) {
    switch (port) {
        case 20: case 21: return "FTP";
        case 22:    return "SSH";
        case 23:    return "Telnet";
        case 25:    return "SMTP";
        case 53:    return "DNS";
        case 67: case 68: return "DHCP";
        case 80:    return "HTTP";
        case 110:   return "POP3";
        case 123:   return "NTP";
        case 143:   return "IMAP";
        case 443:   return "HTTPS";
        case 465: case 587: return "SMTP (TLS)";
        case 631:   return "IPP (in ấn)";
        case 993:   return "IMAPS";
        case 995:   return "POP3S";
        case 1900:  return "SSDP";
        case 3306:  return "MySQL";
        case 3389:  return "RDP";
        case 5353:  return "mDNS";
        case 5432:  return "PostgreSQL";
        case 5900:  return "VNC";
        case 6379:  return "Redis";
        case 8080:  return "HTTP (cổng phụ)";
        case 8443:  return "HTTPS (cổng phụ)";
        case 27017: return "MongoDB";
        default:    return "";
    }
}

// ------------------------------------------------------------ /proc/net/dev

std::vector<InterfaceStat> NetMonitor::parseNetDev(const std::string &text) {
    std::vector<InterfaceStat> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;            // 2 dòng header không có ':'
        InterfaceStat s;
        s.name = trim(line.substr(0, colon));
        std::istringstream ls(line.substr(colon + 1));
        uint64_t v[16] = {0};
        int n = 0;
        while (n < 16 && (ls >> v[n])) n++;
        if (n < 10 || s.name.empty()) continue;
        s.rxBytes = v[0]; s.rxPackets = v[1]; s.rxErrors = v[2]; s.rxDropped = v[3];
        s.txBytes = v[8]; s.txPackets = v[9];
        if (n > 10) s.txErrors = v[10];
        if (n > 11) s.txDropped = v[11];
        out.push_back(s);
    }
    return out;
}

void NetMonitor::sampleInterfaces() {
    auto now = Clock::now();
    std::vector<InterfaceStat> cur = parseNetDev(readFile("/proc/net/dev"));
    double dt = hasPrevIface_ ? std::chrono::duration<double>(now - prevIfaceTime_).count() : 0.0;

    std::unordered_map<std::string, const InterfaceStat *> prevMap;
    for (const auto &p : prevIface_) prevMap[p.name] = &p;

    ifaceRates_.clear();
    double rx = 0.0, tx = 0.0;
    for (const auto &c : cur) {
        InterfaceRate r;
        r.name = c.name;
        r.rxTotal = c.rxBytes;
        r.txTotal = c.txBytes;
        r.isLoopback = (c.name == "lo");
        auto it = prevMap.find(c.name);
        if (it != prevMap.end() && dt > 0.0) {
            uint64_t dRx = c.rxBytes >= it->second->rxBytes ? c.rxBytes - it->second->rxBytes : 0;
            uint64_t dTx = c.txBytes >= it->second->txBytes ? c.txBytes - it->second->txBytes : 0;
            r.rxRate = (double)dRx / dt;
            r.txRate = (double)dTx / dt;
            if (!r.isLoopback) { sessionRx_ += dRx; sessionTx_ += dTx; }
        }
        if (!r.isLoopback) { rx += r.rxRate; tx += r.txRate; }
        ifaceRates_.push_back(r);
    }
    totalRx_ = rx;
    totalTx_ = tx;
    if (rx > peakRx_) peakRx_ = rx;
    if (tx > peakTx_) peakTx_ = tx;

    prevIface_ = cur;
    prevIfaceTime_ = now;
    hasPrevIface_ = true;
}

// ------------------------------------------------------------ /proc/net/{tcp,udp}

static std::string hexToIp(const std::string &hex, bool v6) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (!v6 && hex.size() == 8) {
        // Kernel in %08X của giá trị 32-bit theo thứ tự byte của máy -> gán thẳng vào s_addr
        struct in_addr a;
        a.s_addr = (uint32_t)std::strtoul(hex.c_str(), nullptr, 16);
        inet_ntop(AF_INET, &a, buf, sizeof(buf));
        return buf;
    }
    if (v6 && hex.size() == 32) {
        struct in6_addr a;
        for (int i = 0; i < 4; i++) {
            uint32_t w = (uint32_t)std::strtoul(hex.substr(i * 8, 8).c_str(), nullptr, 16);
            std::memcpy(&a.s6_addr[i * 4], &w, 4);
        }
        inet_ntop(AF_INET6, &a, buf, sizeof(buf));
        return buf;
    }
    return hex;
}

static std::string tcpStateName(const std::string &hexIn) {
    std::string h = hexIn;
    for (auto &c : h) c = (char)std::toupper((unsigned char)c);
    if (h == "01") return "ESTABLISHED";
    if (h == "02") return "SYN_SENT";
    if (h == "03") return "SYN_RECV";
    if (h == "04") return "FIN_WAIT1";
    if (h == "05") return "FIN_WAIT2";
    if (h == "06") return "TIME_WAIT";
    if (h == "07") return "CLOSE";
    if (h == "08") return "CLOSE_WAIT";
    if (h == "09") return "LAST_ACK";
    if (h == "0A") return "LISTEN";
    if (h == "0B") return "CLOSING";
    return "UNKNOWN(" + h + ")";
}

std::vector<Connection> NetMonitor::parseProcNet(const std::string &text, const std::string &proto) {
    std::vector<Connection> out;
    const bool v6 = !proto.empty() && proto.back() == '6';
    const bool isTcp = proto.rfind("tcp", 0) == 0;

    std::istringstream in(text);
    std::string line;
    std::getline(in, line);                                  // bỏ dòng header
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string sl, local, remote, st, txrx, trwhen, retr, timeout;
        unsigned uid = 0;
        uint64_t inode = 0;
        if (!(ls >> sl >> local >> remote >> st >> txrx >> trwhen >> retr >> uid >> timeout >> inode)) continue;

        size_t lc = local.find(':'), rc = remote.find(':');
        if (lc == std::string::npos || rc == std::string::npos) continue;

        Connection c;
        c.proto = proto;
        c.localIp = hexToIp(local.substr(0, lc), v6);
        c.localPort = (uint16_t)std::strtoul(local.substr(lc + 1).c_str(), nullptr, 16);
        c.remoteIp = hexToIp(remote.substr(0, rc), v6);
        c.remotePort = (uint16_t)std::strtoul(remote.substr(rc + 1).c_str(), nullptr, 16);
        if (isTcp) {
            c.state = tcpStateName(st);
        } else {
            c.state = (st == "07") ? "UNCONN" : (st == "01" ? "CONNECTED" : "UDP(" + st + ")");
        }
        c.inode = inode;
        c.uid = uid;
        c.remoteScope = classifyAddress(c.remoteIp);
        out.push_back(c);
    }
    return out;
}

// socket inode -> PID nhỏ nhất đang giữ socket đó (cần quyền đọc /proc/[pid]/fd)
static std::unordered_map<uint64_t, int> buildInodeMap() {
    std::unordered_map<uint64_t, int> map;
    std::vector<int> pids;
    DIR *d = opendir("/proc");
    if (!d) return map;
    while (struct dirent *e = readdir(d)) {
        if (allDigits(e->d_name)) pids.push_back(std::atoi(e->d_name));
    }
    closedir(d);
    std::sort(pids.begin(), pids.end());

    for (int pid : pids) {
        std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
        DIR *fd = opendir(fdDir.c_str());
        if (!fd) continue;                                   // thiếu quyền hoặc tiến trình vừa thoát
        while (struct dirent *e = readdir(fd)) {
            if (e->d_name[0] == '.') continue;
            std::string path = fdDir + "/" + e->d_name;
            char buf[64];
            ssize_t n = readlink(path.c_str(), buf, sizeof(buf) - 1);
            if (n <= 0) continue;
            buf[n] = '\0';
            if (std::strncmp(buf, "socket:[", 8) == 0) {
                uint64_t inode = std::strtoull(buf + 8, nullptr, 10);
                map.emplace(inode, pid);                     // emplace: giữ PID đầu tiên (nhỏ nhất)
            }
        }
        closedir(fd);
    }
    return map;
}

// ------------------------------------------------------------ ss -tinp

static bool extractCounter(const std::string &line, const char *key, uint64_t &out) {
    size_t pos = line.find(key);
    while (pos != std::string::npos) {
        if (pos == 0 || line[pos - 1] == ' ' || line[pos - 1] == '\t') {
            const char *p = line.c_str() + pos + std::strlen(key);
            if (std::isdigit((unsigned char)*p)) {
                out = std::strtoull(p, nullptr, 10);
                return true;
            }
        }
        pos = line.find(key, pos + 1);
    }
    return false;
}

std::string NetMonitor::normalizeEndpoint(const std::string &s) {
    std::string r = s;
    size_t pct = r.find('%');                                // "127.0.0.53%lo:53" -> bỏ "%lo"
    if (pct != std::string::npos) {
        size_t end = r.find_first_of("]:", pct);
        r.erase(pct, end == std::string::npos ? std::string::npos : end - pct);
    }
    r.erase(std::remove(r.begin(), r.end(), '['), r.end());
    r.erase(std::remove(r.begin(), r.end(), ']'), r.end());
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

static std::string endpointKey(const std::string &ip, uint16_t port) {
    return NetMonitor::normalizeEndpoint(ip + ":" + std::to_string(port));
}

std::vector<SsEntry> NetMonitor::parseSsOutput(const std::string &text) {
    std::vector<SsEntry> out;
    std::istringstream in(text);
    std::string line;
    SsEntry cur;
    bool have = false;
    auto flush = [&]() { if (have) out.push_back(cur); have = false; cur = SsEntry(); };

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == ' ' || line[0] == '\t') {             // dòng thông tin mở rộng của kết nối trước
            if (!have) continue;
            uint64_t v;
            if (extractCounter(line, "bytes_sent:", v)) cur.bytesSent = v;
            else if (extractCounter(line, "bytes_acked:", v)) cur.bytesSent = v;
            if (extractCounter(line, "bytes_received:", v)) cur.bytesRecv = v;
            continue;
        }
        flush();
        if (line.rfind("State", 0) == 0 || line.rfind("Netid", 0) == 0) continue;   // header

        std::istringstream ls(line);
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t) tok.push_back(t);
        size_t o = (!tok.empty() && (tok[0] == "tcp" || tok[0] == "tcp6" || tok[0] == "udp" || tok[0] == "udp6")) ? 1 : 0;
        if (tok.size() < 5 + o) continue;
        cur.local = tok[3 + o];
        cur.remote = tok[4 + o];

        size_t u = line.find("users:((");
        if (u != std::string::npos) {                        // users:(("firefox",pid=1234,fd=50),...)
            size_t q1 = line.find('"', u);
            size_t q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
            if (q2 != std::string::npos) cur.name = line.substr(q1 + 1, q2 - q1 - 1);
            size_t p = line.find("pid=", u);
            if (p != std::string::npos) cur.pid = std::atoi(line.c_str() + p + 4);
        }
        uint64_t v;
        if (extractCounter(line, "bytes_sent:", v)) cur.bytesSent = v;
        if (extractCounter(line, "bytes_received:", v)) cur.bytesRecv = v;
        have = true;
    }
    flush();
    return out;
}

// ------------------------------------------------------------ lấy mẫu kết nối

void NetMonitor::sampleConnections(bool withTraffic) {
    auto now = Clock::now();

    std::vector<Connection> all;
    for (const char *proto : {"tcp", "tcp6", "udp", "udp6"}) {
        auto v = parseProcNet(readFile(std::string("/proc/net/") + proto), proto);
        all.insert(all.end(), v.begin(), v.end());
    }

    // gắn PID cho từng socket
    auto inodeMap = buildInodeMap();
    std::unordered_map<int, std::string> names;
    unresolved_ = 0;
    for (auto &c : all) {
        if (c.inode == 0) continue;                          // socket của kernel (vd TIME_WAIT) -> không có chủ
        auto it = inodeMap.find(c.inode);
        if (it == inodeMap.end()) { unresolved_++; continue; }
        c.pid = it->second;
        auto nit = names.find(c.pid);
        if (nit == names.end()) nit = names.emplace(c.pid, readComm(c.pid)).first;
        c.procName = nit->second;
    }

    // lưu lượng TCP theo kết nối (qua ss)
    std::unordered_map<int, std::pair<double, double>> pidRate;   // pid -> (nhận/s, gửi/s)
    if (withTraffic) {
        std::string out;
        int status = runCommand("ss -tinp 2>/dev/null", out);
        trafficAvailable_ = (status == 0);
        if (trafficAvailable_) {
            auto entries = parseSsOutput(out);
            double dt = hasPrevSs_ ? std::chrono::duration<double>(now - prevConnTime_).count() : 0.0;

            std::unordered_map<std::string, Connection *> byKey;
            for (auto &c : all) {
                if (c.proto.rfind("tcp", 0) != 0) continue;
                byKey[endpointKey(c.localIp, c.localPort) + ">" + endpointKey(c.remoteIp, c.remotePort)] = &c;
            }

            std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> curSs;
            for (const auto &e : entries) {
                std::string key = normalizeEndpoint(e.local) + ">" + normalizeEndpoint(e.remote);
                curSs[key] = {e.bytesSent, e.bytesRecv};
                auto cit = byKey.find(key);
                Connection *conn = (cit != byKey.end()) ? cit->second : nullptr;
                if (conn) {
                    conn->bytesSent = e.bytesSent;
                    conn->bytesRecv = e.bytesRecv;
                    conn->hasTraffic = true;
                }
                int pid = e.pid > 0 ? e.pid : (conn ? conn->pid : -1);
                if (pid > 0 && hasPrevSs_ && dt > 0.0) {
                    uint64_t dS = e.bytesSent, dR = e.bytesRecv;   // kết nối mới xuất hiện: tính toàn bộ
                    auto pit = prevSs_.find(key);
                    if (pit != prevSs_.end()) {
                        dS = e.bytesSent >= pit->second.first ? e.bytesSent - pit->second.first : 0;
                        dR = e.bytesRecv >= pit->second.second ? e.bytesRecv - pit->second.second : 0;
                    }
                    pidRate[pid].first += (double)dR / dt;
                    pidRate[pid].second += (double)dS / dt;
                }
            }
            prevSs_.swap(curSs);
            hasPrevSs_ = true;
            prevConnTime_ = now;
        }
    } else if (!hasPrevSs_) {
        trafficAvailable_ = false;
    }

    // tổng hợp theo tiến trình
    usage_.clear();
    std::unordered_map<int, size_t> idx;
    for (const auto &c : all) {
        int pid = c.pid > 0 ? c.pid : 0;
        auto it = idx.find(pid);
        if (it == idx.end()) {
            ProcessNetUsage u;
            u.pid = pid;
            u.name = pid > 0 ? c.procName : "(Không xác định)";
            u.hasTraffic = trafficAvailable_ && pid > 0;
            it = idx.emplace(pid, usage_.size()).first;
            usage_.push_back(u);
        }
        ProcessNetUsage &u = usage_[it->second];
        u.total++;
        if (c.proto.rfind("tcp", 0) == 0) {
            if (c.state == "ESTABLISHED") u.established++;
            else if (c.state == "LISTEN") u.listening++;
        } else {
            u.udp++;
        }
        u.sendTotal += c.bytesSent;
        u.recvTotal += c.bytesRecv;
        if (c.remoteScope == AddrScope::Public && c.state != "LISTEN" && c.state != "UNCONN") u.hasInternet = true;
    }
    for (auto &u : usage_) {
        auto r = pidRate.find(u.pid);
        if (r != pidRate.end()) { u.recvRate = r->second.first; u.sendRate = r->second.second; }
    }
    std::sort(usage_.begin(), usage_.end(), [](const ProcessNetUsage &a, const ProcessNetUsage &b) {
        if ((a.pid > 0) != (b.pid > 0)) return a.pid > 0;    // nhóm "không xác định" xuống cuối
        double ra = a.recvRate + a.sendRate, rb = b.recvRate + b.sendRate;
        if (ra != rb) return ra > rb;
        if (a.established != b.established) return a.established > b.established;
        if (a.total != b.total) return a.total > b.total;
        return a.pid < b.pid;
    });

    conns_ = std::move(all);
}

} // namespace netmon
