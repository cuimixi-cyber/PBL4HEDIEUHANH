#ifndef PROCESSTREE_H
#define PROCESSTREE_H

/* ProcessTree: module MỚI (không sửa src/). Xử lý "đóng cả ứng dụng" cho các ứng dụng
 * đa tiến trình như Firefox/Chrome/VS Code.
 *
 * Vì sao cần: Firefox gồm 1 tiến trình chính + hàng chục tiến trình con (Web Content,
 * Socket Process, RDD...). Kill 1 PID chỉ làm sập 1 tab, ứng dụng vẫn chạy.
 *
 * Cách tìm "gốc ứng dụng": các tiến trình con của trình duyệt được chạy lại từ CHÍNH file
 * thực thi của cha (vd `firefox -contentproc ...`), nên ta leo lên cha chừng nào cha còn
 * cùng file thực thi (token đầu của cmdline). Shell/trình thông dịch (bash, python...)
 * bị loại trừ để không vô tình kéo cả phiên terminal vào cây cần đóng. */

#include "core_bridge.h"

#include <string>
#include <utility>
#include <vector>

namespace proctree {

std::string exeToken(const ProcessInfo &p);                  // token đầu của cmdline (đường dẫn file thực thi)
std::string baseName(const std::string &path);
bool isNoClimbExe(const std::string &baseLower);             // shell/interpreter: không leo lên cha
bool isCriticalName(const std::string &comm);                // tiến trình hệ thống/phiên đăng nhập

int indexOfPid(const ProcessArray &all, pid_t pid);          // -1 nếu không có
pid_t findAppRoot(const ProcessArray &all, pid_t pid);
std::vector<pid_t> collectSubtree(const ProcessArray &all, pid_t root);   // BFS, gốc đứng đầu
bool subtreeContains(const ProcessArray &all, pid_t root, pid_t target);

struct KillReport {
    std::vector<pid_t> sent;                                 // đã gửi tín hiệu thành công
    std::vector<pid_t> gone;                                 // đã thoát trước khi kịp gửi (ESRCH) - không phải lỗi
    std::vector<std::pair<pid_t, ActionResult>> failed;      // lỗi thật (thiếu quyền...)
    bool containsProtected = false;                          // cây chứa tiến trình được bảo vệ -> KHÔNG gửi gì
};

/* leafFirst = true  : con trước, cha sau (dùng cho SIGKILL, tránh con mồ côi)
 * leafFirst = false : cha trước, con sau (dùng cho SIGTERM để ứng dụng tự thoát êm)
 * protectPid: nếu nằm trong cây thì từ chối toàn bộ (truyền getpid() của chính chương trình). */
KillReport killSubtree(const ProcessArray &all, pid_t root, int sig, bool leafFirst, pid_t protectPid);

} // namespace proctree

#endif
