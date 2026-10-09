#ifndef CORE_BRIDGE_H
#define CORE_BRIDGE_H

/* core_bridge.h: FILE MỚI, KHÔNG đụng tới bất kỳ file .h gốc nào trong src/.
 * Mục đích duy nhất: bọc extern "C" khi các header gốc (viết cho C thuần,
 * không có extern "C") được include từ code C++ (Qt) - nếu không bọc,
 * trình biên dịch C++ sẽ đổi tên hàm (name mangling) và linker sẽ KHÔNG
 * tìm thấy các hàm đã biên dịch sẵn bằng gcc (C) trong process_list.o,
 * proc_reader.o, utils.o, process_actions.o, logger.o -> lỗi "undefined
 * reference" khi link, dù code hoàn toàn đúng.
 *
 * Tư duy: các file .c gốc được biên dịch NGUYÊN VẸN bằng gcc như cũ (đúng
 * y lệnh trong Makefile gốc: -std=c11 -D_POSIX_C_SOURCE=200809L
 * -D_DEFAULT_SOURCE). GUI Qt chỉ IMPORT lại các hàm đó qua file cầu nối
 * này, không viết lại, không copy logic. */

#ifdef __cplusplus
extern "C" {
#endif

#include "../src/process_list.h"
#include "../src/proc_reader.h"
#include "../src/utils.h"
#include "../src/process_actions.h"
#include "../src/logger.h"

#ifdef __cplusplus
}
#endif

#endif
