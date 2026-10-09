#ifndef PROCESS_LIST_H
#define PROCESS_LIST_H

#include <sys/types.h>

/* NAME_LEN/CMDLINE_LEN/USERNAME_LEN la kich thuoc buffer CO DINH cho TUNG
 * truong trong 1 ProcessInfo (ten, dong lenh, username) - khong lien quan
 * den TONG SO tien trinh co the luu duoc. 256/512/64 byte la du du cho ten
 * file thuc thi va dong lenh dai nhat thuc te tren Linux, nen giu co dinh
 * o day la hop ly, khong can lam dong. */
#define NAME_LEN      256
#define CMDLINE_LEN   512
#define USERNAME_LEN  64

/* Cau truc luu thong tin 1 tien trinh, tuong ung du lieu doc tu thu muc /proc cua tung PID */
typedef struct {
    pid_t pid;
    pid_t ppid;                  /* PID cua tien trinh cha */
    char  name[NAME_LEN];        /* tên tiến trình (comm) */
    char  cmdline[CMDLINE_LEN];  /* dòng lệnh đầy đủ */
    char  state;                 /* R, S, D, Z, T ... */
    uid_t uid;
    char  username[USERNAME_LEN];
    long  priority;
    long  nice;
    unsigned long utime;         /* thời gian CPU ở chế độ user (ticks) */
    unsigned long stime;         /* thời gian CPU ở chế độ kernel (ticks) */
    long  vm_rss_kb;             /* bộ nhớ thực đang dùng, đơn vị kB */
    double cpu_percent;
    double mem_percent;
    unsigned long long starttime; /* thời điểm start tính theo tick kể từ lúc boot */
    int tree_depth; /*luu do sau trong cay*/
} ProcessInfo;

/* Mang dong cho danh sach tien trinh - THAY THE cho mang tinh MAX_PROCESSES
 * co dinh truoc day. Tu dong "phinh to" (realloc) khi so tien trinh thuc te
 * vuot qua suc chua hien tai, khong bao gio cat bot du lieu. */
typedef struct {
    ProcessInfo *items;
    int count;    /* so phan tu dang dung */
    int capacity; /* so phan tu da cap phat (>= count) */
} ProcessArray;

/* Khoi tao mang rong, chua cap phat gi (items=NULL, count=0, capacity=0) */
void parr_init(ProcessArray *arr);

/* Giai phong bo nho, dua mang ve trang thai nhu vua parr_init() */
void parr_free(ProcessArray *arr);

/* Dam bao arr co the chua it nhat min_capacity phan tu, tu dong realloc
 * (nhan doi kich thuoc) neu chua du. Tra ve 1 neu thanh cong, 0 neu malloc
 * that bai (het bo nho he thong) - KHONG crash, du lieu cu trong arr van
 * con nguyen ven khi that bai. */
int parr_reserve(ProcessArray *arr, int min_capacity);

typedef enum {
    SORT_PID,
    SORT_CPU,
    SORT_MEM,
    SORT_NAME
} SortField;

/* Sắp xếp mảng process theo tiêu chí, descending = 1 nếu muốn giảm dần */
void sort_processes(ProcessArray *arr, SortField field, int descending);

/* Tìm các tiến trình có tên/cmdline hoặc PID khớp voi keyword (khong phan
 * biet hoa thuong). Tu dong cap phat du cho 'dst' (toi da bang so luong cua
 * 'src', vi ket qua loc khong the nhieu hon nguon). Tra ve so ket qua tim
 * duoc (cung chinh la dst->count sau khi goi xong), hoac -1 neu malloc loi. */
int search_processes(const ProcessArray *src, const char *keyword, ProcessArray *dst);

#endif
