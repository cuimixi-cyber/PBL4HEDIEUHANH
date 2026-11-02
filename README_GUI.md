# GUI Qt cho Process Manager (bản cập nhật)

`src/*.c|h` (lõi C của nhóm) **giữ nguyên 100%** — đã so sánh từng byte với bản nhóm gửi.
Mọi thứ dưới đây là code MỚI nằm ngoài `src/`.

## Đặt file thế nào
Đè các file trong zip lên thư mục gốc repo (cạnh `src/`, `tests/`, `Makefile`):
```
CMakeLists.txt  core_bridge.h  main_gui.cpp
MainWindow.h/.cpp   NetworkTab.h/.cpp   LiveChart.h/.cpp   IconProvider.h/.cpp
NetMonitor.h/.cpp   ProcessTree.h/.cpp  ViewUtil.h
tests/test_netmon.cpp  tests/test_proctree.cpp  tests/fakeapp.c   (thêm vào, không đụng test_core.c)
```
`core_bridge.h` giờ include tên "trần" (`process_list.h`...) nên đúng với bố cục `src/` ngang hàng.
CMake tự nhận bố cục (`src/` hoặc `../src/`).

## Build
```bash
sudo apt install qtbase5-dev cmake build-essential   # cần Qt >= 5.11 (Ubuntu 20.04 trở lên)
mkdir build && cd build && cmake .. && make -j$(nproc)
./process_manager_gui          # sudo ./process_manager_gui để thấy socket của user khác
ctest --output-on-failure      # test phần logic, không cần mở GUI
```
Không cần thêm module Qt Charts: biểu đồ tự vẽ bằng `QPainter` (`LiveChart`).

## Các thay đổi theo yêu cầu

### 1) Lỗi thanh xanh chọn tiến trình bị mất
Nguyên nhân thật: mỗi lần làm mới, `setRowCount(0)` đẩy **thanh cuộn về đầu bảng** → dòng đang chọn
bị đẩy ra khỏi tầm nhìn; thêm nữa khi bấm sang nút khác bảng mất focus và Qt tô **xám** dòng chọn.
Sửa (`ViewUtil.h`): giữ PID đang chọn, giữ vị trí cuộn, chọn lại đúng dòng của PID đó (dòng đi theo
tiến trình dù thứ hạng %CPU thay đổi), cuộn tối thiểu để luôn nhìn thấy, và ép màu chọn Inactive = Active
(luôn xanh). Áp dụng cho cả bảng tiến trình, cây tiến trình và các bảng ở tab Mạng. Cây tiến trình còn nhớ
nhánh người dùng đã thu gọn (trước đây mỗi lần làm mới lại mở hết).

### 2) Kill được Firefox & các ứng dụng đa tiến trình
Firefox/Chrome gồm 1 tiến trình chính + nhiều tiến trình con; kill 1 PID chỉ sập 1 tab.
`ProcessTree` leo lên cha chừng nào cha còn **cùng file thực thi** (con của Firefox chạy lại chính
`firefox -contentproc ...`), rồi đóng cả cây. Có nút/menu chuột phải **"Đóng cả ứng dụng"** (SIGTERM, cha trước;
sau 3 giây tự SIGKILL những tiến trình chưa thoát, nhận diện bằng `starttime` để không nhầm PID tái sử dụng)
và **"Buộc đóng cả ứng dụng"** (SIGKILL, con trước cha sau). Tab Cây tiến trình có nút đóng cả nhánh.
An toàn: từ chối nếu cây chứa chính Process Manager; cảnh báo riêng với tiến trình hệ thống
(systemd, gnome-shell, Xorg...); shell/Python/Java... không bị leo lên; hiện danh sách tiến trình sẽ bị tác động.
Việc gửi tín hiệu vẫn gọi `kill_process()` gốc, mỗi lần đóng ứng dụng ghi 1 dòng vào log.

### 3) Icon hệ thống phía trước tiến trình
`IconProvider` dùng icon theme của desktop (`QIcon::fromTheme`) theo **file thực thi** (nên mọi
tiến trình con của Firefox cùng icon Firefox), có bảng ánh xạ cho shell, terminal, systemd, Xorg...,
icon riêng cho kernel thread và zombie, và dự phòng bằng icon chuẩn của Qt nên không bao giờ trống.

### 4) Tab "Mạng" mới
- Biểu đồ tốc độ ↓/↑ chạy thời gian thực (đo từ `/proc/net/dev`, chính xác cho mọi giao thức), bảng từng card mạng.
- Thông số: tốc độ hiện tại, tổng từ khi mở chương trình, đỉnh tốc độ, số kết nối ESTAB/LISTEN.
- Ứng dụng đang dùng mạng, **gộp theo ứng dụng**: số tiến trình, số kết nối, ESTAB/LISTEN/UDP, tốc độ ↓/↑,
  tổng nhận/gửi, có kết nối Internet hay không; lọc, tìm kiếm, đóng ứng dụng ngay từ đây.
- Chi tiết kết nối của ứng dụng đang chọn (giao thức, địa chỉ, dịch vụ HTTPS/DNS/SSH..., phạm vi
  Loopback/LAN/Internet, trạng thái, byte nhận/gửi) + biểu đồ lưu lượng riêng của ứng dụng đó.
- Bổ sung khác: biểu đồ CPU/RAM chạy ở tab Tổng quan, bấm tiêu đề cột để sắp xếp, menu chuột phải
  (xem kết nối mạng, sao chép PID/dòng lệnh), sửa nhãn "%%CPU" thành "%CPU".

## Giới hạn cần nói thật khi bảo vệ
- Tốc độ **theo từng ứng dụng** chỉ ước lượng được cho **TCP** (qua lệnh `ss`); UDP/QUIC không có bộ đếm byte
  theo socket trong `/proc`. Tốc độ tổng ở biểu đồ lớn thì chính xác.
- Thấy socket của user khác cần `sudo`; không có `ss` thì cột tốc độ hiện N/A (phần còn lại vẫn chạy).

## Đã kiểm chứng gì (trong sandbox, chạy thật)
| Phần | Kết quả |
|---|---|
| `NetMonitor` (parse /proc/net đúng đáp án biết trước, socket loopback thật → đúng PID, tính tốc độ) | 50/50 pass |
| `ProcessTree` (cây giả lập kiểu Firefox: tìm gốc, SIGTERM/SIGKILL thật, thứ tự, bảo vệ) | 21/21 pass |
| Cầu nối C/C++ + toàn bộ lõi C biên dịch `-Wall -Wextra` | 0 warning |
| `MainWindow`, `NetworkTab`, `LiveChart`, `IconProvider`, `ViewUtil`, `main_gui.cpp` | **CHƯA build được** (sandbox không có Qt) — mới rà soát tĩnh, bạn cần build thử |
