#ifndef UTILS_H
#define UTILS_H

#include "process_list.h"

/* Tính %CPU cho từng tiến trình trong 'curr' dựa trên snapshot trước đó 'prev'.
 * total_delta = chênh lệch tổng thời gian CPU hệ thống giữa 2 lần đo.
 * Nếu 1 pid ở curr không tìm thấy trong prev (tiến trình mới), %CPU = 0. */
void compute_cpu_percent(ProcessInfo *curr, int curr_count,
                          const ProcessInfo *prev, int prev_count,
                          unsigned long long total_delta);

/* Tính %MEM cho từng tiến trình dựa trên tổng RAM hệ thống (kB) */
void compute_mem_percent(ProcessInfo *list, int count, long total_mem_kb);

/* Định dạng thời gian chạy của tiến trình thành "HH:MM:SS" */
void format_run_time(unsigned long long starttime_ticks, double uptime_sec, char *out, size_t out_size);

/* Parse chuoi thanh so nguyen (long) MOT CACH CHAT CHE, dung thay the atoi().
 * Tra ve 1 neu hop le, 0 neu khong. "Hop le" nghia la: khong rong, khong co
 * ky tu la (ngoai khoang trang o dau/cuoi), khong tran so (ERANGE).
 * atoi() se am tham tra ve 0 cho ca 3 truong hop "", "abc", so tran - khong
 * co cach nao phan biet "nguoi dung go so 0 that" voi "nguoi dung go rac".
 * Ham nay giup phan biet ro rang de bao loi chinh xac cho nguoi dung. */
int parse_long_strict(const char *s, long *out);

#endif
