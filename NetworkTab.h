#ifndef NETWORKTAB_H
#define NETWORKTAB_H

#include "IconProvider.h"
#include "LiveChart.h"
#include "NetMonitor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QTableWidget>
#include <QVector>
#include <QWidget>
#include <functional>
#include <vector>

/* NetworkTab: tab "Mạng" - quản lý mạng của các ứng dụng đang chạy.
 *   - Biểu đồ tốc độ tải xuống/tải lên theo thời gian thực (đo từ card mạng -> chính xác)
 *   - Bảng card mạng, các thông số tổng hợp (tốc độ, tổng phiên, đỉnh, số kết nối)
 *   - Bảng ứng dụng đang dùng mạng, GỘP theo ứng dụng (Firefox + các tiến trình con = 1 dòng)
 *   - Chi tiết kết nối của ứng dụng đang chọn + biểu đồ lưu lượng riêng của nó
 * Toàn bộ thu thập dữ liệu nằm ở NetMonitor (thuần C++, đã test); lớp này chỉ hiển thị. */
class NetworkTab : public QWidget {
    Q_OBJECT
public:
    explicit NetworkTab(IconProvider *icons, QWidget *parent = nullptr);

    void setProcessLookup(std::function<const ProcessInfo *(pid_t)> fn) { m_lookup = std::move(fn); }
    void setAppRootResolver(std::function<pid_t(pid_t)> fn) { m_appRoot = std::move(fn); }
    void setSampleIntervalSec(double sec);

    void tick(bool visible);        // gọi mỗi chu kỳ làm mới từ MainWindow
    void refreshNow();              // gọi khi người dùng mở tab
    void selectApp(int key);        // chọn sẵn 1 ứng dụng (key = PID gốc)

signals:
    void closeAppRequested(int pid, bool force);
    void showProcessRequested(int pid);

private slots:
    void onProcSelectionChanged();
    void onFilterChanged();
    void onCloseClicked();
    void onForceClicked();
    void onShowClicked();
    void onProcContextMenu(const QPoint &pos);

private:
    struct Group {
        int key = 0;                // PID gốc của ứng dụng (0 = không xác định)
        QString name;
        int procCount = 0;
        int total = 0, established = 0, listening = 0, udp = 0;
        double recvRate = 0.0, sendRate = 0.0;
        uint64_t recvTotal = 0, sendTotal = 0;
        bool hasInternet = false, hasTraffic = false;
    };

    void buildUi();
    void sampleAndRefreshConnections();
    void aggregate();
    void updateSummary();
    void updateIfaceTable();
    void updateProcTable();
    void updateConnTable();
    void updateProcChart();
    void updateNote();
    void updateButtons();
    QIcon iconForGroup(const Group &g) const;

    IconProvider *m_icons;
    netmon::NetMonitor m_net;
    std::function<const ProcessInfo *(pid_t)> m_lookup;
    std::function<pid_t(pid_t)> m_appRoot;

    std::vector<Group> m_groups;
    QHash<int, int> m_keyOfPid;                 // pid -> khoá nhóm
    QHash<int, QVector<double>> m_histRecv, m_histSend;
    QHash<int, int> m_histAge;
    int m_tick = 0;
    int m_selKey = -1;
    bool m_rebuildingProc = false;
    bool m_rebuildingConn = false;

    LiveChart *m_speedChart = nullptr;
    LiveChart *m_procChart = nullptr;
    QLabel *m_downLbl = nullptr, *m_upLbl = nullptr, *m_sessionLbl = nullptr, *m_peakLbl = nullptr, *m_connLbl = nullptr;
    QLabel *m_noteLbl = nullptr;
    QTableWidget *m_ifaceTable = nullptr;
    QTableWidget *m_procTable = nullptr;
    QTableWidget *m_connTable = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_groupCheck = nullptr;
    QCheckBox *m_onlyInternet = nullptr;
    QComboBox *m_protoCombo = nullptr;
    QComboBox *m_stateCombo = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_forceBtn = nullptr;
    QPushButton *m_showBtn = nullptr;
};

#endif
