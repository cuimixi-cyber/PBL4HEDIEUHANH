#ifndef PROC_READER_H
#define PROC_READER_H

#include "process_list.h"

/* Doc thong tin 1 tien trinh tu cac file trong thu muc /proc cua PID tuong ung.
 * Trả về 0 nếu thành công, -1 nếu tiến trình không tồn tại/không đọc được. */
int read_process_info(pid_t pid, ProcessInfo *out);

/* Duyet toan bo /proc, doc thong tin tat ca tien trinh vao 'out'.
 * 'out' PHAI da duoc parr_init() truoc do (co the la mang rong). Ham nay tu
 * dong parr_reserve() de mo rong dung luong khi can, KHONG gioi han so
 * luong toi da nhu MAX_PROCESSES truoc day.
 * Tra ve so luong tien trinh doc duoc (chinh la out->count sau khi goi xong). */
int read_all_processes(ProcessArray *out);

/* Đọc tổng thời gian CPU của toàn hệ thống từ /proc/stat (dòng "cpu ...").
 * Dùng làm mốc để tính %CPU. Trả về 0 nếu thành công. */
int read_total_cpu_time(unsigned long long *total_time);

/* Đọc tổng RAM hệ thống (kB) từ /proc/meminfo. Trả về 0 nếu thành công. */
int read_total_memory_kb(long *total_mem_kb);

/* Đọc uptime hệ thống (giây) từ /proc/uptime. Trả về 0 nếu thành công. */
int read_system_uptime(double *uptime_sec);

#endif
