#include "process_list.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PROC_ARRAY_INITIAL_CAPACITY 64 /* co y de NHO de de kiem chung logic
    tang truong (realloc) hoat dong dung ngay ca voi so tien trinh binh
    thuong (~50-300) cua 1 he thong that, khong can cho den khi co >4096
    tien trinh moi thay tac dung */

void parr_init(ProcessArray *arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void parr_free(ProcessArray *arr) {
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

int parr_reserve(ProcessArray *arr, int min_capacity) {
    if (arr->capacity >= min_capacity) return 1; /* da du cho, khong can lam gi */

    int new_capacity = (arr->capacity == 0) ? PROC_ARRAY_INITIAL_CAPACITY : arr->capacity;
    while (new_capacity < min_capacity) {
        new_capacity *= 2; /* nhan doi cho den khi du - tranh phai realloc
                               tung chut mot, gay cham khi co nhieu tien trinh */
    }

    ProcessInfo *new_items = realloc(arr->items, sizeof(ProcessInfo) * (size_t)new_capacity);
    if (!new_items) {
        /* malloc/realloc that bai (het bo nho he thong) - du lieu CU trong
         * arr->items van con nguyen ven (realloc khong giai phong ban cu
         * neu that bai), nen chuong trinh van chay tiep duoc voi du lieu
         * cu, chi la khong them duoc tien trinh moi vao lan nay. */
        return 0;
    }

    arr->items = new_items;
    arr->capacity = new_capacity;
    return 1;
}

/* --- Sort --- */

/* Bien toan cuc tam de qsort() biet dang sort theo tieu chi nao (qsort
 * chuan C khong cho truyen them context). */
static SortField g_sort_field;
static int g_sort_desc;

static int compare_fn(const void *a, const void *b) {
    const ProcessInfo *pa = (const ProcessInfo *)a;
    const ProcessInfo *pb = (const ProcessInfo *)b;
    int cmp = 0;

    switch (g_sort_field) {
        case SORT_PID:
            cmp = (pa->pid > pb->pid) - (pa->pid < pb->pid);
            break;
        case SORT_CPU:
            cmp = (pa->cpu_percent > pb->cpu_percent) - (pa->cpu_percent < pb->cpu_percent);
            break;
        case SORT_MEM:
            cmp = (pa->mem_percent > pb->mem_percent) - (pa->mem_percent < pb->mem_percent);
            break;
        case SORT_NAME:
            cmp = strcasecmp(pa->name, pb->name);
            break;
    }
    return g_sort_desc ? -cmp : cmp;
}

void sort_processes(ProcessArray *arr, SortField field, int descending) {
    g_sort_field = field;
    g_sort_desc = descending;
    qsort(arr->items, (size_t)arr->count, sizeof(ProcessInfo), compare_fn);
}

/* --- Search --- */

static void to_lower_copy(const char *src, char *dst, size_t dst_size) {
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < dst_size; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* Kiem tra chuoi co phai toan chu so hay khong (dung de nhan biet nguoi
 * dung dang go PID de tim, thay vi tu khoa van ban thong thuong) */
static int is_all_digits_str(const char *s) {
    if (*s == '\0') return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

int search_processes(const ProcessArray *src, const char *keyword, ProcessArray *dst) {
    /* Ket qua loc khong bao gio nhieu hon nguon -> cap phat truoc dung 1
     * lan theo dung so luong nguon, tranh phai realloc nhieu lan trong
     * vong lap ben duoi. */
    int upper_bound = (src->count > 0) ? src->count : 1;
    if (!parr_reserve(dst, upper_bound)) {
        dst->count = 0;
        return -1; /* malloc that bai */
    }

    char kw_lower[CMDLINE_LEN];
    to_lower_copy(keyword, kw_lower, sizeof(kw_lower));

    /* Neu tu khoa toan la chu so, uu tien coi day la tim theo PID (so sanh
     * chinh xac PID thay vi substring ten/cmdline) - VD go "8" chi khop
     * dung PID 8, khong khop nham voi PID 800, 8123... */
    int search_by_pid = is_all_digits_str(keyword);
    long target_pid = search_by_pid ? atol(keyword) : -1;

    int found = 0;
    for (int i = 0; i < src->count; i++) {
        int matched;

        if (search_by_pid) {
            /* Tu khoa toan chu so -> CHI so khop PID chinh xac, KHONG rot
             * xuong kiem tra substring ten/cmdline nua. Neu khong lam vay,
             * go "1" se khop nham voi moi tien trinh co cmdline chua ky tu
             * "1" o bat ky dau (VD "kworker/0:1-virtio") - gay nhieu ket
             * qua vo nghia thay vi tim dung 1 tien trinh theo PID. */
            matched = (src->items[i].pid == (pid_t)target_pid);
        } else {
            char name_lower[NAME_LEN];
            char cmd_lower[CMDLINE_LEN];
            to_lower_copy(src->items[i].name, name_lower, sizeof(name_lower));
            to_lower_copy(src->items[i].cmdline, cmd_lower, sizeof(cmd_lower));
            matched = (strstr(name_lower, kw_lower) != NULL || strstr(cmd_lower, kw_lower) != NULL);
        }

        if (matched) {
            dst->items[found] = src->items[i];
            found++;
        }
    }

    dst->count = found;
    return found;
}
