// Test ProcessTree bang ung dung da tien trinh gia lap (fakeapp) - fork/exec/kill that.
#include "../ProcessTree.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static int g_failed = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); g_failed++; } \
    else { std::printf("[OK]   %s\n", msg); } \
} while (0)

static std::string g_fakeapp;

static pid_t launchFakeApp() {
    pid_t p = fork();
    if (p == 0) {
        execl(g_fakeapp.c_str(), g_fakeapp.c_str(), (char *)nullptr);
        _exit(127);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));   // cho cay tien trinh dung xong
    return p;
}

static bool isDeadOrZombie(pid_t pid) {
    ProcessInfo info;
    if (read_process_info(pid, &info) != 0) return true;
    return info.state == 'Z';
}

static bool waitAllDead(const std::vector<pid_t> &pids, int timeoutMs = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        bool all = true;
        for (pid_t p : pids) if (!isDeadOrZombie(p)) { all = false; break; }
        if (all) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static pid_t findByCmdlineSubstr(const ProcessArray &all, const char *needle) {
    for (int i = 0; i < all.count; i++)
        if (std::strstr(all.items[i].cmdline, needle) && std::strstr(all.items[i].cmdline, "fakeapp")) return all.items[i].pid;
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 2) { std::fprintf(stderr, "Cach dung: %s <duong dan tuyet doi toi fakeapp>\n", argv[0]); return 2; }
    g_fakeapp = argv[1];
    std::printf("===== TEST ProcessTree =====\n");

    CHECK(proctree::isNoClimbExe("bash") && proctree::isNoClimbExe("python3.11") && proctree::isNoClimbExe("sudo"),
          "isNoClimbExe: bash/python3.11/sudo bi loai tru (khong leo len cha)");
    CHECK(!proctree::isNoClimbExe("firefox") && !proctree::isNoClimbExe("chrome"), "firefox/chrome duoc phep leo len cha");
    CHECK(proctree::isCriticalName("systemd") && proctree::isCriticalName("gnome-shell") && proctree::isCriticalName("Xorg") &&
          !proctree::isCriticalName("firefox"), "isCriticalName: nhan dien tien trinh he thong, khong nham firefox");
    CHECK(proctree::baseName("/snap/firefox/4793/usr/lib/firefox/firefox") == "firefox", "baseName cua duong dan snap firefox");

    // ---------- Cay 1: kiem tra tim goc + dong bang SIGTERM (cha truoc) ----------
    pid_t root = launchFakeApp();
    ProcessArray all;
    parr_init(&all);
    read_all_processes(&all);

    pid_t grand = findByCmdlineSubstr(all, "-contentproc 2");
    pid_t child3 = findByCmdlineSubstr(all, "-contentproc 3");
    CHECK(grand > 0 && child3 > 0, "dung duoc cay da tien trinh (tim thay chau '-contentproc 2' va con '-contentproc 3')");
    CHECK(proctree::findAppRoot(all, grand) == root, "findAppRoot(chau) leo qua con len dung tien trinh chinh (cung file thuc thi)");
    CHECK(proctree::findAppRoot(all, child3) == root, "findAppRoot(con 3) = tien trinh chinh");
    CHECK(proctree::findAppRoot(all, root) == root, "findAppRoot(goc) = chinh no (khong leo qua cha khac chuong trinh)");
    CHECK(proctree::findAppRoot(all, getpid()) == getpid(), "findAppRoot(test) khong leo len shell/cha khac file thuc thi");

    std::vector<pid_t> tree = proctree::collectSubtree(all, root);
    CHECK(tree.size() == 4 && tree.front() == root, "collectSubtree: dung 4 tien trinh, goc dung dau");
    CHECK(proctree::subtreeContains(all, root, grand) && !proctree::subtreeContains(all, root, getpid()), "subtreeContains dung");

    auto prot = proctree::killSubtree(all, getpid(), SIGKILL, true, getpid());
    CHECK(prot.containsProtected && prot.sent.empty() && prot.failed.empty(),
          "BAO VE: cay chua chinh Process Manager bi tu choi, KHONG gui tin hieu nao");

    auto rep = proctree::killSubtree(all, root, SIGTERM, /*leafFirst=*/false, getpid());
    CHECK(rep.sent.size() == 4 && rep.failed.empty(), "SIGTERM: gui thanh cong toi ca 4 tien trinh");
    CHECK(!rep.sent.empty() && rep.sent.front() == root, "SIGTERM theo thu tu cha truoc (de ung dung tu thoat em)");
    int status = 0;
    waitpid(root, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM, "tien trinh chinh thuc su nhan SIGTERM va thoat");
    CHECK(waitAllDead(tree), "TAT CA 4 tien trinh cua cay deu da chet (hoac zombie cho don) - ung dung da dong han");

    // tin hieu toi tien trinh da bien mat khong duoc tinh la loi
    auto stale = proctree::killSubtree(all, root, SIGKILL, true, getpid());
    bool rootInGone = false;
    for (pid_t p : stale.gone) if (p == root) rootInGone = true;
    CHECK(rootInGone, "snapshot cu: tien trinh da thoat nam trong 'gone' (ESRCH), khong bi tinh la loi");
    parr_free(&all);

    // ---------- Cay 2: SIGKILL, con truoc cha sau ----------
    pid_t root2 = launchFakeApp();
    parr_init(&all);
    read_all_processes(&all);
    std::vector<pid_t> tree2 = proctree::collectSubtree(all, root2);
    CHECK(tree2.size() == 4, "cay thu 2 dung duoc 4 tien trinh");
    auto rep2 = proctree::killSubtree(all, root2, SIGKILL, /*leafFirst=*/true, getpid());
    CHECK(rep2.sent.size() == 4 && rep2.sent.back() == root2, "SIGKILL theo thu tu con truoc, cha sau (goc bi kill cuoi cung)");
    waitpid(root2, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "tien trinh chinh chet vi SIGKILL");
    CHECK(waitAllDead(tree2), "SIGKILL: toan bo cay 2 da chet");
    parr_free(&all);

    std::printf("\n===== KET QUA: %s (%d loi) =====\n", g_failed == 0 ? "TAT CA PASS" : "CO LOI", g_failed);
    return g_failed == 0 ? 0 : 1;
}
