#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QAbstractItemView>
#include <QTableWidgetItem>
#include <QFile>
#include <QTextStream>
#include <QBrush>
#include <QColor>
#include <QItemSelectionModel>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace Col {
    constexpr int Pid = 0, Name = 1, State = 2, Cpu = 3, Mem = 4,
                  Pri = 5, Nice = 6, User = 7, RunTime = 8, Count = 9;
}

static QString stateToVi(char s) {
    switch (s) {
        case 'R': return "Đang chạy";
        case 'S': return "Sleeping";
        case 'D': return "Chờ I/O";
        case 'Z': return "ZOMBIE";
        case 'T': return "Stopped";
        case 'I': return "Idle";
        default:  return "?";
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    parr_init(&m_curr);
    parr_init(&m_prev);
    parr_init(&m_view);

    if (read_total_memory_kb(&m_totalMemKb) != 0) m_totalMemKb = 0;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    m_numCores = (cores >= 1) ? (int)cores : 1;

    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTick);
    m_refreshTimer->start(2000); // mac dinh 2 giay, giong gia tri mac dinh trong main.c goc

    onRefreshTick(); // nap du lieu lan dau ngay khi mo, khong doi tick dau tien
}

MainWindow::~MainWindow() {
    parr_free(&m_curr);
    parr_free(&m_prev);
    parr_free(&m_view);
}

void MainWindow::setupUi() {
    setWindowTitle("Process Manager - Giao diện GUI (Qt)");
    resize(1150, 700);

    m_tabs = new QTabWidget(this);
    setCentralWidget(m_tabs);

    m_tabs->addTab(createOverviewTab(), "Tổng quan");
    m_tabs->addTab(createProcessListTab(), "Danh sách tiến trình");
    m_tabs->addTab(createTreeTab(), "Cây tiến trình");
    m_tabs->addTab(createLogTab(), "Nhật ký");

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
}

QWidget *MainWindow::createOverviewTab() {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QGroupBox *statsBox = new QGroupBox("Tình trạng hệ thống", page);
    QFormLayout *statsLayout = new QFormLayout(statsBox);

    m_cpuBar = new QProgressBar(statsBox);
    m_cpuBar->setRange(0, 100);
    statsLayout->addRow("CPU hệ thống:", m_cpuBar);

    m_ramBar = new QProgressBar(statsBox);
    m_ramBar->setRange(0, 100);
    statsLayout->addRow("RAM:", m_ramBar);

    m_summaryLabel = new QLabel(statsBox);
    statsLayout->addRow("Tổng số tiến trình:", m_summaryLabel);

    m_uptimeLabel = new QLabel(statsBox);
    statsLayout->addRow("Uptime hệ thống:", m_uptimeLabel);

    layout->addWidget(statsBox);

    QGroupBox *settingsBox = new QGroupBox("Cài đặt làm mới", page);
    QFormLayout *settingsLayout = new QFormLayout(settingsBox);

    m_autoRefreshCheck = new QCheckBox("Tự động làm mới", settingsBox);
    m_autoRefreshCheck->setChecked(true);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &MainWindow::onAutoRefreshToggled);
    settingsLayout->addRow(m_autoRefreshCheck);

    m_intervalSpin = new QSpinBox(settingsBox);
    m_intervalSpin->setRange(1, 3600);
    m_intervalSpin->setValue(2);
    m_intervalSpin->setSuffix(" giây");
    connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onIntervalChanged);
    settingsLayout->addRow("Chu kỳ làm mới:", m_intervalSpin);

    m_perCoreCheck = new QCheckBox(
        QString("Hiển thị %%CPU theo kiểu 'top' (nhân với %1 core)").arg(m_numCores), settingsBox);
    connect(m_perCoreCheck, &QCheckBox::toggled, this, &MainWindow::onPerCoreModeToggled);
    settingsLayout->addRow(m_perCoreCheck);

    layout->addWidget(settingsBox);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createProcessListTab() {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QHBoxLayout *toolbar = new QHBoxLayout();
    m_searchEdit = new QLineEdit(page);
    m_searchEdit->setPlaceholderText("Tìm theo tên/cmdline/PID chính xác...");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    toolbar->addWidget(new QLabel("Tìm:", page));
    toolbar->addWidget(m_searchEdit);

    m_sortFieldCombo = new QComboBox(page);
    m_sortFieldCombo->addItem("PID", SORT_PID);
    m_sortFieldCombo->addItem("%CPU", SORT_CPU);
    m_sortFieldCombo->addItem("%MEM", SORT_MEM);
    m_sortFieldCombo->addItem("Tên", SORT_NAME);
    m_sortFieldCombo->setCurrentIndex(1); // mac dinh %CPU, giong ban TUI
    connect(m_sortFieldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSortFieldChanged);
    toolbar->addWidget(new QLabel("Sắp xếp:", page));
    toolbar->addWidget(m_sortFieldCombo);

    m_sortDescCheck = new QCheckBox("Giảm dần", page);
    m_sortDescCheck->setChecked(true);
    connect(m_sortDescCheck, &QCheckBox::toggled, this, &MainWindow::onSortOrderChanged);
    toolbar->addWidget(m_sortDescCheck);

    m_refreshBtn = new QPushButton("Làm mới ngay", page);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onManualRefreshClicked);
    toolbar->addWidget(m_refreshBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_table = new QTableWidget(0, Col::Count, page);
    m_table->setHorizontalHeaderLabels({
        "PID", "Tên", "Trạng thái", "%CPU", "%MEM", "PRI", "NI", "User", "Thời gian chạy"
    });
    m_table->horizontalHeader()->setSectionResizeMode(Col::Name, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        pid_t p = selectedPid();
        if (p != -1) {
            m_selectedPid = p; // Ghi nhớ vào biến toàn cục của class
        }
    });
    layout->addWidget(m_table, /*stretch*/ 1);

    QGroupBox *controlBox = new QGroupBox("Thao tác với tiến trình đang chọn", page);
    QHBoxLayout *controlLayout = new QHBoxLayout(controlBox);

    QPushButton *termBtn = new QPushButton("Kết thúc (SIGTERM)", controlBox);
    connect(termBtn, &QPushButton::clicked, this, &MainWindow::onKillTerminateClicked);
    controlLayout->addWidget(termBtn);

    QPushButton *killBtn = new QPushButton("Buộc dừng (SIGKILL)", controlBox);
    killBtn->setStyleSheet("color:#b00020; font-weight:bold;");
    connect(killBtn, &QPushButton::clicked, this, &MainWindow::onKillForceClicked);
    controlLayout->addWidget(killBtn);

    controlLayout->addSpacing(16);
    controlLayout->addWidget(new QLabel("Nice value mới:", controlBox));
    m_niceSpin = new QSpinBox(controlBox);
    m_niceSpin->setRange(-20, 19);
    controlLayout->addWidget(m_niceSpin);

    QPushButton *applyNiceBtn = new QPushButton("Áp dụng", controlBox);
    connect(applyNiceBtn, &QPushButton::clicked, this, &MainWindow::onPriorityClicked);
    controlLayout->addWidget(applyNiceBtn);
    controlLayout->addStretch();

    layout->addWidget(controlBox);
    return page;
}

QWidget *MainWindow::createTreeTab() {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QLabel *hint = new QLabel(
        "Cây tiến trình dựng từ quan hệ PPID (cha-con) trên snapshot mới nhất.", page);
    layout->addWidget(hint);

    m_treeWidget = new QTreeWidget(page);
    m_treeWidget->setHeaderLabels({"PID", "Tên", "Trạng thái", "%CPU", "%MEM"});
    layout->addWidget(m_treeWidget, 1);
    return page;
}

QWidget *MainWindow::createLogTab() {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel("Nội dung process_manager.log (ghi bởi log_action() gốc):", page));
    toolbar->addStretch();
    QPushButton *reloadBtn = new QPushButton("Tải lại log", page);
    connect(reloadBtn, &QPushButton::clicked, this, &MainWindow::onReloadLogClicked);
    toolbar->addWidget(reloadBtn);
    layout->addLayout(toolbar);

    m_logView = new QTextEdit(page);
    m_logView->setReadOnly(true);
    m_logView->setFontFamily("monospace");
    layout->addWidget(m_logView, 1);
    return page;
}

SortField MainWindow::currentSortField() const {
    return static_cast<SortField>(m_sortFieldCombo->currentData().toInt());
}

void MainWindow::onRefreshTick() {
    read_all_processes(&m_curr);

    unsigned long long totalCurr = 0;
    unsigned long long delta = 0;
    if (read_total_cpu_time(&totalCurr) == 0 && m_hasPrev) {
        delta = (totalCurr > m_prevTotalCpu) ? (totalCurr - m_prevTotalCpu) : 0;
    }

    compute_cpu_percent(m_curr.items, m_curr.count, m_prev.items, m_prev.count, delta);
    compute_mem_percent(m_curr.items, m_curr.count, m_totalMemKb);
    sort_processes(&m_curr, currentSortField(), m_sortDescCheck->isChecked() ? 1 : 0);

    read_system_uptime(&m_uptimeSec);

    // luu snapshot hien tai lam moc cho lan sau (dung cach lam giong main.c goc:
    // deep-copy items[], KHONG gan ca struct ProcessArray)
    if (parr_reserve(&m_prev, m_curr.count > 0 ? m_curr.count : 1)) {
        memcpy(m_prev.items, m_curr.items, sizeof(ProcessInfo) * (size_t)m_curr.count);
        m_prev.count = m_curr.count;
    }
    m_prevTotalCpu = totalCurr;
    m_hasPrev = true;

    rebuildView();
    populateTable();
    populateTree();
    updateOverviewLabels();
}

void MainWindow::rebuildView() {
    if (!m_filterKeyword.isEmpty()) {
        search_processes(&m_curr, m_filterKeyword.toUtf8().constData(), &m_view);
    } else {
        if (parr_reserve(&m_view, m_curr.count > 0 ? m_curr.count : 1)) {
            memcpy(m_view.items, m_curr.items, sizeof(ProcessInfo) * (size_t)m_curr.count);
            m_view.count = m_curr.count;
        }
    }
}

void MainWindow::populateTable() {
    // Tạm thời chặn tín hiệu ngầm của Qt để nó không vô tình xóa biến m_selectedPid
    m_table->blockSignals(true); 

    m_table->setRowCount(0);
    double cpuMultiplier = (m_perCoreCheck && m_perCoreCheck->isChecked()) ? (double)m_numCores : 1.0;

    int rowToSelect = -1;

    for (int i = 0; i < m_view.count; i++) {
        const ProcessInfo &p = m_view.items[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto setCell = [&](int col, const QString &text) {
            m_table->setItem(row, col, new QTableWidgetItem(text));
        };

        setCell(Col::Pid, QString::number(p.pid));
        setCell(Col::Name, QString::fromUtf8(p.name));
        setCell(Col::State, stateToVi(p.state));

        double displayCpu = p.cpu_percent * cpuMultiplier;
        QTableWidgetItem *cpuItem = new QTableWidgetItem(QString::number(displayCpu, 'f', 1));
        m_table->setItem(row, Col::Cpu, cpuItem);
        if (displayCpu >= (cpuMultiplier > 1.0 ? 80.0 : 50.0)) {
            cpuItem->setBackground(QBrush(QColor(255, 205, 210)));
        }

        setCell(Col::Mem, QString::number(p.mem_percent, 'f', 1));
        setCell(Col::Pri, QString::number(p.priority));
        setCell(Col::Nice, QString::number(p.nice));
        setCell(Col::User, QString::fromUtf8(p.username));

        char timeBuf[32];
        format_run_time(p.starttime, m_uptimeSec, timeBuf, sizeof(timeBuf));
        setCell(Col::RunTime, QString::fromUtf8(timeBuf));

        if (p.state == 'Z') {
            for (int c = 0; c < Col::Count; c++) {
                if (auto *it = m_table->item(row, c)) it->setForeground(QBrush(QColor(200, 0, 0)));
            }
        }

        // Kiểm tra xem tiến trình này có khớp với PID đã được bộ nhớ lưu lại không
        if (p.pid == m_selectedPid) {
            rowToSelect = row;
        }
    }

    // Mở lại tín hiệu ngầm
    m_table->blockSignals(false);

    // Tiến hành bôi xanh lại dòng
    if (rowToSelect != -1) {
        m_table->selectRow(rowToSelect);
    }
}

void MainWindow::appendTreeChildren(QTreeWidgetItem *parentItem, pid_t parentPid, bool *visited) {
    for (int i = 0; i < m_curr.count; i++) {
        if (visited[i]) continue;
        if (m_curr.items[i].ppid != parentPid) continue;
        visited[i] = true;

        const ProcessInfo &p = m_curr.items[i];
        QTreeWidgetItem *item = new QTreeWidgetItem(parentItem);
        item->setText(0, QString::number(p.pid));
        item->setText(1, QString::fromUtf8(p.name));
        item->setText(2, stateToVi(p.state));
        item->setText(3, QString::number(p.cpu_percent, 'f', 1));
        item->setText(4, QString::number(p.mem_percent, 'f', 1));

        appendTreeChildren(item, p.pid, visited);
    }
}

void MainWindow::populateTree() {
    m_treeWidget->clear();
    if (m_curr.count == 0) return;

    bool *visited = new bool[m_curr.count];
    memset(visited, 0, sizeof(bool) * (size_t)m_curr.count);

    // Tim "re": tien trinh ma PPID khong ton tai trong danh sach hien tai
    for (int i = 0; i < m_curr.count; i++) {
        if (visited[i]) continue;

        bool ppidExists = false;
        for (int j = 0; j < m_curr.count; j++) {
            if (m_curr.items[j].pid == m_curr.items[i].ppid) { ppidExists = true; break; }
        }
        if (ppidExists) continue; // khong phai re, se duoc duyet toi boi cha cua no

        visited[i] = true;
        const ProcessInfo &p = m_curr.items[i];
        QTreeWidgetItem *rootItem = new QTreeWidgetItem(m_treeWidget);
        rootItem->setText(0, QString::number(p.pid));
        rootItem->setText(1, QString::fromUtf8(p.name));
        rootItem->setText(2, stateToVi(p.state));
        rootItem->setText(3, QString::number(p.cpu_percent, 'f', 1));
        rootItem->setText(4, QString::number(p.mem_percent, 'f', 1));

        appendTreeChildren(rootItem, p.pid, visited);
    }
    delete[] visited;
    m_treeWidget->expandAll();
    for (int c = 0; c < 5; c++) m_treeWidget->resizeColumnToContents(c);
}

void MainWindow::updateOverviewLabels() {
    double totalCpu = 0.0;
    long totalRss = 0;
    int zombieCount = 0;
    for (int i = 0; i < m_curr.count; i++) {
        totalCpu += m_curr.items[i].cpu_percent;
        totalRss += m_curr.items[i].vm_rss_kb;
        if (m_curr.items[i].state == 'Z') zombieCount++;
    }
    double memPercent = (m_totalMemKb > 0) ? (100.0 * totalRss / m_totalMemKb) : 0.0;

    m_cpuBar->setValue((int)(totalCpu > 100.0 ? 100.0 : totalCpu));
    m_cpuBar->setFormat(QString("%1%").arg(totalCpu, 0, 'f', 1));
    m_ramBar->setValue((int)(memPercent > 100.0 ? 100.0 : memPercent));
    m_ramBar->setFormat(QString("%1%  (%2/%3 MB)")
                         .arg(memPercent, 0, 'f', 1)
                         .arg(totalRss / 1024.0, 0, 'f', 0)
                         .arg(m_totalMemKb / 1024.0, 0, 'f', 0));

    QString summary = QString("%1 tiến trình").arg(m_curr.count);
    if (zombieCount > 0) summary += QString("  —  ⚠ %1 ZOMBIE").arg(zombieCount);
    if (!m_filterKeyword.isEmpty()) summary += QString("   (hiển thị %1 sau khi lọc)").arg(m_view.count);
    m_summaryLabel->setText(summary);

    long h = (long)m_uptimeSec / 3600;
    long mm = ((long)m_uptimeSec % 3600) / 60;
    long ss = (long)m_uptimeSec % 60;
    m_uptimeLabel->setText(QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0')));
}

pid_t MainWindow::selectedPid() const {
    QModelIndexList rows = m_table->selectionModel()
        ? m_table->selectionModel()->selectedRows()
        : QModelIndexList{};
    if (rows.isEmpty()) return -1;
    QTableWidgetItem *item = m_table->item(rows.first().row(), Col::Pid);
    return item ? (pid_t)item->text().toInt() : -1;
}

const ProcessInfo *MainWindow::findInCurrByPid(pid_t pid) const {
    for (int i = 0; i < m_curr.count; i++) {
        if (m_curr.items[i].pid == pid) return &m_curr.items[i];
    }
    return nullptr;
}

void MainWindow::onKillTerminateClicked() {
    pid_t pid = selectedPid();
    if (pid <= 0) { QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình trong bảng."); return; }

    const ProcessInfo *info = findInCurrByPid(pid);
    QString label = info
        ? QString("Kết thúc (SIGTERM) PID %1 (%2, user=%3)?").arg(pid).arg(info->name).arg(info->username)
        : QString("Kết thúc (SIGTERM) PID %1?").arg(pid);
    if (QMessageBox::question(this, "Xác nhận", label) != QMessageBox::Yes) return;

    ActionResult r = kill_process(pid, SIGTERM);
    log_action("KILL(SIGTERM)", pid, action_result_to_string(r));
    if (r != ACTION_OK) QMessageBox::warning(this, "Lỗi", action_result_to_string(r));
    onRefreshTick();
}

void MainWindow::onKillForceClicked() {
    pid_t pid = selectedPid();
    if (pid <= 0) { QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình trong bảng."); return; }

    const ProcessInfo *info = findInCurrByPid(pid);
    QString label = info
        ? QString("BUỘC DỪNG (SIGKILL) PID %1 (%2, user=%3)? Không thể hoàn tác.").arg(pid).arg(info->name).arg(info->username)
        : QString("BUỘC DỪNG (SIGKILL) PID %1? Không thể hoàn tác.").arg(pid);
    if (QMessageBox::question(this, "Xác nhận", label) != QMessageBox::Yes) return;

    ActionResult r = kill_process(pid, SIGKILL);
    log_action("KILL(SIGKILL)", pid, action_result_to_string(r));
    if (r != ACTION_OK) QMessageBox::warning(this, "Lỗi", action_result_to_string(r));
    onRefreshTick();
}

void MainWindow::onPriorityClicked() {
    pid_t pid = selectedPid();
    if (pid <= 0) { QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình trong bảng."); return; }

    int niceVal = m_niceSpin->value();
    ActionResult r = set_process_priority(pid, niceVal);
    log_action("SET_PRIORITY", pid, action_result_to_string(r));
    if (r != ACTION_OK) QMessageBox::warning(this, "Lỗi", action_result_to_string(r));
    onRefreshTick();
}

void MainWindow::onSearchTextChanged(const QString &text) {
    m_filterKeyword = text;
    rebuildView();
    populateTable();
    updateOverviewLabels();
}

void MainWindow::onSortFieldChanged(int) { onRefreshTick(); }
void MainWindow::onSortOrderChanged(bool) { onRefreshTick(); }

void MainWindow::onAutoRefreshToggled(bool checked) {
    if (checked) m_refreshTimer->start(m_intervalSpin->value() * 1000);
    else m_refreshTimer->stop();
}

void MainWindow::onIntervalChanged(int seconds) {
    if (m_autoRefreshCheck->isChecked()) m_refreshTimer->setInterval(seconds * 1000);
}

void MainWindow::onPerCoreModeToggled(bool) { populateTable(); }

void MainWindow::onManualRefreshClicked() { onRefreshTick(); }

void MainWindow::onReloadLogClicked() {
    QFile f("process_manager.log");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logView->setPlainText("(chưa có file log, hoặc chưa thực hiện thao tác nào)");
        return;
    }
    QTextStream in(&f);
    m_logView->setPlainText(in.readAll());
    m_logView->moveCursor(QTextCursor::End);
}

void MainWindow::onTabChanged(int) {
    // Lam moi ngay khi chuyen sang tab Nhat ky, tien loi khi demo
    if (m_tabs->currentWidget() == m_logView->parentWidget()) {
        onReloadLogClicked();
    }
}
