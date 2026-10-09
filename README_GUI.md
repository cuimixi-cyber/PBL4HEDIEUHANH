# Giao diện GUI (Qt) cho Process Manager

Đây là **giao diện thứ 2**, song song với bản TUI (ncurses) gốc trong `src/`.
Cả 2 bản dùng **chung một lõi**, không trùng lặp logic.

## Cam kết "không sửa code gốc"

Toàn bộ các file trong `src/` (`process_list.c/h`, `proc_reader.c/h`,
`utils.c/h`, `process_actions.c/h`, `logger.c/h`, `main.c`, `ui.c/h`) **được
giữ nguyên 100%**, không chỉnh sửa gì. Bằng chứng kỹ thuật: thư mục này chỉ
`add_library(core_c ...)` biên dịch LẠI đúng các file `.c` gốc (cùng đúng cờ
biên dịch `-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE` như trong
Makefile gốc), rồi GUI Qt (C++) gọi vào qua `core_bridge.h` (file MỚI, chỉ
bọc `extern "C"` để C++ linker tìm đúng hàm C, không chứa logic gì).

**Đã kiểm chứng thật**: biên dịch các file gốc bằng gcc thành `.o`, viết 1
chương trình C++ tối giản gọi `read_all_processes()`, `sort_processes()`,
`kill_process()`, `parse_long_strict()`, `log_action()` qua `core_bridge.h`,
link bằng `g++` — **chạy đúng trên dữ liệu `/proc` thật** (đọc được 50 tiến
trình, kill signal 0 tới chính mình thành công...). Việc "ghép GUI Qt vào
code C gốc mà không sửa gì" là khả thi và đã test, không phải lý thuyết.

## Các file MỚI (chỉ ở đây, không đụng `src/`)

```
gui_qt/
├── core_bridge.h      # bọc extern "C", KHÔNG chứa logic
├── MainWindow.h/.cpp   # TOÀN BỘ UI Qt - gọi lại hàm gốc, không viết lại thuật toán
├── main_gui.cpp        # điểm khởi động QApplication
└── CMakeLists.txt      # build riêng, không đụng Makefile gốc
```

## Cách build (cần Qt5, chưa test được trong sandbox vì thiếu Qt dev headers)

```bash
sudo apt install qtbase5-dev cmake build-essential   # nếu máy chưa có
cd gui_qt
mkdir build && cd build
cmake ..
make -j$(nproc)
./process_manager_gui
```

Bản TUI gốc vẫn build như cũ, không bị ảnh hưởng gì:
```bash
cd ..          # về thư mục gốc chứa Makefile
make run
```

## Giao diện chia 4 tab (dễ quản lý, dễ demo)

1. **Tổng quan** — progress bar %CPU/%RAM toàn hệ thống, uptime, số tiến
   trình, cảnh báo zombie, cài đặt chu kỳ refresh + chế độ %CPU (hệ
   thống/per-core, giống phím `m` ở bản TUI).
2. **Danh sách tiến trình** — bảng đầy đủ (PID/Tên/Trạng thái/%CPU/%MEM/
   PRI/NI/User/**Thời gian chạy**), tìm kiếm, sắp xếp, kill (SIGTERM riêng
   và SIGKILL riêng — tách rõ 2 nút, khác bản TUI chỉ có SIGKILL), đổi nice.
3. **Cây tiến trình** — `QTreeWidget` dựng từ quan hệ PPID, thay cho ASCII
   tree trong bản TUI.
4. **Nhật ký** — đọc trực tiếp `process_manager.log` do chính `log_action()`
   gốc ghi ra, tự động tải lại khi chuyển sang tab này.

## Một phát hiện nhỏ khi làm GUI

Hàm `format_run_time()` trong `utils.c` đã viết sẵn và đúng, nhưng **chưa
được gọi ở bất kỳ đâu trong bản TUI** (`main.c`/`ui.c` không dùng tới). GUI
lần này tận dụng lại đúng hàm này cho cột "Thời gian chạy" — không viết
thêm logic mới, chỉ là dùng lại cái đã có sẵn nhưng bị bỏ quên.

## Giới hạn cần biết

Sandbox tạo ra bản này **không có Qt dev headers và không có mạng để cài**,
nên phần `MainWindow.cpp`/`main_gui.cpp` **chưa build-test được** (khác với
phần lõi C đã test thật ở trên). Mình đã viết đúng API Qt5 Widgets theo kinh
nghiệm, nhưng bạn cần tự `cmake .. && make` trên máy có Qt5 và báo lại nếu
gặp lỗi compile — khả năng cao nhất nếu có lỗi sẽ là lỗi cú pháp nhỏ (tên
hàm/thiếu include), không phải lỗi logic, vì toàn bộ phần tính toán đã dùng
lại nguyên hàm đã test.
