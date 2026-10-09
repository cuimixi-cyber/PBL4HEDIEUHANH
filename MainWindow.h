#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core_bridge.h" // extern "C" sang toan bo ham/struct goc trong src/

#include <QMainWindow>
#include <QTimer>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>

/* MainWindow: TOÀN BỘ là code MỚI cho giao diện Qt. KHÔNG chứa bất kỳ
 * thuật toán nào đã có trong src/ - mọi phép tính (%CPU, %MEM, sort,
 * search, kill, set priority, log) đều GỌI LẠI đúng hàm gốc qua
 * core_bridge.h. Lớp này chỉ có nhiệm vụ: hiển thị + nhận input + gọi hàm. */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshTick();          // goi dinh ky boi QTimer
    void onKillTerminateClicked();
    void onKillForceClicked();
    void onPriorityClicked();
    void onSearchTextChanged(const QString &text);
    void onSortFieldChanged(int index);
    void onSortOrderChanged(bool checked);
    void onAutoRefreshToggled(bool checked);
    void onIntervalChanged(int seconds);
    void onPerCoreModeToggled(bool checked);
    void onManualRefreshClicked();
    void onReloadLogClicked();
    void onTabChanged(int index);

private:
    void setupUi();
    QWidget *createOverviewTab();
    QWidget *createProcessListTab();
    QWidget *createTreeTab();
    QWidget *createLogTab();

    void rebuildView();            // ap dung filter (search_processes) tu m_curr -> m_view
    void populateTable();
    void populateTree();
    void updateOverviewLabels();
    void appendTreeChildren(QTreeWidgetItem *parentItem, pid_t parentPid, bool *visited);

    pid_t selectedPid() const;
    const ProcessInfo *findInCurrByPid(pid_t pid) const;
    SortField currentSortField() const;

    // ---- Du lieu: CUNG mot bo du lieu goc (ProcessArray/ProcessInfo) nhu ban TUI ----
    ProcessArray m_curr;   // snapshot day du, moi nhat
    ProcessArray m_prev;   // snapshot lan truoc, dung tinh %CPU
    ProcessArray m_view;   // ban hien thi (co the la ket qua loc cua m_curr)
    bool m_hasPrev = false;
    unsigned long long m_prevTotalCpu = 0;
    long m_totalMemKb = 0;
    double m_uptimeSec = 0.0;
    int m_numCores = 1;

    QString m_filterKeyword;
    pid_t m_selectedPid = -1;

    // ---- Widget ----
    QTabWidget *m_tabs = nullptr;
    QTimer *m_refreshTimer = nullptr;

    // Tab Tong quan
    QProgressBar *m_cpuBar = nullptr;
    QProgressBar *m_ramBar = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_uptimeLabel = nullptr;
    QCheckBox *m_autoRefreshCheck = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QCheckBox *m_perCoreCheck = nullptr;

    // Tab Danh sach tien trinh
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_sortFieldCombo = nullptr;
    QCheckBox *m_sortDescCheck = nullptr;
    QTableWidget *m_table = nullptr;
    QSpinBox *m_niceSpin = nullptr;
    QPushButton *m_refreshBtn = nullptr;

    // Tab Cay tien trinh
    QTreeWidget *m_treeWidget = nullptr;

    // Tab Nhat ky
    QTextEdit *m_logView = nullptr;
};

#endif
