#ifndef VIEWUTIL_H
#define VIEWUTIL_H

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QPalette>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>

/* Giữ màu xanh của dòng đang chọn kể cả khi bảng mất focus (vd sau khi bấm nút "Kill").
 * Mặc định Qt đổi sang màu xám ở nhóm màu Inactive, làm người dùng tưởng dòng chọn bị mất. */
inline void keepSelectionColorWhenInactive(QWidget *w) {
    QPalette p = w->palette();
    p.setColor(QPalette::Inactive, QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
    p.setColor(QPalette::Inactive, QPalette::HighlightedText, p.color(QPalette::Active, QPalette::HighlightedText));
    w->setPalette(p);
}

/* RAII bao quanh việc dựng lại toàn bộ 1 bảng mỗi chu kỳ làm mới.
 *
 * Nguyên nhân lỗi "thanh xanh biến mất": setRowCount(0) + nạp lại làm THANH CUỘN NHẢY VỀ ĐẦU
 * bảng, dòng đang chọn bị đẩy ra khỏi tầm nhìn dù vẫn còn được chọn. Lớp này:
 *   1) bật cờ *rebuilding để slot itemSelectionChanged bỏ qua các tín hiệu giả khi xoá/nạp lại,
 *   2) nhớ vị trí cuộn và khôi phục sau khi nạp xong,
 *   3) chọn lại đúng dòng của tiến trình (select(row)) và cuộn tối thiểu để nó luôn nhìn thấy,
 *   4) tắt vẽ lại trong lúc nạp để không nhấp nháy. */
class TableRebuildGuard {
public:
    TableRebuildGuard(QTableWidget *t, bool *rebuildingFlag)
        : m_t(t), m_flag(rebuildingFlag), m_scroll(t->verticalScrollBar()->value()) {
        *m_flag = true;
        m_t->setUpdatesEnabled(false);
    }
    void select(int row) {
        m_row = row;
        if (row >= 0) m_t->selectRow(row);
    }
    ~TableRebuildGuard() {
        restore(m_t, m_scroll, m_row);
        m_t->setUpdatesEnabled(true);
        *m_flag = false;
        // Khoảng cuộn của bảng có thể chưa kịp cập nhật ngay sau khi đổi số dòng, nên lặp lại 1 lần
        // ở vòng lặp sự kiện kế tiếp (0 ms, người dùng không thấy giật).
        QTableWidget *t = m_t;
        const int s = m_scroll, r = m_row;
        QTimer::singleShot(0, t, [t, s, r]() { restore(t, s, r); });
    }
    static void restore(QTableWidget *t, int scroll, int row) {
        t->verticalScrollBar()->setValue(scroll);
        if (row >= 0 && row < t->rowCount())
            t->scrollTo(t->model()->index(row, 0), QAbstractItemView::EnsureVisible);
    }
private:
    QTableWidget *m_t;
    bool *m_flag;
    int m_scroll;
    int m_row = -1;
};

#endif
