#include "utils.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/* Tìm 1 tiến trình trong danh sách 'prev' theo pid, trả về con trỏ hoặc NULL */
static const ProcessInfo *find_by_pid(const ProcessInfo *prev, int prev_count, pid_t pid) {
    for (int i = 0; i < prev_count; i++) {
        if (prev[i].pid == pid) return &prev[i];
    }
    return NULL;
}

void compute_cpu_percent(ProcessInfo *curr, int curr_count,
                          const ProcessInfo *prev, int prev_count,
                          unsigned long long total_delta) {
    if (total_delta == 0) {
        /* lần đo đầu tiên chưa có mốc so sánh -> %CPU = 0 cho tất cả */
        for (int i = 0; i < curr_count; i++) curr[i].cpu_percent = 0.0;
        return;
    }

    for (int i = 0; i < curr_count; i++) {
        const ProcessInfo *p_old = find_by_pid(prev, prev_count, curr[i].pid);
        if (!p_old) {
            curr[i].cpu_percent = 0.0; /* tiến trình mới xuất hiện */
            continue;
        }
        unsigned long long old_time = (unsigned long long)p_old->utime + p_old->stime;
        unsigned long long new_time = (unsigned long long)curr[i].utime + curr[i].stime;

        if (new_time < old_time) {
            /* phòng trường hợp PID bị tái sử dụng cho tiến trình khác */
            curr[i].cpu_percent = 0.0;
            continue;
        }

        unsigned long long proc_delta = new_time - old_time;
        curr[i].cpu_percent = 100.0 * (double)proc_delta / (double)total_delta;
    }
}

void compute_mem_percent(ProcessInfo *list, int count, long total_mem_kb) {
    if (total_mem_kb <= 0) {
        for (int i = 0; i < count; i++) list[i].mem_percent = 0.0;
        return;
    }
    for (int i = 0; i < count; i++) {
        list[i].mem_percent = 100.0 * (double)list[i].vm_rss_kb / (double)total_mem_kb;
    }
}

void format_run_time(unsigned long long starttime_ticks, double uptime_sec, char *out, size_t out_size) {
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100; /* giá trị mặc định phổ biến trên Linux */

    double start_sec = (double)starttime_ticks / (double)clk_tck;
    double run_sec = uptime_sec - start_sec;
    if (run_sec < 0) run_sec = 0;

    long total_seconds = (long)run_sec;
    long hh = total_seconds / 3600;
    long mm = (total_seconds % 3600) / 60;
    long ss = total_seconds % 60;

    snprintf(out, out_size, "%02ld:%02ld:%02ld", hh, mm, ss);
}

int parse_long_strict(const char *s, long *out) {
    if (s == NULL) return 0;

    /* bo qua khoang trang dau chuoi (cho phep nguoi dung go " 123" van hop le) */
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return 0; /* chuoi rong hoac toan khoang trang -> khong hop le */

    errno = 0;
    char *endptr;
    long val = strtol(s, &endptr, 10);

    if (endptr == s) return 0;      /* khong doc duoc chu so nao (VD "abc") */
    if (errno == ERANGE) return 0;  /* tran so (qua lon/qua nho so voi long) */

    /* cho phep khoang trang o CUOI chuoi (VD nguoi dung go "123 "), nhung
     * neu con ky tu khac khoang trang sau so -> coi la khong hop le
     * (VD "123abc" khong duoc chap nhan la 123) */
    while (*endptr == ' ' || *endptr == '\t') endptr++;
    if (*endptr != '\0') return 0;

    *out = val;
    return 1;
}
