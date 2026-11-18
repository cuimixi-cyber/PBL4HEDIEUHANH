# PBL4: Đồ án Hệ Điều Hành — Linux Process Manager & System Monitor

Ứng dụng quản lý tiến trình và giám sát hệ thống đồ họa (GUI) trên môi trường Linux, được xây dựng theo kiến trúc phân tầng: **Lõi C (Backend giao tiếp Kernel)** kết hợp cùng **C++/Qt5 (Logic mở rộng & Giao diện người dùng)**.

---

## 📑 Mục lục
- [1. Kiến trúc hệ thống](#1-kiến-trúc-hệ-thống)
- [2. Tác dụng của lõi C (Core Engine)](#2-tác-dụng-của-lõi-c-core-engine)
- [3. Các tính năng chính](#3-các-tính-năng-chính)
- [4. Cấu trúc thư mục mã nguồn](#4-cấu-trúc-thư-mục-mã-nguồn)
- [5. Yêu cầu môi trường & Cài đặt](#5-yêu-cầu-môi-trường--cài-đặt)
- [6. Hướng dẫn biên dịch & Thực thi](#6-hướng-dẫn-biên-dịch--thực-thi)
- [7. Kiểm thử tự động (Unit Test)](#7-kiểm-thử-tự-động-unit-test)
- [8. Cơ sở lý thuyết Hệ Điều Hành áp dụng](#8-cơ-sở-lý-thuyết-hệ-điều-hành-áp-dụng)

---

## 1. Kiến trúc hệ thống

Dự án áp dụng mô hình kiến trúc tách biệt rõ ràng giữa **Tầng truy xuất hệ thống (System Layer)** và **Tầng giao diện (Presentation Layer)**:

```
┌─────────────────────────────────────────────────────────────┐
│                    GIAO DIỆN NGƯỜI DÙNG                     │
│        (Qt5 Widgets, Biểu đồ thời gian thực, Icon Theme)     │
│       MainWindow, NetworkTab, LiveChart, IconProvider       │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────┴──────────────────────────────┐
│                    TẦNG LOGIC MỞ RỘNG (C++)                 │
│         NetMonitor (Mạng & Sockets), ProcessTree (Cây)      │
└──────────────────────────────┬──────────────────────────────┘
                               │ (core_bridge.h - extern "C")
┌──────────────────────────────┴──────────────────────────────┐
│                    LÕI HỆ THỐNG C (core_c)                  │
│       proc_reader.c, process_list.c, process_actions.c      │
│                     utils.c, logger.c                       │
└──────────────────────────────┬──────────────────────────────┘
                               │ (POSIX System Calls & /proc)
┌──────────────────────────────┴──────────────────────────────┐
│                     LINUX KERNEL SUBSYSTEM                  │
│        Virtual File System (/proc), Signals, Scheduler      │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Tác dụng của lõi C (Core Engine)

Toàn bộ mã nguồn C thuần chuẩn C11/POSIX nằm trong thư mục `src/`, đóng vai trò là **Backend xử lý nghiệp vụ hệ điều hành**, độc lập hoàn toàn với thư viện đồ họa:

| Module | Tác dụng trong hệ thống |
|---|---|
| **`proc_reader.c/.h`** | **Giao tiếp Virtual File System `/proc`**: Đọc và bóc tách thông tin tiến trình từ `/proc/[pid]/stat`, `/proc/[pid]/status`, `/proc/[pid]/cmdline`; đọc trạng thái CPU toàn hệ thống từ `/proc/stat`, tổng dung lượng bộ nhớ từ `/proc/meminfo`, và thời gian máy chạy từ `/proc/uptime`. |
| **`process_actions.c/.h`** | **Tương tác tiến trình (System Calls)**: Gọi hàm hệ thống `kill()` để gửi tín hiệu (SIGTERM, SIGKILL, SIGSTOP, SIGCONT) và `setpriority()` để điều chỉnh giá trị `nice` (-20 đến 19). Chuẩn hóa mã lỗi hệ điều hành (`ESRCH`, `EPERM`, `EINVAL`) thành thông điệp thân thiện. |
| **`utils.c/.h`** | **Thuật toán tính toán tài nguyên**: Tính `%CPU` dựa trên giải thuật chênh lệch (delta) CPU ticks giữa 2 chu kỳ đo liên tiếp; tính tỷ lệ `%RAM` thực tế chiếm dụng (`VmRSS / Total_RAM`); định dạng thời gian thực thi (Uptime/Runtime); hàm phân tích số nguyên an toàn `parse_long_strict`. |
| **`process_list.c/.h`** | **Quản lý dữ liệu động & Tìm kiếm**: Cung cấp cấu trúc mảng động `ProcessArray` tự động co giãn (`realloc`), bảo đảm không giới hạn số lượng tiến trình và chống tràn bộ nhớ đệm; thuật toán sắp xếp (`sort_processes`) đa tiêu chí (%CPU, %RAM, PID, Tên); thuật toán lọc tìm kiếm (`search_processes`). |
| **`logger.c/.h`** | **Nhật ký kiểm toán (Audit Logging)**: Ghi lại lịch sử các tác vụ can thiệp tiến trình (Kill, Đổi độ ưu tiên,...) vào file `process_manager.log` phục vụ việc truy vết và kiểm thử. |

> **Tại sao tách riêng lõi C?**
> * Đảm bảo hiệu năng tối đa khi đọc/ghi file hệ thống liên tục mỗi chu kỳ làm mới (1-2 giây).
> * Đáp ứng đúng chuẩn môn học Hệ điều hành: Làm việc trực tiếp với POSIX API, Signal, `/proc` bằng ngôn ngữ lập trình hệ thống C.
> * Tính độc lập và tái sử dụng cao: Lõi C có thể liên kết với giao diện Qt GUI qua `core_bridge.h`, hoặc tái sử dụng cho giao diện CLI/TUI hay dịch vụ chạy ngầm (Daemon) mà không cần chỉnh sửa bất kỳ dòng mã nào.

---

## 3. Các tính năng chính

### 1. Quản lý danh sách tiến trình (Tab Tiến trình)
- Liệt kê đầy đủ các tiến trình đang hoạt động với các trường: PID, Tên, Trạng thái (Running, Sleeping, Zombie,...), %CPU, %RAM, Priority, Nice, Người sở hữu (User), Thời gian chạy.
- Tìm kiếm theo tên hoặc PID tức thì; bộ lọc tiến trình của người dùng hiện tại hoặc tất cả người dùng.
- Sắp xếp nhanh theo cột bằng cách nhấp chuột vào tiêu đề cột hoặc chọn qua combobox.
- Nhớ vị trí dòng đang chọn và vị trí cuộn khi làm mới tự động (không bị nhảy thanh cuộn).

### 2. Quản lý phân cấp & Ứng dụng đa tiến trình (Tab Cây tiến trình)
- Hiển thị mối quan hệ phân cấp Cha - Con (PPID - PID) dạng cây trực quan.
- **Xử lý ứng dụng đa tiến trình (như Firefox, Chrome):** Tự động phát hiện và gom nhóm các tiến trình con cùng file thực thi.
- Hỗ trợ đóng theo cấp độ:
  - *Đóng cả ứng dụng*: Gửi `SIGTERM` cho tiến trình gốc trước, tự động dọn dẹp các tiến trình con còn sót sau 3 giây bằng `SIGKILL`.
  - *Buộc đóng cả ứng dụng*: Gửi `SIGKILL` toàn bộ nhánh cây.
- **Cơ chế an toàn (Safety Guard):** Ngăn chặn tự đóng chính Process Manager; cảnh báo xác nhận khi can thiệp vào các tiến trình quan trọng của hệ thống (`systemd`, `gnome-shell`, `Xorg`,...).

### 3. Giám sát & Biểu đồ thời gian thực (Tab Tổng quan)
- Biểu đồ thời gian thực (Real-time Live Charts) tự vẽ bằng `QPainter` (tối ưu, không phụ thuộc module nặng `QtCharts`).
- Theo dõi lịch sử mức độ sử dụng CPU (%) và Bộ nhớ RAM (MB / %).
- Thống kê tóm tắt số lượng tiến trình theo từng trạng thái.

### 4. Giám sát mạng chuyên sâu (Tab Mạng)
- Đo tốc độ truyền/nhận mạng tổng (Download / Upload) thời gian thực từ `/proc/net/dev`.
- Thống kê chi tiết từng card mạng (Interface): địa chỉ IP, trạng thái, dung lượng đã truyền/nhận.
- **Lưu lượng mạng theo từng ứng dụng:** Bóc tách dữ liệu socket TCP/UDP, hiển thị số kết nối, cổng dịch vụ (HTTPS, DNS, SSH,...), trạng thái kết nối (ESTABLISHED, LISTEN,...), địa chỉ đối tác.

---

## 4. Cấu trúc thư mục mã nguồn

```
PBL4HEDIEUHANH/
├── src/                      # === LÕI C (HỆ ĐIỀU HÀNH) ===
│   ├── proc_reader.c/.h      # Đọc dữ liệu từ /proc ảo
│   ├── process_list.c/.h     # Cấu trúc mảng động, sắp xếp, tìm kiếm
│   ├── process_actions.c/.h  # Gửi tín hiệu kill, đổi nice
│   ├── utils.c/.h            # Tính %CPU delta, %RAM, format thời gian
│   └── logger.c/.h           # Ghi audit log ra process_manager.log
│
├── core_bridge.h             # Cầu nối extern "C" cho C++ gọi lõi C
│
├── MainWindow.cpp/.h         # Cửa sổ chính, tab Tổng quan & Danh sách tiến trình
├── ProcessTree.cpp/.h        # Phân tích cây tiến trình & đóng đa tiến trình
├── NetworkTab.cpp/.h         # Giao diện tab Mạng & giám sát socket
├── NetMonitor.cpp/.h         # Backend đọc /proc/net/dev và bảng socket TCP/UDP
├── LiveChart.cpp/.h          # Widget vẽ biểu đồ đường thời gian thực (QPainter)
├── IconProvider.cpp/.h       # Quản lý icon theo theme Linux desktop
├── ViewUtil.h                # Tiện ích giữ dòng chọn & cuộn bảng khi refresh
├── main_gui.cpp              # Điểm khởi đầu ứng dụng (main Qt)
│
├── tests/                    # === KIỂM THỬ TỰ ĐỘNG (UNIT TESTS) ===
│   ├── test_netmon.cpp       # Test giám sát mạng và socket
│   ├── test_proctree.cpp     # Test cây tiến trình và thuật toán nhóm PID
│   └── fakeapp.c             # Ứng dụng giả lập tiến trình cha-con để test
│
├── CMakeLists.txt            # Cấu hình biên dịch chuẩn CMake
└── process_manager.log       # File nhật ký hoạt động
```

---

## 5. Yêu cầu môi trường & Cài đặt

Ứng dụng được thiết kế chạy trên hệ điều hành **Linux** (đã kiểm thử tốt trên Ubuntu 20.04, 22.04, 24.04).

Cài đặt các gói công cụ biên dịch và thư viện phụ thuộc:
```bash
sudo apt update
sudo apt install -y build-essential cmake qtbase5-dev iproute2
```
* `build-essential`: Trình biên dịch `gcc`, `g++`, công cụ `make`.
* `cmake`: Hệ thống cấu hình build dự án (phiên bản >= 3.16).
* `qtbase5-dev`: Thư viện giao diện Qt5 Widgets.
* `iproute2`: Cung cấp lệnh `ss` để giám sát lưu lượng socket theo ứng dụng.

---

## 6. Hướng dẫn biên dịch & Thực thi

### 1. Biên dịch dự án
Sử dụng CMake để cấu hình và build dự án:
```bash
# Tạo thư mục build và cấu hình
mkdir -p build && cd build
cmake ..

# Biên dịch song song với số luồng CPU hiện có
make -j$(nproc)
```

### 2. Chạy ứng dụng

- **Chạy với quyền người dùng thông thường:**
  ```bash
  ./process_manager_gui
  ```
  *(Có thể xem hầu hết tiến trình của bản thân; chỉ gửi được tín hiệu tới tiến trình do mình sở hữu).*

- **Chạy với quyền Root (Khuyến nghị để khai thác đủ tính năng):**
  ```bash
  sudo ./process_manager_gui
  ```
  *(Cần thiết để: xem được toàn bộ thông tin socket/mạng của mọi user khác trên hệ thống, can thiệp ưu tiên `nice` âm, gửi tín hiệu đến tiến trình hệ thống).*

---

## 7. Kiểm thử tự động (Unit Test)

Dự án tích hợp sẵn bộ kiểm thử đơn vị cho các module logic hệ thống (chạy độc lập, không cần mở giao diện đồ họa):

```bash
cd build
ctest --output-on-failure
```

Bộ test bao gồm:
1. **`NetMonitor`**: Kiểm thử giải thuật đọc `/proc/net/dev`, phân giải socket loopback thực tế, đo đạc tốc độ truyền tải.
2. **`ProcessTree`**: Sử dụng `fakeapp` giả lập kiến trúc tiến trình cha-con đa nhánh (tương tự trình duyệt), kiểm tra tính chính xác của thuật toán tìm gốc cây, gửi tín hiệu `SIGTERM`/`SIGKILL` theo thứ tự và cơ chế bảo vệ an toàn.

---

## 8. Cơ sở lý thuyết Hệ Điều Hành áp dụng

1. **Hệ thống tệp ảo `/proc` (Procfs):**
   * Đây không phải là file thực trên đĩa cứng mà là giao diện bộ nhớ của Linux Kernel cung cấp trạng thái của các tiến trình và hệ thống.
   * `read_all_processes()` quét các thư mục số nguyên (PID) trong `/proc`, lấy dữ liệu không đồng bộ mà không cần gọi các hàm thư viện nặng nề.

2. **Thuật toán tính `%CPU` chuẩn:**
   * Trong một khoảng thời gian $\Delta t$ giữa 2 lần đo:
     $$\%CPU = \frac{\Delta (\text{utime} + \text{stime})}{\Delta \text{Total System CPU Time}} \times 100\%$$
   * Lần đo đầu tiên (khi chưa có snapshot trước đó) luôn hiển thị 0% để đảm bảo số liệu chính xác từ lần đo thứ 2 trở đi.

3. **Cơ chế Tín hiệu (Signals) & Điều phối tiến trình (Scheduling):**
   * `SIGTERM` (15): Yêu cầu tiến trình kết thúc an toàn, cho phép giải phóng tài nguyên.
   * `SIGKILL` (9): Tín hiệu cưỡng chế từ Kernel, tiến trình bị hủy ngay lập tức không thể bắt (catch) hoặc bỏ qua.
   * `nice` (-20 đến 19): Tác động đến độ ưu tiên tĩnh của tiến trình trong bộ lập lịch CFS (Completely Fair Scheduler) của Linux.
