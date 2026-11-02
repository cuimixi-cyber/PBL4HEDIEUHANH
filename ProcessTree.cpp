#include "ProcessTree.h"

#include <algorithm>
#include <cctype>
#include <queue>
#include <unordered_map>

namespace proctree {

std::string exeToken(const ProcessInfo &p) {
    std::string cmd = p.cmdline;
    size_t sp = cmd.find(' ');
    return sp == std::string::npos ? cmd : cmd.substr(0, sp);
}

std::string baseName(const std::string &path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool isNoClimbExe(const std::string &b) {
    static const char *exact[] = {"bash", "sh", "dash", "zsh", "fish", "ksh", "tcsh", "tmux", "screen",
                                  "sudo", "su", "ssh", "sshd", "login", "systemd", "init", "node",
                                  "nodejs", "java", "perl", "ruby", "php", "lua"};
    for (const char *e : exact) if (b == e) return true;
    return b.rfind("python", 0) == 0;
}

bool isCriticalName(const std::string &comm) {
    std::string c = lower(comm);
    static const char *exact[] = {"init", "gnome-shell", "xorg", "xwayland", "gdm", "gdm3", "lightdm", "sddm",
                                  "sshd", "dbus-daemon", "dbus-broker", "login", "networkmanager",
                                  "plasmashell", "kwin_x11", "kwin_wayland", "cinnamon", "mutter", "xfce4-session",
                                  "gnome-session-b", "pipewire", "wireplumber", "polkitd", "udisksd"};
    for (const char *e : exact) if (c == e) return true;
    return c.rfind("systemd", 0) == 0 || c.rfind("gdm", 0) == 0;
}

int indexOfPid(const ProcessArray &all, pid_t pid) {
    for (int i = 0; i < all.count; i++) if (all.items[i].pid == pid) return i;
    return -1;
}

pid_t findAppRoot(const ProcessArray &all, pid_t pid) {
    int i = indexOfPid(all, pid);
    if (i < 0) return pid;
    std::string exe = exeToken(all.items[i]);
    if (exe.empty() || exe[0] == '[' || isNoClimbExe(lower(baseName(exe)))) return pid;

    pid_t cur = pid;
    for (int guard = 0; guard < 64; guard++) {               // chặn vòng lặp vô hạn nếu dữ liệu bất thường
        int ci = indexOfPid(all, cur);
        if (ci < 0) break;
        pid_t parent = all.items[ci].ppid;
        if (parent <= 1) break;
        int pi = indexOfPid(all, parent);
        if (pi < 0) break;
        if (exeToken(all.items[pi]) != exe) break;           // cha là chương trình khác -> dừng
        cur = parent;
    }
    return cur;
}

static std::unordered_map<pid_t, std::vector<pid_t>> childrenMap(const ProcessArray &all) {
    std::unordered_map<pid_t, std::vector<pid_t>> m;
    for (int i = 0; i < all.count; i++) m[all.items[i].ppid].push_back(all.items[i].pid);
    return m;
}

std::vector<pid_t> collectSubtree(const ProcessArray &all, pid_t root) {
    auto kids = childrenMap(all);
    std::vector<pid_t> order;
    std::queue<pid_t> q;
    q.push(root);
    while (!q.empty()) {
        pid_t cur = q.front(); q.pop();
        order.push_back(cur);
        auto it = kids.find(cur);
        if (it != kids.end()) for (pid_t c : it->second) if (c != cur) q.push(c);
    }
    return order;
}

bool subtreeContains(const ProcessArray &all, pid_t root, pid_t target) {
    auto t = collectSubtree(all, root);
    return std::find(t.begin(), t.end(), target) != t.end();
}

KillReport killSubtree(const ProcessArray &all, pid_t root, int sig, bool leafFirst, pid_t protectPid) {
    KillReport r;
    std::vector<pid_t> order = collectSubtree(all, root);
    if (protectPid > 0 && std::find(order.begin(), order.end(), protectPid) != order.end()) {
        r.containsProtected = true;
        return r;
    }
    if (leafFirst) std::reverse(order.begin(), order.end());
    for (pid_t pid : order) {
        ActionResult a = kill_process(pid, sig);             // tái sử dụng hàm gốc (đã chặn pid <= 1)
        if (a == ACTION_OK) r.sent.push_back(pid);
        else if (a == ACTION_ERR_NOT_FOUND) r.gone.push_back(pid);
        else r.failed.emplace_back(pid, a);
    }
    return r;
}

} // namespace proctree
