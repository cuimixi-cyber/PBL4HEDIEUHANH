#ifndef CORE_BRIDGE_H
#define CORE_BRIDGE_H

/* core_bridge.h: FILE MỚI, KHÔNG đụng tới bất kỳ file .h gốc nào trong src/.
 * Chỉ bọc extern "C" để C++ (Qt) gọi được các hàm C đã biên dịch bằng gcc.
 *
 * Dùng tên header "trần" (không có ../src/): thư mục chứa các file gốc được
 * thêm vào đường dẫn include bởi CMake (core_c PUBLIC include dir), nên cách
 * này đúng với cả 2 bố cục: src/ ngang hàng, hoặc ../src/ khi gui_qt là thư mục con. */

#ifdef __cplusplus
extern "C" {
#endif

#include "process_list.h"
#include "proc_reader.h"
#include "utils.h"
#include "process_actions.h"
#include "logger.h"

#ifdef __cplusplus
}
#endif

#endif
