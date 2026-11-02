#include "MainWindow.h"
#include "ProcessTree.h"
#include "ViewUtil.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
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
    resize(1250, 780);

    m_tabs = new QTabWidget(this);
    setCentralWidget(m_tabs);

    m_tabs->addTab(createOverviewTab(), "Tổng quan");
    m_tabs->addTab(createProcessListTab(), "Danh sách tiến trình");
    m_tabs->addTab(createTreeTab(), "Cây tiến trình");

    m_netTab = new NetworkTab(&m_icons, this);
    m_netTab->setProcessLookup([this](pid_t p) { return findInCurrByPid(p); });
    m_netTab->setAppRootResolver([this](pid_t p) { return proctree::findAppRoot(m_curr, p); });
    connect(m_netTab, &NetworkTab::closeAppRequested, this, &MainWindow::onCloseAppRequested);
    connect(m_netTab, &NetworkTab::showProcessRequested, this, &MainWindow::onShowProcessRequested);
    m_tabs->addTab(m_netTab, "Mạng");

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

    // Hai biểu đồ chạy thời gian thực: CPU và RAM
    QHBoxLayout *charts = new QHBoxLayout();
    m_cpuChart = new LiveChart(page);
    m_cpuChart->setTitle("CPU hệ thống");
    m_cpuChart->setUnit(LiveChart::Unit::Percent);
    m_cpuChart->setSeries({"CPU"}, {QColor("#e53935")});
    m_ramChart = new LiveChart(page);
    m_ramChart->setTitle("RAM");
    m_ramChart->setUnit(LiveChart::Unit::Percent);
    m_ramChart->setSeries({"RAM"}, {QColor("#1e88e5")});
    charts->addWidget(m_cpuChart);
    charts->addWidget(m_ramChart);
    layout->addLayout(charts, 1);

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

    // (da sua: ban cu viet "%%CPU" nen label hien 2 dau %)
    m_perCoreCheck = new QCheckBox(
        QString("Hiển thị %CPU theo kiểu 'top' (nhân với %1 core)").arg(m_numCores), settingsBox);
    connect(m_perCoreCheck, &QCheckBox::toggled, this, &MainWindow::onPerCoreModeToggled);
    settingsLayout->addRow(m_perCoreCheck);

    layout->addWidget(settingsBox);
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
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setIconSize(QSize(18, 18));
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    keepSelectionColorWhenInactive(m_table);   // thanh xanh khong chuyen xam khi bam sang nut khac
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MainWindow::onTableSelectionChanged);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onTableHeaderClicked);
    connect(m_table, &QWidget::customContextMenuRequested, this, &MainWindow::onTableContextMenu);
    layout->addWidget(m_table, /*stretch*/ 1);

    QGroupBox *controlBox = new QGroupBox("Thao tác với tiến trình đang chọn", page);
    QHBoxLayout *controlLayout = new QHBoxLayout(controlBox);

    QPushButton *termBtn = new QPushButton("Kết thúc (SIGTERM)", controlBox);
    termBtn->setToolTip("Gửi SIGTERM tới đúng 1 tiến trình đang chọn");
    connect(termBtn, &QPushButton::clicked, this, &MainWindow::onKillTerminateClicked);
    controlLayout->addWidget(termBtn);

    QPushButton *killBtn = new QPushButton("Buộc dừng (SIGKILL)", controlBox);
    killBtn->setStyleSheet("color:#b00020; font-weight:bold;");
    killBtn->setToolTip("Gửi SIGKILL tới đúng 1 tiến trình đang chọn");
    connect(killBtn, &QPushButton::clicked, this, &MainWindow::onKillForceClicked);
    controlLayout->addWidget(killBtn);

    controlLayout->addSpacing(12);
    QPushButton *closeAppBtn = new QPushButton("Đóng cả ứng dụng", controlBox);
    closeAppBtn->setToolTip("Tìm tiến trình gốc của ứng dụng (vd Firefox) và đóng toàn bộ tiến trình con/cháu.\n"
                            "Gửi SIGTERM trước, sau 3 giây tự buộc dừng những tiến trình chưa thoát.");
    connect(closeAppBtn, &QPushButton::clicked, this, &MainWindow::onCloseAppClicked);
    controlLayout->addWidget(closeAppBtn);

    QPushButton *forceAppBtn = new QPushButton("Buộc đóng cả ứng dụng", controlBox);
    forceAppBtn->setStyleSheet("color:#b00020; font-weight:bold;");
    forceAppBtn->setToolTip("Gửi SIGKILL tới toàn bộ cây tiến trình của ứng dụng");
    connect(forceAppBtn, &QPushButton::clicked, this, &MainWindow::onForceCloseAppClicked);
    controlLayout->addWidget(forceAppBtn);

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
    m_treePage = page;
    QVBoxLayout *layout = new QVBoxLayout(page);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->addWidget(new QLabel("Cây tiến trình dựng từ quan hệ PPID (cha-con). Chọn 1 nút rồi đóng cả nhánh:", page), 1);
    QPushButton *closeBtn = new QPushButton("Đóng cả nhánh (SIGTERM)", page);
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::onTreeCloseClicked);
    QPushButton *forceBtn = new QPushButton("Buộc đóng cả nhánh (SIGKILL)", page);
    forceBtn->setStyleSheet("color:#b00020; font-weight:bold;");
    connect(forceBtn, &QPushButton::clicked, this, &MainWindow::onTreeForceClicked);
    bar->addWidget(closeBtn);
    bar->addWidget(forceBtn);
    layout->addLayout(bar);

    m_treeWidget = new QTreeWidget(page);
    m_treeWidget->setHeaderLabels({"Tên", "PID", "Trạng thái", "%CPU", "%MEM"});
    m_treeWidget->setIconSize(QSize(18, 18));
    keepSelectionColorWhenInactive(m_treeWidget);
    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelectionChanged);
    // nho cac nut nguoi dung thu gon/mo de khong bi "mo het" lai sau moi lan lam moi
    connect(m_treeWidget, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *it) {
        if (!m_rebuildingTree) m_treeCollapsed.insert(it->data(0, Qt::UserRole).toInt());
    });
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) {
        if (!m_rebuildingTree) m_treeCollapsed.remove(it->data(0, Qt::UserRole).toInt());
    });
    layout->addWidget(m_treeWidget, 1);
    return page;
}

QWidget *MainWindow::createLogTab() {
    QWidget *page = new QWidget(this);
    m_logPage = page;
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

void MainWindow::onRefreshTick() { refreshAll(true); }

// fromTimer = true : lan lam moi dinh ky -> them mau vao bieu do + lay mau mang.
// fromTimer = false: lam moi do nguoi dung bam nut / doi sap xep / sau khi kill -> khong them mau
//                    bieu do (de truc thoi gian cua bieu do deu dan).
void MainWindow::refreshAll(bool fromTimer) {
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

    // luu snapshot hien tai lam moc cho lan sau (deep-copy items[], KHONG gan ca struct ProcessArray)
    if (parr_reserve(&m_prev, m_curr.count > 0 ? m_curr.count : 1)) {
        memcpy(m_prev.items, m_curr.items, sizeof(ProcessInfo) * (size_t)m_curr.count);
        m_prev.count = m_curr.count;
    }
    m_prevTotalCpu = totalCurr;
    m_hasPrev = true;

    rebuildView();
    populateTable();
    if (m_tabs->currentWidget() == m_treePage) populateTree();   // cay chi dung lai khi dang xem
    updateOverviewLabels(fromTimer);
    if (fromTimer) m_netTab->tick(m_tabs->currentWidget() == m_netTab);
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

// ---------------------------------------------------------------- bang tien trinh

void MainWindow::populateTable() {
    const double cpuMultiplier = (m_perCoreCheck && m_perCoreCheck->isChecked()) ? (double)m_numCores : 1.0;
    int rowToSelect = -1;
    {
        // Guard: bo qua tin hieu chon gia, GIU vi tri cuon, chon lai dung dong cua tien trinh
        TableRebuildGuard guard(m_table, &m_rebuildingTable);
        m_table->clearContents();
        m_table->setRowCount(m_view.count);

        for (int row = 0; row < m_view.count; row++) {
            const ProcessInfo &p = m_view.items[row];

            auto setCell = [&](int col, const QString &text, bool right = false) {
                QTableWidgetItem *it = new QTableWidgetItem(text);
                if (right) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_table->setItem(row, col, it);
                return it;
            };

            setCell(Col::Pid, QString::number(p.pid), true);
            QTableWidgetItem *nameItem = setCell(Col::Name, QString::fromUtf8(p.name));
            nameItem->setIcon(m_icons.iconFor(p));
            nameItem->setToolTip(QString::fromUtf8(p.cmdline));
            setCell(Col::State, stateToVi(p.state));

            const double displayCpu = p.cpu_percent * cpuMultiplier;
            QTableWidgetItem *cpuItem = setCell(Col::Cpu, QString::number(displayCpu, 'f', 1), true);
            if (displayCpu >= (cpuMultiplier > 1.0 ? 80.0 : 50.0)) {
                cpuItem->setBackground(QBrush(QColor(255, 205, 210)));
            }

            setCell(Col::Mem, QString::number(p.mem_percent, 'f', 1), true);
            setCell(Col::Pri, QString::number(p.priority), true);
            setCell(Col::Nice, QString::number(p.nice), true);
            setCell(Col::User, QString::fromUtf8(p.username));

            char timeBuf[32];
            format_run_time(p.starttime, m_uptimeSec, timeBuf, sizeof(timeBuf));
            setCell(Col::RunTime, QString::fromUtf8(timeBuf), true);

            if (p.state == 'Z') {
                for (int c = 0; c < Col::Count; c++) {
                    if (auto *it = m_table->item(row, c)) it->setForeground(QBrush(QColor(200, 0, 0)));
                }
            }
            if (p.pid == m_selectedPid) rowToSelect = row;
        }
        guard.select(rowToSelect);
    }
    // tien trinh dang chon da thoat han -> bo chon (con dang bi loc an thi van giu de nho)
    if (rowToSelect < 0 && m_selectedPid > 0 && !findInCurrByPid(m_selectedPid)) m_selectedPid = -1;
}

void MainWindow::onTableSelectionChanged() {
    if (m_rebuildingTable) return;       // tin hieu gia khi dang dung lai bang
    m_selectedPid = selectedPid();       // nguoi dung that su doi chon (ke ca bo chon -> -1)
}

pid_t MainWindow::selectedPid() const {
    QModelIndexList rows = m_table->selectionModel()
        ? m_table->selectionModel()->selectedRows()
        : QModelIndexList{};
    if (rows.isEmpty()) return -1;
    QTableWidgetItem *item = m_table->item(rows.first().row(), Col::Pid);
    return item ? (pid_t)item->text().toInt() : -1;
}

void MainWindow::onTableHeaderClicked(int column) {
    int field = -1;
    switch (column) {
        case Col::Pid:  field = SORT_PID;  break;
        case Col::Name: field = SORT_NAME; break;
        case Col::Cpu:  field = SORT_CPU;  break;
        case Col::Mem:  field = SORT_MEM;  break;
        default: return;
    }
    const int idx = m_sortFieldCombo->findData(field);
    if (idx == m_sortFieldCombo->currentIndex())
        m_sortDescCheck->setChecked(!m_sortDescCheck->isChecked());   // bam lai cung cot: dao chieu
    else
        m_sortFieldCombo->setCurrentIndex(idx);
}

void MainWindow::onTableContextMenu(const QPoint &pos) {
    QTableWidgetItem *it = m_table->itemAt(pos);
    if (!it) return;
    m_table->selectRow(it->row());       // slot chon se cap nhat m_selectedPid
    const pid_t pid = selectedPid();
    const ProcessInfo *info = findInCurrByPid(pid);
    if (!info) return;

    QMenu menu(this);
    QAction *aTerm = menu.addAction("Kết thúc (SIGTERM)");
    QAction *aKill = menu.addAction("Buộc dừng (SIGKILL)");
    menu.addSeparator();
    QAction *aApp = menu.addAction("Đóng cả ứng dụng");
    QAction *aForceApp = menu.addAction("Buộc đóng cả ứng dụng");
    menu.addSeparator();
    QAction *aNet = menu.addAction("Xem kết nối mạng");
    QAction *aCopyPid = menu.addAction("Sao chép PID");
    QAction *aCopyCmd = menu.addAction("Sao chép dòng lệnh");

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aTerm) onKillTerminateClicked();
    else if (chosen == aKill) onKillForceClicked();
    else if (chosen == aApp) onCloseAppClicked();
    else if (chosen == aForceApp) onForceCloseAppClicked();
    else if (chosen == aNet) {
        const int key = (int)proctree::findAppRoot(m_curr, pid);
        m_tabs->setCurrentWidget(m_netTab);          // onTabChanged se nap du lieu mang
        m_netTab->selectApp(key);
    }
    else if (chosen == aCopyPid) QApplication::clipboard()->setText(QString::number(pid));
    else if (chosen == aCopyCmd) QApplication::clipboard()->setText(QString::fromUtf8(info->cmdline));
}

// ---------------------------------------------------------------- cay tien trinh

QTreeWidgetItem *MainWindow::makeTreeItem(const ProcessInfo &p, QTreeWidgetItem *parent) {
    QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_treeWidget);
    item->setText(0, QString::fromUtf8(p.name));
    item->setIcon(0, m_icons.iconFor(p));
    item->setText(1, QString::number(p.pid));
    item->setText(2, stateToVi(p.state));
    item->setText(3, QString::number(p.cpu_percent, 'f', 1));
    item->setText(4, QString::number(p.mem_percent, 'f', 1));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
    item->setData(0, Qt::UserRole, (int)p.pid);
    item->setToolTip(0, QString::fromUtf8(p.cmdline));
    return item;
}

void MainWindow::appendTreeChildren(QTreeWidgetItem *parentItem, pid_t parentPid, bool *visited) {
    for (int i = 0; i < m_curr.count; i++) {
        if (visited[i]) continue;
        if (m_curr.items[i].ppid != parentPid) continue;
        visited[i] = true;
        QTreeWidgetItem *item = makeTreeItem(m_curr.items[i], parentItem);
        appendTreeChildren(item, m_curr.items[i].pid, visited);
    }
}

void MainWindow::populateTree() {
    const int scroll = m_treeWidget->verticalScrollBar()->value();
    m_rebuildingTree = true;
    m_treeWidget->setUpdatesEnabled(false);
    m_treeWidget->clear();

    if (m_curr.count > 0) {
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
            QTreeWidgetItem *rootItem = makeTreeItem(m_curr.items[i], nullptr);
            appendTreeChildren(rootItem, m_curr.items[i].pid, visited);
        }
        delete[] visited;
    }

    // Mo/thu gon theo trang thai nguoi dung da chon, khoi phuc dong dang chon
    QTreeWidgetItem *selItem = nullptr;
    QTreeWidgetItemIterator it(m_treeWidget);
    while (*it) {
        const int pid = (*it)->data(0, Qt::UserRole).toInt();
        (*it)->setExpanded(!m_treeCollapsed.contains(pid));
        if (pid == m_treeSelectedPid) selItem = *it;
        ++it;
    }
    for (int c = 0; c < 5; c++) m_treeWidget->resizeColumnToContents(c);
    if (selItem) m_treeWidget->setCurrentItem(selItem);
    m_treeWidget->verticalScrollBar()->setValue(scroll);
    if (selItem) m_treeWidget->scrollToItem(selItem, QAbstractItemView::EnsureVisible);

    m_treeWidget->setUpdatesEnabled(true);
    m_rebuildingTree = false;
}

void MainWindow::onTreeSelectionChanged() {
    if (m_rebuildingTree) return;
    const QList<QTreeWidgetItem *> sel = m_treeWidget->selectedItems();
    m_treeSelectedPid = sel.isEmpty() ? -1 : (pid_t)sel.first()->data(0, Qt::UserRole).toInt();
}

void MainWindow::onTreeCloseClicked() { killTreeAction(m_treeSelectedPid, false, false); }
void MainWindow::onTreeForceClicked() { killTreeAction(m_treeSelectedPid, true, false); }

// ---------------------------------------------------------------- tong quan

void MainWindow::updateOverviewLabels(bool pushChartSample) {
    double totalCpu = 0.0;
    long totalRss = 0;
    int zombieCount = 0;
    for (int i = 0; i < m_curr.count; i++) {
        totalCpu += m_curr.items[i].cpu_percent;
        totalRss += m_curr.items[i].vm_rss_kb;
        if (m_curr.items[i].state == 'Z') zombieCount++;
    }
    double memPercent = (m_totalMemKb > 0) ? (100.0 * totalRss / m_totalMemKb) : 0.0;
    if (totalCpu > 100.0) totalCpu = 100.0;
    if (memPercent > 100.0) memPercent = 100.0;

    m_cpuBar->setValue((int)totalCpu);
    m_cpuBar->setFormat(QString("%1%").arg(totalCpu, 0, 'f', 1));
    m_ramBar->setValue((int)memPercent);
    m_ramBar->setFormat(QString("%1%  (%2/%3 MB)")
                         .arg(memPercent, 0, 'f', 1)
                         .arg(totalRss / 1024.0, 0, 'f', 0)
                         .arg(m_totalMemKb / 1024.0, 0, 'f', 0));

    if (pushChartSample) {
        m_cpuChart->addSample({totalCpu});
        m_ramChart->addSample({memPercent});
    }

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

const ProcessInfo *MainWindow::findInCurrByPid(pid_t pid) const {
    for (int i = 0; i < m_curr.count; i++) {
        if (m_curr.items[i].pid == pid) return &m_curr.items[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------- kill 1 tien trinh (hanh vi cu)

void MainWindow::onKillTerminateClicked() {
    pid_t pid = selectedPid();
    if (pid <= 0) { QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình trong bảng."); return; }

    const ProcessInfo *info = findInCurrByPid(pid);
    QString label = info
        ? QString("Kết thúc (SIGTERM) PID %1 (%2, user=%3)?\n\nLưu ý: chỉ đóng đúng 1 tiến trình này. "
                  "Với ứng dụng nhiều tiến trình như Firefox hãy dùng \"Đóng cả ứng dụng\".")
              .arg(pid).arg(info->name).arg(info->username)
        : QString("Kết thúc (SIGTERM) PID %1?").arg(pid);
    if (QMessageBox::question(this, "Xác nhận", label) != QMessageBox::Yes) return;

    ActionResult r = kill_process(pid, SIGTERM);
    log_action("KILL(SIGTERM)", pid, action_result_to_string(r));
    if (r != ACTION_OK) QMessageBox::warning(this, "Lỗi", action_result_to_string(r));
    refreshAll(false);
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
    refreshAll(false);
}

void MainWindow::onPriorityClicked() {
    pid_t pid = selectedPid();
    if (pid <= 0) { QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình trong bảng."); return; }

    int niceVal = m_niceSpin->value();
    ActionResult r = set_process_priority(pid, niceVal);
    log_action("SET_PRIORITY", pid, action_result_to_string(r));
    if (r != ACTION_OK) QMessageBox::warning(this, "Lỗi", action_result_to_string(r));
    refreshAll(false);
}

// ---------------------------------------------------------------- dong ca ung dung (Firefox...)

void MainWindow::onCloseAppClicked() { killTreeAction(selectedPid(), false, true); }
void MainWindow::onForceCloseAppClicked() { killTreeAction(selectedPid(), true, true); }
void MainWindow::onCloseAppRequested(int pid, bool force) { killTreeAction((pid_t)pid, force, true); }

void MainWindow::onShowProcessRequested(int pid) {
    m_searchEdit->clear();                 // bo loc de chac chan thay duoc tien trinh
    m_selectedPid = (pid_t)pid;
    rebuildView();
    populateTable();
    m_tabs->setCurrentIndex(1);
}

void MainWindow::killTreeAction(pid_t pid, bool force, bool climbToAppRoot) {
    if (pid <= 1 || !findInCurrByPid(pid)) {
        QMessageBox::information(this, "Chưa chọn", "Vui lòng chọn 1 tiến trình hợp lệ.");
        return;
    }

    // Firefox/Chrome... gom nhieu tien trinh: leo len tien trinh goc roi dong ca cay
    const pid_t root = climbToAppRoot ? proctree::findAppRoot(m_curr, pid) : pid;
    const int ri = proctree::indexOfPid(m_curr, root);
    if (ri < 0) return;
    const ProcessInfo &rp = m_curr.items[ri];
    const std::vector<pid_t> tree = proctree::collectSubtree(m_curr, root);

    if (proctree::subtreeContains(m_curr, root, getpid())) {
        QMessageBox::warning(this, "Không thể thực hiện",
            "Cây tiến trình này chứa chính chương trình Process Manager (hoặc terminal đang chạy nó). "
            "Đóng nó sẽ tắt luôn chương trình này nên thao tác bị từ chối.");
        return;
    }

    QString text = QString("%1 \"%2\" (PID %3, user %4) cùng toàn bộ tiến trình con/cháu?\n\n"
                           "Tổng cộng sẽ tác động tới %5 tiến trình bằng %6.")
        .arg(force ? "BUỘC ĐÓNG ứng dụng" : "Đóng ứng dụng")
        .arg(QString::fromUtf8(rp.name)).arg(root).arg(QString::fromUtf8(rp.username))
        .arg((int)tree.size())
        .arg(force ? "SIGKILL (dừng ngay, không thể hoàn tác, có thể mất dữ liệu chưa lưu)"
                   : "SIGTERM (yêu cầu thoát êm)");
    if (!force)
        text += "\n\nNếu sau 3 giây vẫn còn tiến trình chưa thoát, chương trình sẽ tự động buộc dừng (SIGKILL) các tiến trình đó.";
    if (root != pid)
        text += QString("\n\nTiến trình bạn chọn (PID %1) chỉ là một phần của ứng dụng này.").arg(pid);
    if (proctree::isCriticalName(rp.name))
        text += "\n\n⚠ Đây là tiến trình hệ thống/phiên làm việc: đóng nó có thể làm văng phiên đăng nhập hoặc mất kết nối mạng.";

    QString detail = "PID\tTên\tUser\n";
    for (size_t i = 0; i < tree.size() && i < 80; i++) {
        const int ti = proctree::indexOfPid(m_curr, tree[i]);
        if (ti >= 0)
            detail += QString("%1\t%2\t%3\n").arg(tree[i]).arg(QString::fromUtf8(m_curr.items[ti].name))
                                              .arg(QString::fromUtf8(m_curr.items[ti].username));
    }
    if (tree.size() > 80) detail += "...\n";

    QMessageBox box(QMessageBox::Question, "Xác nhận", text, QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    box.setDetailedText(detail);
    if (box.exec() != QMessageBox::Yes) return;

    // moc starttime de sau nay nhan dien dung tien trinh (tranh nham khi PID duoc tai su dung)
    QVector<QPair<int, qulonglong>> targets;
    for (pid_t t : tree) {
        const int ti = proctree::indexOfPid(m_curr, t);
        if (ti >= 0) targets.append(qMakePair((int)t, (qulonglong)m_curr.items[ti].starttime));
    }

    const int sig = force ? SIGKILL : SIGTERM;
    // SIGKILL: con truoc cha sau (khong de lai mo coi). SIGTERM: cha truoc de ung dung tu thoat em.
    proctree::KillReport rep = proctree::killSubtree(m_curr, root, sig, /*leafFirst=*/force, getpid());

    const QString summary = QString("Gui %1 toi %2 tien trinh (%3 da thoat truoc, %4 loi)")
        .arg(force ? "SIGKILL" : "SIGTERM").arg((int)rep.sent.size()).arg((int)rep.gone.size()).arg((int)rep.failed.size());
    const QByteArray logResult = summary.toUtf8();
    log_action(force ? "KILL_APP(SIGKILL)" : "KILL_APP(SIGTERM)", root, logResult.constData());

    if (!rep.failed.empty()) {
        QString msg = QString("Không gửi được tín hiệu tới %1 tiến trình:\n").arg((int)rep.failed.size());
        for (const auto &f : rep.failed)
            msg += QString("  - PID %1: %2\n").arg(f.first).arg(action_result_to_string(f.second));
        QMessageBox::warning(this, "Một phần thất bại", msg);
    } else {
        statusBar()->showMessage(QString("Đã gửi %1 tới %2 tiến trình của \"%3\".")
            .arg(force ? "SIGKILL" : "SIGTERM").arg((int)rep.sent.size()).arg(QString::fromUtf8(rp.name)), 6000);
    }

    // SIGTERM chi la "yeu cau": sau 3 giay tien trinh nao con song thi buoc dung
    if (!force && !rep.sent.empty()) {
        QTimer::singleShot(3000, this, [this, targets, root]() { escalateKill(targets, root); });
    }
    refreshAll(false);
}

void MainWindow::escalateKill(const QVector<QPair<int, qulonglong>> &targets, pid_t root) {
    ProcessArray now;
    parr_init(&now);
    read_all_processes(&now);

    int killed = 0;
    for (const auto &t : targets) {
        const int i = proctree::indexOfPid(now, (pid_t)t.first);
        if (i < 0) continue;                                      // da thoat em
        const ProcessInfo &p = now.items[i];
        if ((qulonglong)p.starttime != t.second) continue;        // PID da bi tai su dung cho tien trinh khac
        if (p.state == 'Z') continue;                             // zombie: cho cha don, kill vo ich
        if (kill_process((pid_t)t.first, SIGKILL) == ACTION_OK) killed++;
    }
    parr_free(&now);

    if (killed > 0) {
        const QByteArray res = QString("SIGTERM khong du, da buoc dung %1 tien trinh con song").arg(killed).toUtf8();
        log_action("KILL_APP(SIGKILL-escalate)", root, res.constData());
        statusBar()->showMessage(QString("%1 tiến trình chưa chịu thoát sau SIGTERM nên đã bị buộc dừng (SIGKILL).").arg(killed), 6000);
    }
    refreshAll(false);
}

// ---------------------------------------------------------------- cac slot nho

void MainWindow::onSearchTextChanged(const QString &text) {
    m_filterKeyword = text;
    rebuildView();
    populateTable();
    updateOverviewLabels(false);
}

void MainWindow::onSortFieldChanged(int) { refreshAll(false); }
void MainWindow::onSortOrderChanged(bool) { refreshAll(false); }

void MainWindow::onAutoRefreshToggled(bool checked) {
    if (checked) m_refreshTimer->start(m_intervalSpin->value() * 1000);
    else m_refreshTimer->stop();
}

void MainWindow::onIntervalChanged(int seconds) {
    if (m_autoRefreshCheck->isChecked()) m_refreshTimer->setInterval(seconds * 1000);
    m_cpuChart->setSampleIntervalSec(seconds);
    m_ramChart->setSampleIntervalSec(seconds);
    m_netTab->setSampleIntervalSec(seconds);
}

void MainWindow::onPerCoreModeToggled(bool) { populateTable(); }

void MainWindow::onManualRefreshClicked() { refreshAll(false); }

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
    QWidget *cur = m_tabs->currentWidget();
    if (cur == m_logPage) onReloadLogClicked();        // tien loi khi demo
    else if (cur == m_treePage) populateTree();        // cay chi dung lai khi dang xem
    else if (cur == m_netTab) m_netTab->refreshNow();  // quet ket noi ngay khi mo tab Mang
}
