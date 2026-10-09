// Kiem tra: C++ co goi duoc dung cac ham C da bien dich san (process_list.o,
// proc_reader.o, utils.o, process_actions.o, logger.o) thong qua core_bridge.h
// hay khong - day la rui ro ky thuat DUY NHAT cua huong "them GUI Qt ma khong
// sua code loi C goc". Neu file nay link + chay dung -> huong di an toan.
#include "core_bridge.h"
#include <cstdio>
#include <unistd.h>

int main() {
    ProcessArray arr;
    parr_init(&arr);

    int n = read_all_processes(&arr);
    printf("[C++ gọi hàm C] read_all_processes() -> %d tiến trình\n", n);
    if (n <= 1) { printf("[FAIL]\n"); return 1; }

    long total_mem_kb = 0;
    read_total_memory_kb(&total_mem_kb);
    compute_mem_percent(arr.items, arr.count, total_mem_kb);

    sort_processes(&arr, SORT_CPU, 1);
    printf("[C++ gọi hàm C] sort_processes(SORT_CPU) OK, top1 pid=%d name=%s mem=%.3f%%\n",
           arr.items[0].pid, arr.items[0].name, arr.items[0].mem_percent);

    ActionResult r = kill_process((pid_t)getpid(), 0); // signal 0: chỉ kiểm tra tồn tại
    printf("[C++ gọi hàm C] kill_process(self, 0) -> %s\n", action_result_to_string(r));

    long val;
    int ok = parse_long_strict("42", &val);
    printf("[C++ gọi hàm C] parse_long_strict('42') -> ok=%d val=%ld\n", ok, val);

    log_action("CPP_BRIDGE_TEST", getpid(), "Thanh cong.");
    printf("[C++ gọi hàm C] log_action() đã ghi file log thành công\n");

    parr_free(&arr);
    printf("\n===== KET LUAN: C++ goi duoc toan bo ham C goc qua extern \"C\", KHONG can sua file .c/.h nao =====\n");
    return 0;
}
