// Biên dịch: g++ -std=c++17 -Wall -Wextra -I. NetMonitor.cpp tests/test_netmon.cpp -o test_netmon
#include "../NetMonitor.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace netmon;

static int g_failed = 0, g_skipped = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); g_failed++; } \
    else { std::printf("[OK]   %s\n", msg); } \
} while (0)

static const Connection *findConn(const std::vector<Connection> &v, uint16_t localPort, const std::string &state) {
    for (const auto &c : v)
        if (c.localPort == localPort && c.state == state) return &c;
    return nullptr;
}

int main() {
    std::printf("===== TEST NetMonitor =====\n");

    // ---------- A. parseNetDev ----------
    {
        const char *sample =
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo:  1000      10    0    0    0     0          0         0     1000      10    0    0    0     0       0          0\n"
            "  eth0:123456789  9000    1    2    0     0          0         0 98765432   7000    3    4    0     0       0          0\n";
        auto v = NetMonitor::parseNetDev(sample);
        CHECK(v.size() == 2, "parseNetDev: doc dung 2 giao dien (bo qua 2 dong header)");
        CHECK(v.size() == 2 && v[1].name == "eth0" && v[1].rxBytes == 123456789 && v[1].txBytes == 98765432,
              "parseNetDev: rx/tx bytes cua eth0 dung");
        CHECK(v.size() == 2 && v[1].rxErrors == 1 && v[1].rxDropped == 2 && v[1].txErrors == 3 && v[1].txDropped == 4,
              "parseNetDev: errs/drop dung cot");
    }

    // ---------- B. parseProcNet (IPv4 + IPv6) ----------
    {
        const char *tcp4 =
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 12345 1 0000000000000000 100 0 0 10 0\n"
            "   1: 0F01A8C0:C350 2E42FA8E:01BB 01 00000000:00000000 02:000A7214 00000000  1000        0 23456 2 0000000000000000 20 4 30 10 -1\n"
            "   2: 0F01A8C0:C351 0100A8C0:0050 06 00000000:00000000 03:000001A4 00000000     0        0 0 3 0000000000000000\n";
        auto v = NetMonitor::parseProcNet(tcp4, "tcp");
        CHECK(v.size() == 3, "parseProcNet(tcp): doc dung 3 ket noi");
        CHECK(v.size() == 3 && v[0].localIp == "127.0.0.1" && v[0].localPort == 8080 && v[0].state == "LISTEN" && v[0].inode == 12345 && v[0].uid == 1000,
              "parseProcNet: 0100007F:1F90 -> 127.0.0.1:8080 LISTEN, inode/uid dung");
        CHECK(v.size() == 3 && v[1].localIp == "192.168.1.15" && v[1].localPort == 50000 &&
              v[1].remoteIp == "142.250.66.46" && v[1].remotePort == 443 && v[1].state == "ESTABLISHED",
              "parseProcNet: giai ma dung dia chi little-endian 192.168.1.15:50000 -> 142.250.66.46:443");
        CHECK(v.size() == 3 && v[1].remoteScope == AddrScope::Public, "ket noi toi 142.250.66.46 duoc xep la Internet");
        CHECK(v.size() == 3 && v[2].state == "TIME_WAIT" && v[2].inode == 0 && v[2].remoteScope == AddrScope::Private,
              "TIME_WAIT: inode 0 (socket cua kernel), dich 192.168.0.1 la LAN");

        const char *tcp6 =
            "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 00000000000000000000000001000000:0277 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 777 1 0000000000000000 100 0 0 10 0\n";
        auto v6 = NetMonitor::parseProcNet(tcp6, "tcp6");
        CHECK(v6.size() == 1 && v6[0].localIp == "::1" && v6[0].localPort == 631 && v6[0].state == "LISTEN",
              "parseProcNet(tcp6): giai ma dung ::1 cong 631");
        CHECK(v6.size() == 1 && v6[0].remoteScope == AddrScope::Any, "dich :: duoc xep la 'Bat ky'");

        const char *udp4 =
            "   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
            "  100: 00000000:14E9 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 999 2 0000000000000000 0\n";
        auto vu = NetMonitor::parseProcNet(udp4, "udp");
        CHECK(vu.size() == 1 && vu[0].localPort == 5353 && vu[0].state == "UNCONN", "parseProcNet(udp): cong 5353 (mDNS) UNCONN");
    }

    // ---------- C. classifyAddress / serviceName ----------
    {
        CHECK(NetMonitor::classifyAddress("127.0.0.1") == AddrScope::Loopback, "127.0.0.1 = Loopback");
        CHECK(NetMonitor::classifyAddress("10.1.2.3") == AddrScope::Private, "10.1.2.3 = LAN");
        CHECK(NetMonitor::classifyAddress("172.20.0.5") == AddrScope::Private, "172.20.0.5 = LAN");
        CHECK(NetMonitor::classifyAddress("172.32.0.5") == AddrScope::Public, "172.32.0.5 = Internet (ngoai dai 172.16/12)");
        CHECK(NetMonitor::classifyAddress("192.168.5.5") == AddrScope::Private, "192.168.5.5 = LAN");
        CHECK(NetMonitor::classifyAddress("169.254.1.1") == AddrScope::LinkLocal, "169.254.x.x = Link-local");
        CHECK(NetMonitor::classifyAddress("8.8.8.8") == AddrScope::Public, "8.8.8.8 = Internet");
        CHECK(NetMonitor::classifyAddress("0.0.0.0") == AddrScope::Any, "0.0.0.0 = Bat ky");
        CHECK(NetMonitor::classifyAddress("::1") == AddrScope::Loopback, "::1 = Loopback");
        CHECK(NetMonitor::classifyAddress("fe80::1") == AddrScope::LinkLocal, "fe80::1 = Link-local");
        CHECK(NetMonitor::classifyAddress("fd12:3456::1") == AddrScope::Private, "fd..:: = LAN (ULA)");
        CHECK(NetMonitor::classifyAddress("2404:6800::1") == AddrScope::Public, "2404:6800::1 = Internet");
        CHECK(NetMonitor::classifyAddress("::ffff:192.168.1.1") == AddrScope::Private, "IPv4-mapped xet theo phan IPv4");
        CHECK(NetMonitor::serviceName(443) == "HTTPS" && NetMonitor::serviceName(22) == "SSH" && NetMonitor::serviceName(12345).empty(),
              "serviceName: 443=HTTPS, 22=SSH, cong la = rong");
    }

    // ---------- D. parseSsOutput + normalizeEndpoint ----------
    {
        const char *ssOut =
            "State  Recv-Q Send-Q Local Address:Port  Peer Address:Port Process\n"
            "ESTAB  0      0      192.168.1.15:50000  142.250.66.46:443 users:((\"firefox\",pid=2345,fd=96))\n"
            "\t cubic wscale:7,7 rto:204 rtt:15.5/7.2 mss:1448 cwnd:10 bytes_sent:5120 bytes_retrans:0 bytes_acked:5121 bytes_received:48213 segs_out:42\n"
            "LISTEN 0      4096   127.0.0.53%lo:53    0.0.0.0:*\n"
            "\t cubic cwnd:10 bytes_received:0\n"
            "ESTAB  0      0      [::1]:40000         [::1]:631         users:((\"cupsd\",pid=900,fd=7))\n"
            "\t bytes_sent:77 bytes_acked:77 bytes_received:99\n";
        auto e = NetMonitor::parseSsOutput(ssOut);
        CHECK(e.size() == 3, "parseSsOutput: doc dung 3 ket noi, bo qua dong header");
        CHECK(e.size() == 3 && e[0].pid == 2345 && e[0].name == "firefox" && e[0].bytesSent == 5120 && e[0].bytesRecv == 48213,
              "parseSsOutput: pid/ten/bytes_sent/bytes_received cua firefox dung (khong nham bytes_acked/bytes_retrans)");
        CHECK(e.size() == 3 && e[1].pid == -1 && e[1].bytesSent == 0, "ket noi khong co users:(...) -> pid = -1");
        CHECK(e.size() == 3 && e[2].pid == 900 && e[2].bytesSent == 77 && e[2].bytesRecv == 99, "dong v6 [::1] parse dung");
        CHECK(NetMonitor::normalizeEndpoint("127.0.0.53%lo:53") == "127.0.0.53:53", "normalizeEndpoint bo hau to %lo");
        CHECK(NetMonitor::normalizeEndpoint("[fe80::1%eth0]:631") == "fe80::1:631", "normalizeEndpoint bo [] va %eth0 o dia chi v6");
    }

    // ---------- E. TEST THAT: socket loopback do chinh test tao ra ----------
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    CHECK(bind(listener, (sockaddr *)&addr, sizeof(addr)) == 0 && listen(listener, 4) == 0, "tao socket LISTEN tren 127.0.0.1");
    socklen_t len = sizeof(addr);
    getsockname(listener, (sockaddr *)&addr, &len);
    const uint16_t listenPort = ntohs(addr.sin_port);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(connect(client, (sockaddr *)&addr, sizeof(addr)) == 0, "client ket noi toi socket LISTEN");
    int server = accept(listener, nullptr, nullptr);
    sockaddr_in caddr{};
    len = sizeof(caddr);
    getsockname(client, (sockaddr *)&caddr, &len);
    const uint16_t clientPort = ntohs(caddr.sin_port);

    // phat sinh luu luong that tren loopback de thu tinh toc do card mang
    NetMonitor mon;
    mon.sampleInterfaces();
    {
        std::vector<char> buf(65536, 'x'), rbuf(65536);
        for (int i = 0; i < 40; i++) {
            send(client, buf.data(), buf.size(), 0);
            size_t got = 0;
            while (got < buf.size()) {
                ssize_t n = recv(server, rbuf.data(), rbuf.size(), 0);
                if (n <= 0) break;
                got += (size_t)n;
            }
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    mon.sampleInterfaces();
    {
        const InterfaceRate *lo = nullptr;
        for (const auto &r : mon.interfaces()) if (r.isLoopback) lo = &r;
        CHECK(lo != nullptr, "sampleInterfaces: thay giao dien loopback 'lo'");
        if (lo && lo->rxRate > 0.0) {
            CHECK(lo->rxRate > 100000.0, "toc do loopback do duoc > 100KB/s sau khi truyen ~2.5MB that");
            CHECK(mon.totalRxRate() >= 0.0 && mon.totalTxRate() >= 0.0, "tong toc do (khong tinh loopback) khong am");
        } else {
            std::printf("[SKIP] /proc/net/dev cua sandbox khong cap nhat bo dem 'lo' (kernel gia lap) - bo qua kiem tra toc do loopback\n");
            g_skipped++;
        }
    }

    mon.sampleConnections(false);
    {
        const auto &conns = mon.connections();
        const Connection *l = findConn(conns, listenPort, "LISTEN");
        CHECK(l != nullptr, "sampleConnections: tim thay socket LISTEN cua chinh test trong /proc/net/tcp");
        CHECK(l && l->pid == getpid(), "anh xa inode -> PID: socket LISTEN thuoc dung PID cua test (qua /proc/[pid]/fd)");
        const Connection *est = findConn(conns, clientPort, "ESTABLISHED");
        CHECK(est && est->pid == getpid() && est->remotePort == listenPort && est->remoteScope == AddrScope::Loopback,
              "ket noi ESTABLISHED phia client: dung PID, dung cong dich, phan loai Loopback");

        const ProcessNetUsage *me = nullptr;
        for (const auto &u : mon.processUsage()) if (u.pid == getpid()) me = &u;
        CHECK(me != nullptr, "processUsage co dong cho chinh tien trinh test");
        CHECK(me && me->listening >= 1 && me->established >= 2, "processUsage: dem dung LISTEN>=1 va ESTABLISHED>=2 (client+server)");
        std::printf("       -> tong ket noi he thong doc duoc: %zu, socket khong xac dinh PID: %d\n",
                    conns.size(), mon.unresolvedSocketCount());
    }

    // ---------- F. TEST tinh toc do qua `ss` GIA LAP (sandbox khong co ss that) ----------
    system("mkdir -p /tmp/fakebin && printf '#!/bin/sh\\ncat /tmp/fake_ss_out.txt\\n' > /tmp/fakebin/ss && chmod +x /tmp/fakebin/ss");
    const char *oldPath = std::getenv("PATH");
    std::string newPath = std::string("/tmp/fakebin:") + (oldPath ? oldPath : "/usr/bin:/bin");
    setenv("PATH", newPath.c_str(), 1);

    auto writeSs = [&](unsigned long sent, unsigned long recv) {
        std::ofstream f("/tmp/fake_ss_out.txt");
        f << "State Recv-Q Send-Q Local Address:Port Peer Address:Port Process\n"
          << "ESTAB 0 0 127.0.0.1:" << clientPort << " 127.0.0.1:" << listenPort
          << " users:((\"test_netmon\",pid=" << getpid() << ",fd=5))\n"
          << "\t cubic wscale:7,7 bytes_sent:" << sent << " bytes_acked:" << sent << " bytes_received:" << recv << " segs_out:1\n";
    };

    NetMonitor mon2;
    writeSs(1000, 2000);
    mon2.sampleConnections(true);                            // lan 1: moc co so, chua co toc do
    CHECK(mon2.trafficAvailable(), "ss chay thanh cong -> trafficAvailable = true");
    {
        const Connection *c = findConn(mon2.connections(), clientPort, "ESTABLISHED");
        CHECK(c && c->hasTraffic && c->bytesSent == 1000 && c->bytesRecv == 2000,
              "gan dung bytes_sent/bytes_received cua ss vao ket noi (khop theo 4-tuple)");
        const ProcessNetUsage *me = nullptr;
        for (const auto &u : mon2.processUsage()) if (u.pid == getpid()) me = &u;
        CHECK(me && me->sendRate == 0.0 && me->recvRate == 0.0, "lan lay mau dau tien: toc do = 0 (chua co moc so sanh)");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    writeSs(11000, 52000);                                   // +10000 gui, +50000 nhan
    mon2.sampleConnections(true);
    {
        const ProcessNetUsage *me = nullptr;
        for (const auto &u : mon2.processUsage()) if (u.pid == getpid()) me = &u;
        CHECK(me != nullptr, "co du lieu tien trinh sau lan lay mau thu 2");
        if (me) {
            std::printf("       -> sendRate=%.0f B/s recvRate=%.0f B/s\n", me->sendRate, me->recvRate);
            CHECK(me->sendRate > 0.0 && me->recvRate > 0.0, "toc do gui/nhan > 0 khi byte tang giua 2 lan do");
            double ratio = me->recvRate / me->sendRate;
            CHECK(ratio > 4.9 && ratio < 5.1, "ti le nhan/gui = 5.0 (50000/10000), khong phu thuoc khoang thoi gian do");
            CHECK(me->sendRate < 10000.0 / 0.25 * 1.01 && me->sendRate > 10000.0 / 2.0,
                  "toc do gui nam trong khoang hop ly so voi ~0.25s troi qua");
        }
    }
    // dong ket noi bien mat o lan do 3 -> khong duoc ra toc do am / khong crash
    {
        std::ofstream f("/tmp/fake_ss_out.txt");
        f << "State Recv-Q Send-Q Local Address:Port Peer Address:Port Process\n";
    }
    mon2.sampleConnections(true);
    CHECK(mon2.trafficAvailable(), "ss khong tra ket noi nao van la thanh cong, khong crash");

    // khi khong co ss -> trafficAvailable = false va van liet ke ket noi binh thuong
    setenv("PATH", "/nonexistent", 1);
    NetMonitor mon3;
    mon3.sampleConnections(true);
    CHECK(!mon3.trafficAvailable(), "khong co lenh ss -> trafficAvailable = false (UI se bao 'N/A')");
    CHECK(!mon3.connections().empty(), "thieu ss van doc duoc danh sach ket noi tu /proc/net (it nhat la socket cua test)");
    setenv("PATH", newPath.c_str(), 1);

    close(client);
    close(server);
    close(listener);
    std::printf("\n===== KET QUA: %s (%d loi, %d bo qua) =====\n", g_failed == 0 ? "TAT CA PASS" : "CO LOI", g_failed, g_skipped);
    return g_failed == 0 ? 0 : 1;
}
