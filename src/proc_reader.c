#include "proc_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <errno.h>

/* Kiểm tra 1 chuỗi có phải toàn chữ số hay không (dùng để lọc thư mục PID trong /proc) */
static int is_all_digits(const char *s) {
    if (*s == '\0') return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

/* Đọc /proc/[pid]/comm -> tên tiến trình (đơn giản hơn parse từ stat) */
static void read_comm(pid_t pid, char *out, size_t out_size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        out[0] = '\0';
        return;
    }
    if (fgets(out, (int)out_size, f) != NULL) {
        /* comm luôn kết thúc bằng '\n', cần cắt bỏ */
        size_t len = strlen(out);
        if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    } else {
        out[0] = '\0';
    }
    fclose(f);
}

/* Đọc /proc/[pid]/cmdline -> dòng lệnh đầy đủ.
 * File này ngăn cách tham số bằng byte '\0', cần thay bằng khoảng trắng khi hiển thị. */
static void read_cmdline(pid_t pid, char *out, size_t out_size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        out[0] = '\0';
        return;
    }
    size_t n = fread(out, 1, out_size - 1, f);
    fclose(f);
    out[n] = '\0';

    /* Nếu tiến trình là kernel thread, cmdline rỗng -> để trống, sẽ dùng name thay thế */
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\0') out[i] = ' ';
    }
    /* cắt khoảng trắng thừa ở cuối */
    while (n > 0 && out[n - 1] == ' ') { out[--n] = '\0'; }
}

/* Đọc /proc/[pid]/stat -> state, utime, stime, priority, nice, starttime.
 * Trả về 0 nếu thành công. */
static int read_stat_fields(pid_t pid, ProcessInfo *p) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Tên tiến trình nằm trong dấu ngoặc, có thể chứa khoảng trắng
     * -> tìm dấu ')' CUỐI CÙNG để tránh bị lệch nếu tên có chứa ')' */
    char *close_paren = strrchr(buffer, ')');
    if (!close_paren) return -1;
    char *rest = close_paren + 1; /* phần còn lại bắt đầu từ field 'state' */

    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    unsigned long utime, stime;
    long cutime, cstime, priority, nice_val, num_threads, itrealvalue;
    unsigned long long starttime;

    int n = sscanf(rest,
        " %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu",
        &state, &ppid, &pgrp, &session, &tty_nr, &tpgid, &flags,
        &minflt, &cminflt, &majflt, &cmajflt,
        &utime, &stime, &cutime, &cstime,
        &priority, &nice_val, &num_threads, &itrealvalue, &starttime);

    if (n < 20) return -1; /* parse thiếu trường -> coi như lỗi */

    p->state = state;
    p->ppid = (pid_t)ppid;
    p->utime = utime;
    p->stime = stime;
    p->priority = priority;
    p->nice = nice_val;
    p->starttime = starttime;
    return 0;
}

/* Đọc /proc/[pid]/status -> VmRSS (kB) và Uid */
static void read_status_fields(pid_t pid, ProcessInfo *p) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        p->vm_rss_kb = 0;
        p->uid = 0;
        return;
    }

    char line[256];
    int got_rss = 0, got_uid = 0;
    p->vm_rss_kb = 0;
    p->uid = 0;

    while (fgets(line, sizeof(line), f) && !(got_rss && got_uid)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &p->vm_rss_kb);
            got_rss = 1;
        } else if (strncmp(line, "Uid:", 4) == 0) {
            unsigned int uid;
            sscanf(line + 4, "%u", &uid);
            p->uid = (uid_t)uid;
            got_uid = 1;
        }
    }
    fclose(f);
}

/* Tra username từ uid, dùng getpwuid(). Nếu không tìm thấy, hiển thị số uid. */
static void resolve_username(uid_t uid, char *out, size_t out_size) {
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        snprintf(out, out_size, "%s", pw->pw_name);
    } else {
        snprintf(out, out_size, "%u", (unsigned int)uid);
    }
}

int read_process_info(pid_t pid, ProcessInfo *out) {
    memset(out, 0, sizeof(ProcessInfo));
    out->pid = pid;

    if (read_stat_fields(pid, out) != 0) {
        return -1; /* tiến trình có thể vừa thoát -> bỏ qua an toàn */
    }

    read_comm(pid, out->name, sizeof(out->name));
    read_cmdline(pid, out->cmdline, sizeof(out->cmdline));
    if (out->cmdline[0] == '\0') {
        /* kernel thread thường không có cmdline -> hiển thị tên trong ngoặc vuông */
        snprintf(out->cmdline, sizeof(out->cmdline), "[%s]", out->name);
    }

    read_status_fields(pid, out);
    resolve_username(out->uid, out->username, sizeof(out->username));

    out->cpu_percent = 0.0;
    out->mem_percent = 0.0;

    return 0;
}

int read_all_processes(ProcessArray *out) {
    DIR *d = opendir("/proc");
    if (!d) return 0;

    out->count = 0; /* bat dau lai tu dau, nhung GIU NGUYEN capacity da cap
                        phat tu lan truoc - tranh phai malloc/free lien tuc
                        moi lan refresh (chi realloc khi thuc su can THEM
                        cho, khong bao gio can THU NHO lai) */

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (!is_all_digits(entry->d_name)) continue; /* bỏ qua thư mục không phải PID */

        pid_t pid = (pid_t)atoi(entry->d_name);

        if (!parr_reserve(out, out->count + 1)) {
            /* Het bo nho he thong (rat hiem gap) - dung lai voi so tien
             * trinh da doc duoc, KHONG crash. Nguoi dung van thay duoc
             * phan lon danh sach thay vi chuong trinh sup do hoan toan. */
            break;
        }

        if (read_process_info(pid, &out->items[out->count]) == 0) {
            out->count++;
        }
        /* neu doc loi (tien trinh vua thoat), bo qua, KHONG tang count */
    }

    closedir(d);
    return out->count;
}

int read_total_cpu_time(unsigned long long *total_time) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char cpu_label[16];
    unsigned long long user, nice_v, system, idle, iowait, irq, softirq, steal;

    int n = fscanf(f, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                   cpu_label, &user, &nice_v, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);

    if (n < 9) return -1;

    *total_time = user + nice_v + system + idle + iowait + irq + softirq + steal;
    return 0;
}

int read_total_memory_kb(long *total_mem_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", total_mem_kb);
            found = 1;
            break;
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

int read_system_uptime(double *uptime_sec) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    int n = fscanf(f, "%lf", uptime_sec);
    fclose(f);
    return (n == 1) ? 0 : -1;
}
