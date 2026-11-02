#include "NetworkTab.h"
#include "ViewUtil.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QSplitter>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace PCol {   // cột bảng ứng dụng
    constexpr int Name = 0, Pid = 1, Procs = 2, Total = 3, Estab = 4, Listen = 5, Udp = 6,
                  Recv = 7, Send = 8, RecvTotal = 9, SendTotal = 10, Internet = 11, Count = 12;
}

static QTableWidgetItem *mkItem(const QString &text, bool alignRight = false) {
    auto *it = new QTableWidgetItem(text);
    if (alignRight) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

static QTableWidget *makeTable(const QStringList &headers, QWidget *parent) {
    auto *t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    t->setIconSize(QSize(18, 18));
    keepSelectionColorWhenInactive(t);
    return t;
}

NetworkTab::NetworkTab(IconProvider *icons, QWidget *parent) : QWidget(parent), m_icons(icons) {
    buildUi();
}

void NetworkTab::setSampleIntervalSec(double sec) {
    m_speedChart->setSampleIntervalSec(sec);
}

void NetworkTab::buildUi() {
    auto *root = new QVBoxLayout(this);

    // ---- hàng thông số tổng hợp ----
    auto *summary = new QGroupBox("Tổng quan mạng (tất cả card mạng, không tính loopback)", this);
    auto *sl = new QHBoxLayout(summary);
    auto makeStat = [&]() {
        QLabel *l = new QLabel(summary);
        l->setTextFormat(Qt::RichText);
        l->setMinimumWidth(150);
        sl->addWidget(l, 1);
        return l;
    };
    m_downLbl = makeStat();
    m_upLbl = makeStat();
    m_sessionLbl = makeStat();
    m_peakLbl = makeStat();
    m_connLbl = makeStat();
    root->addWidget(summary);

    // ---- biểu đồ tốc độ + bảng card mạng ----
    auto *topSplit = new QSplitter(Qt::Horizontal, this);
    m_speedChart = new LiveChart(topSplit);
    m_speedChart->setTitle("Tốc độ mạng");
    m_speedChart->setUnit(LiveChart::Unit::BytesPerSec);
    m_speedChart->setSeries({"↓ Tải xuống", "↑ Tải lên"}, {QColor("#2e7d32"), QColor("#1565c0")});
    m_ifaceTable = makeTable({"Card mạng", "↓ Nhận/s", "↑ Gửi/s", "Tổng nhận", "Tổng gửi"}, topSplit);
    m_ifaceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    topSplit->addWidget(m_speedChart);
    topSplit->addWidget(m_ifaceTable);
    topSplit->setStretchFactor(0, 3);
    topSplit->setStretchFactor(1, 2);

    // ---- bảng ứng dụng đang dùng mạng ----
    auto *procBox = new QGroupBox("Ứng dụng đang sử dụng mạng", this);
    auto *pl = new QVBoxLayout(procBox);
    auto *bar = new QHBoxLayout();
    m_search = new QLineEdit(procBox);
    m_search->setPlaceholderText("Tìm theo tên ứng dụng hoặc PID...");
    bar->addWidget(m_search, 1);
    m_groupCheck = new QCheckBox("Gộp theo ứng dụng", procBox);
    m_groupCheck->setChecked(true);
    m_groupCheck->setToolTip("Gộp tiến trình chính và các tiến trình con (vd Firefox + Web Content...) thành 1 dòng");
    bar->addWidget(m_groupCheck);
    m_onlyInternet = new QCheckBox("Chỉ ứng dụng đang kết nối Internet", procBox);
    bar->addWidget(m_onlyInternet);
    m_closeBtn = new QPushButton("Đóng ứng dụng", procBox);
    m_closeBtn->setToolTip("Gửi SIGTERM tới cả ứng dụng (tự buộc dừng nếu sau 3 giây chưa thoát)");
    m_forceBtn = new QPushButton("Buộc đóng", procBox);
    m_forceBtn->setStyleSheet("color:#b00020; font-weight:bold;");
    m_showBtn = new QPushButton("Xem trong danh sách tiến trình", procBox);
    bar->addWidget(m_closeBtn);
    bar->addWidget(m_forceBtn);
    bar->addWidget(m_showBtn);
    pl->addLayout(bar);

    m_procTable = makeTable({"Ứng dụng", "PID", "Số tiến trình", "Kết nối", "ESTAB", "LISTEN", "UDP",
                             "↓ Nhận/s", "↑ Gửi/s", "Tổng nhận", "Tổng gửi", "Internet"}, procBox);
    m_procTable->horizontalHeader()->setSectionResizeMode(PCol::Name, QHeaderView::Stretch);
    m_procTable->setContextMenuPolicy(Qt::CustomContextMenu);
    pl->addWidget(m_procTable, 1);

    m_noteLbl = new QLabel(procBox);
    m_noteLbl->setWordWrap(true);
    pl->addWidget(m_noteLbl);

    // ---- chi tiết kết nối + biểu đồ riêng của ứng dụng đang chọn ----
    auto *botSplit = new QSplitter(Qt::Horizontal, this);
    auto *connBox = new QGroupBox("Kết nối của ứng dụng đang chọn", botSplit);
    auto *cl = new QVBoxLayout(connBox);
    auto *cbar = new QHBoxLayout();
    m_protoCombo = new QComboBox(connBox);
    m_protoCombo->addItems({"Mọi giao thức", "TCP", "UDP"});
    m_stateCombo = new QComboBox(connBox);
    m_stateCombo->addItems({"Mọi trạng thái", "ESTABLISHED", "LISTEN", "Khác (TIME_WAIT...)"});
    cbar->addWidget(new QLabel("Lọc:", connBox));
    cbar->addWidget(m_protoCombo);
    cbar->addWidget(m_stateCombo);
    cbar->addStretch();
    cl->addLayout(cbar);
    m_connTable = makeTable({"Giao thức", "Địa chỉ cục bộ", "Địa chỉ từ xa", "Dịch vụ", "Phạm vi", "Trạng thái", "Đã nhận", "Đã gửi"}, connBox);
    m_connTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    cl->addWidget(m_connTable, 1);

    m_procChart = new LiveChart(botSplit);
    m_procChart->setUnit(LiveChart::Unit::BytesPerSec);
    m_procChart->setSeries({"↓ Nhận", "↑ Gửi"}, {QColor("#2e7d32"), QColor("#ef6c00")});
    m_procChart->setTitle("Chọn một ứng dụng để xem lưu lượng");
    botSplit->addWidget(connBox);
    botSplit->addWidget(m_procChart);
    botSplit->setStretchFactor(0, 3);
    botSplit->setStretchFactor(1, 2);

    auto *vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->addWidget(topSplit);
    vsplit->addWidget(procBox);
    vsplit->addWidget(botSplit);
    vsplit->setStretchFactor(0, 2);
    vsplit->setStretchFactor(1, 3);
    vsplit->setStretchFactor(2, 2);
    root->addWidget(vsplit, 1);

    connect(m_procTable, &QTableWidget::itemSelectionChanged, this, &NetworkTab::onProcSelectionChanged);
    connect(m_procTable, &QWidget::customContextMenuRequested, this, &NetworkTab::onProcContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, &NetworkTab::onFilterChanged);
    connect(m_groupCheck, &QCheckBox::toggled, this, [this](bool) { aggregate(); onFilterChanged(); });
    connect(m_onlyInternet, &QCheckBox::toggled, this, [this](bool) { onFilterChanged(); });
    connect(m_protoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateConnTable(); });
    connect(m_stateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateConnTable(); });
    connect(m_closeBtn, &QPushButton::clicked, this, &NetworkTab::onCloseClicked);
    connect(m_forceBtn, &QPushButton::clicked, this, &NetworkTab::onForceClicked);
    connect(m_showBtn, &QPushButton::clicked, this, &NetworkTab::onShowClicked);

    updateSummary();
    updateButtons();
}

// ------------------------------------------------------------------ lấy mẫu

void NetworkTab::tick(bool visible) {
    m_net.sampleInterfaces();                                // rẻ: luôn lấy để biểu đồ liền mạch
    m_speedChart->addSample({m_net.totalRxRate(), m_net.totalTxRate()});
    if (visible) {
        updateSummary();
        updateIfaceTable();
        sampleAndRefreshConnections();
    }
}

void NetworkTab::refreshNow() {
    updateSummary();
    updateIfaceTable();
    sampleAndRefreshConnections();
}

void NetworkTab::sampleAndRefreshConnections() {
    m_net.sampleConnections(true);
    aggregate();
    updateProcTable();
    updateConnTable();
    updateProcChart();
    updateNote();
    updateSummary();
}

void NetworkTab::selectApp(int key) {
    m_selKey = key;
    updateProcTable();
    updateConnTable();
    updateProcChart();
    updateButtons();
}

void NetworkTab::aggregate() {
    m_groups.clear();
    m_keyOfPid.clear();
    QHash<int, int> idx;
    const bool grouping = m_groupCheck->isChecked();

    for (const auto &u : m_net.processUsage()) {
        int key = 0;
        if (u.pid > 0) key = (grouping && m_appRoot) ? (int)m_appRoot((pid_t)u.pid) : u.pid;
        m_keyOfPid.insert(u.pid, key);

        auto it = idx.find(key);
        if (it == idx.end()) {
            Group g;
            g.key = key;
            g.name = QString::fromStdString(u.name);
            if (key > 0 && key != u.pid && m_lookup) {       // tên lấy theo tiến trình gốc của ứng dụng
                if (const ProcessInfo *pi = m_lookup((pid_t)key)) g.name = QString::fromUtf8(pi->name);
            }
            it = idx.insert(key, (int)m_groups.size());
            m_groups.push_back(g);
        }
        Group &g = m_groups[it.value()];
        g.procCount++;
        g.total += u.total;
        g.established += u.established;
        g.listening += u.listening;
        g.udp += u.udp;
        g.recvRate += u.recvRate;
        g.sendRate += u.sendRate;
        g.recvTotal += u.recvTotal;
        g.sendTotal += u.sendTotal;
        g.hasInternet = g.hasInternet || u.hasInternet;
        g.hasTraffic = g.hasTraffic || u.hasTraffic;
    }

    std::sort(m_groups.begin(), m_groups.end(), [](const Group &a, const Group &b) {
        if ((a.key > 0) != (b.key > 0)) return a.key > 0;
        const double ra = a.recvRate + a.sendRate, rb = b.recvRate + b.sendRate;
        if (ra != rb) return ra > rb;
        if (a.established != b.established) return a.established > b.established;
        if (a.total != b.total) return a.total > b.total;
        return a.key < b.key;
    });

    // lịch sử lưu lượng theo từng ứng dụng (để vẽ biểu đồ riêng)
    m_tick++;
    for (const Group &g : m_groups) {
        if (g.key <= 0) continue;
        QVector<double> &r = m_histRecv[g.key];
        QVector<double> &s = m_histSend[g.key];
        r.append(g.recvRate);
        s.append(g.sendRate);
        if (r.size() > 120) r.remove(0, r.size() - 120);
        if (s.size() > 120) s.remove(0, s.size() - 120);
        m_histAge[g.key] = m_tick;
    }
    const QList<int> keys = m_histAge.keys();
    for (int k : keys) {
        if (m_tick - m_histAge.value(k) > 60) { m_histAge.remove(k); m_histRecv.remove(k); m_histSend.remove(k); }
    }
}

// ------------------------------------------------------------------ hiển thị

QIcon NetworkTab::iconForGroup(const Group &g) const {
    if (g.key > 0 && m_lookup) {
        if (const ProcessInfo *pi = m_lookup((pid_t)g.key)) return m_icons->iconFor(*pi);
    }
    return m_icons->iconForName(g.name);
}

void NetworkTab::updateSummary() {
    auto stat = [](const QString &title, const QString &value, const QString &color) {
        return QString("<span style='color:%1'>%2</span><br><span style='font-size:15pt;font-weight:bold'>%3</span>")
            .arg(color, title, value);
    };
    m_downLbl->setText(stat("↓ Tải xuống", LiveChart::formatRate(m_net.totalRxRate()), "#2e7d32"));
    m_upLbl->setText(stat("↑ Tải lên", LiveChart::formatRate(m_net.totalTxRate()), "#1565c0"));
    m_sessionLbl->setText(stat("Tổng từ khi mở chương trình",
        QString("↓ %1  ↑ %2").arg(LiveChart::formatBytes((double)m_net.sessionRxBytes()),
                                  LiveChart::formatBytes((double)m_net.sessionTxBytes())), "#555555"));
    m_peakLbl->setText(stat("Đỉnh tốc độ",
        QString("↓ %1  ↑ %2").arg(LiveChart::formatRate(m_net.peakRxRate()), LiveChart::formatRate(m_net.peakTxRate())), "#555555"));

    int est = 0, lis = 0;
    for (const auto &c : m_net.connections()) {
        if (c.state == "ESTABLISHED") est++;
        else if (c.state == "LISTEN") lis++;
    }
    m_connLbl->setText(stat("Kết nối (ESTAB / LISTEN)",
        QString("%1 / %2").arg(est).arg(lis), "#555555"));
}

void NetworkTab::updateIfaceTable() {
    const auto &ifs = m_net.interfaces();
    m_ifaceTable->setUpdatesEnabled(false);
    m_ifaceTable->setRowCount((int)ifs.size());
    for (int r = 0; r < (int)ifs.size(); r++) {
        const auto &i = ifs[r];
        QString name = QString::fromStdString(i.name);
        if (i.isLoopback) name += " (nội bộ)";
        m_ifaceTable->setItem(r, 0, mkItem(name));
        m_ifaceTable->setItem(r, 1, mkItem(LiveChart::formatRate(i.rxRate), true));
        m_ifaceTable->setItem(r, 2, mkItem(LiveChart::formatRate(i.txRate), true));
        m_ifaceTable->setItem(r, 3, mkItem(LiveChart::formatBytes((double)i.rxTotal), true));
        m_ifaceTable->setItem(r, 4, mkItem(LiveChart::formatBytes((double)i.txTotal), true));
    }
    m_ifaceTable->setUpdatesEnabled(true);
}

void NetworkTab::updateProcTable() {
    const QString kw = m_search->text().trimmed().toLower();
    const bool onlyNet = m_onlyInternet->isChecked();

    std::vector<const Group *> rows;
    for (const Group &g : m_groups) {
        if (onlyNet && !g.hasInternet) continue;
        if (!kw.isEmpty() && !g.name.toLower().contains(kw) && !QString::number(g.key).contains(kw)) continue;
        rows.push_back(&g);
    }

    int selRow = -1;
    {
        TableRebuildGuard guard(m_procTable, &m_rebuildingProc);
        m_procTable->clearContents();
        m_procTable->setRowCount((int)rows.size());
        for (int r = 0; r < (int)rows.size(); r++) {
            const Group &g = *rows[r];
            auto *nameItem = new QTableWidgetItem(g.name);
            nameItem->setIcon(iconForGroup(g));
            m_procTable->setItem(r, PCol::Name, nameItem);
            m_procTable->setItem(r, PCol::Pid, mkItem(g.key > 0 ? QString::number(g.key) : "—", true));
            m_procTable->setItem(r, PCol::Procs, mkItem(QString::number(g.procCount), true));
            m_procTable->setItem(r, PCol::Total, mkItem(QString::number(g.total), true));
            m_procTable->setItem(r, PCol::Estab, mkItem(QString::number(g.established), true));
            m_procTable->setItem(r, PCol::Listen, mkItem(QString::number(g.listening), true));
            m_procTable->setItem(r, PCol::Udp, mkItem(QString::number(g.udp), true));
            const bool ok = g.hasTraffic;
            m_procTable->setItem(r, PCol::Recv, mkItem(ok ? LiveChart::formatRate(g.recvRate) : "N/A", true));
            m_procTable->setItem(r, PCol::Send, mkItem(ok ? LiveChart::formatRate(g.sendRate) : "N/A", true));
            m_procTable->setItem(r, PCol::RecvTotal, mkItem(ok ? LiveChart::formatBytes((double)g.recvTotal) : "N/A", true));
            m_procTable->setItem(r, PCol::SendTotal, mkItem(ok ? LiveChart::formatBytes((double)g.sendTotal) : "N/A", true));
            m_procTable->setItem(r, PCol::Internet, mkItem(g.hasInternet ? "✔" : ""));
            if (g.key == m_selKey) selRow = r;
        }
        guard.select(selRow);
    }
    if (selRow < 0) {                                        // ứng dụng đã biến mất hẳn -> bỏ chọn
        bool exists = false;
        for (const Group &g : m_groups) if (g.key == m_selKey) exists = true;
        if (!exists) m_selKey = -1;
    }
    updateButtons();
}

void NetworkTab::updateConnTable() {
    std::vector<const netmon::Connection *> rows;
    if (m_selKey >= 0) {
        const int protoSel = m_protoCombo->currentIndex();   // 0 tất cả, 1 TCP, 2 UDP
        const int stateSel = m_stateCombo->currentIndex();   // 0 tất cả, 1 ESTAB, 2 LISTEN, 3 khác
        for (const auto &c : m_net.connections()) {
            const int key = m_keyOfPid.value(c.pid > 0 ? c.pid : 0, 0);
            if (key != m_selKey) continue;
            const bool tcp = c.proto.rfind("tcp", 0) == 0;
            if (protoSel == 1 && !tcp) continue;
            if (protoSel == 2 && tcp) continue;
            if (stateSel == 1 && c.state != "ESTABLISHED") continue;
            if (stateSel == 2 && c.state != "LISTEN") continue;
            if (stateSel == 3 && (c.state == "ESTABLISHED" || c.state == "LISTEN")) continue;
            rows.push_back(&c);
        }
        auto rank = [](const netmon::Connection *c) { return c->state == "ESTABLISHED" ? 0 : (c->state == "LISTEN" ? 1 : 2); };
        std::stable_sort(rows.begin(), rows.end(), [&](const netmon::Connection *a, const netmon::Connection *b) {
            return rank(a) < rank(b);
        });
    }

    auto endpoint = [](const std::string &ip, uint16_t port) {
        const QString i = QString::fromStdString(ip);
        return (i.contains(':') ? "[" + i + "]" : i) + ":" + QString::number(port);
    };

    TableRebuildGuard guard(m_connTable, &m_rebuildingConn);
    m_connTable->clearContents();
    m_connTable->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); r++) {
        const netmon::Connection &c = *rows[r];
        const bool listening = (c.state == "LISTEN" || c.state == "UNCONN");
        QString svc = QString::fromStdString(netmon::NetMonitor::serviceName(c.remotePort));
        if (svc.isEmpty() && listening) svc = QString::fromStdString(netmon::NetMonitor::serviceName(c.localPort));

        m_connTable->setItem(r, 0, mkItem(QString::fromStdString(c.proto).toUpper()));
        m_connTable->setItem(r, 1, mkItem(endpoint(c.localIp, c.localPort)));
        m_connTable->setItem(r, 2, mkItem(listening ? "—" : endpoint(c.remoteIp, c.remotePort)));
        m_connTable->setItem(r, 3, mkItem(svc));
        m_connTable->setItem(r, 4, mkItem(listening ? "" : QString::fromUtf8(netmon::scopeToString(c.remoteScope))));
        m_connTable->setItem(r, 5, mkItem(QString::fromStdString(c.state)));
        m_connTable->setItem(r, 6, mkItem(c.hasTraffic ? LiveChart::formatBytes((double)c.bytesRecv) : "—", true));
        m_connTable->setItem(r, 7, mkItem(c.hasTraffic ? LiveChart::formatBytes((double)c.bytesSent) : "—", true));
    }
}

void NetworkTab::updateProcChart() {
    if (m_selKey > 0 && m_histRecv.contains(m_selKey)) {
        QString name;
        for (const Group &g : m_groups) if (g.key == m_selKey) name = g.name;
        m_procChart->setTitle(QString("Lưu lượng TCP: %1 (PID %2)").arg(name).arg(m_selKey));
        m_procChart->setHistory(0, m_histRecv.value(m_selKey));
        m_procChart->setHistory(1, m_histSend.value(m_selKey));
    } else {
        m_procChart->setTitle(m_selKey == 0 ? "Nhóm không xác định: không có biểu đồ" : "Chọn một ứng dụng để xem lưu lượng");
        m_procChart->clearData();
    }
}

void NetworkTab::updateNote() {
    QString note;
    if (!m_net.trafficAvailable()) {
        note = "<span style='color:#b00020'>Không chạy được lệnh <b>ss</b> (gói iproute2) nên không đo được tốc độ theo ứng dụng "
               "(hiện N/A). Số kết nối và biểu đồ tổng ở trên vẫn đúng.</span>";
    } else {
        note = "Tốc độ theo ứng dụng là <b>ước lượng từ các kết nối TCP</b> (UDP/QUIC không có bộ đếm byte). "
               "Biểu đồ tổng phía trên đo trực tiếp từ card mạng nên chính xác cho mọi giao thức.";
    }
    if (m_net.unresolvedSocketCount() > 0) {
        note += QString(" &nbsp;|&nbsp; <span style='color:#ef6c00'>%1 socket thuộc tiến trình của user khác "
                        "(chạy bằng <b>sudo</b> để thấy đầy đủ).</span>").arg(m_net.unresolvedSocketCount());
    }
    m_noteLbl->setText(note);
}

void NetworkTab::updateButtons() {
    const bool app = m_selKey > 1;
    m_closeBtn->setEnabled(app);
    m_forceBtn->setEnabled(app);
    m_showBtn->setEnabled(app);
}

// ------------------------------------------------------------------ tương tác

void NetworkTab::onProcSelectionChanged() {
    if (m_rebuildingProc) return;                            // bỏ qua tín hiệu giả khi đang dựng lại bảng
    int key = -1;
    const QModelIndexList rows = m_procTable->selectionModel() ? m_procTable->selectionModel()->selectedRows() : QModelIndexList();
    if (!rows.isEmpty()) {
        QTableWidgetItem *it = m_procTable->item(rows.first().row(), PCol::Pid);
        if (it) key = it->text() == "—" ? 0 : it->text().toInt();
    }
    m_selKey = key;
    updateConnTable();
    updateProcChart();
    updateButtons();
}

void NetworkTab::onFilterChanged() {
    updateProcTable();
    updateConnTable();
    updateProcChart();
}

void NetworkTab::onCloseClicked() { if (m_selKey > 1) emit closeAppRequested(m_selKey, false); }
void NetworkTab::onForceClicked() { if (m_selKey > 1) emit closeAppRequested(m_selKey, true); }
void NetworkTab::onShowClicked() { if (m_selKey > 1) emit showProcessRequested(m_selKey); }

void NetworkTab::onProcContextMenu(const QPoint &pos) {
    QTableWidgetItem *it = m_procTable->itemAt(pos);
    if (!it) return;
    m_procTable->selectRow(it->row());
    if (m_selKey <= 1) return;

    QMenu menu(this);
    QAction *aClose = menu.addAction("Đóng ứng dụng (SIGTERM)");
    QAction *aForce = menu.addAction("Buộc đóng ứng dụng (SIGKILL)");
    menu.addSeparator();
    QAction *aShow = menu.addAction("Xem trong danh sách tiến trình");
    QAction *aCopy = menu.addAction("Sao chép PID");
    QAction *chosen = menu.exec(m_procTable->viewport()->mapToGlobal(pos));
    if (chosen == aClose) emit closeAppRequested(m_selKey, false);
    else if (chosen == aForce) emit closeAppRequested(m_selKey, true);
    else if (chosen == aShow) emit showProcessRequested(m_selKey);
    else if (chosen == aCopy) QApplication::clipboard()->setText(QString::number(m_selKey));
}
