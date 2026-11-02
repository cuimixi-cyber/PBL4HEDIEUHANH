#include "LiveChart.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <algorithm>
#include <cmath>

LiveChart::LiveChart(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(110);
    setAutoFillBackground(false);
}

void LiveChart::setTitle(const QString &title) { m_title = title; update(); }
void LiveChart::setUnit(Unit unit) { m_unit = unit; update(); }
void LiveChart::setSampleIntervalSec(double sec) { m_intervalSec = sec > 0 ? sec : 1.0; update(); }

void LiveChart::setMaxPoints(int n) {
    m_maxPoints = n < 2 ? 2 : n;
    for (auto &v : m_data) trim(v);
    update();
}

void LiveChart::setSeries(const QStringList &names, const QVector<QColor> &colors) {
    m_names = names;
    m_colors = colors;
    m_data.clear();
    m_data.resize(names.size());
    update();
}

void LiveChart::trim(QVector<double> &v) const {
    if (v.size() > m_maxPoints) v.remove(0, v.size() - m_maxPoints);
}

void LiveChart::addSample(const QVector<double> &values) {
    if (m_data.size() != m_names.size()) m_data.resize(m_names.size());
    for (int i = 0; i < m_data.size(); i++) {
        m_data[i].append(i < values.size() ? values[i] : 0.0);
        trim(m_data[i]);
    }
    update();
}

void LiveChart::setHistory(int series, const QVector<double> &values) {
    if (series < 0 || series >= m_data.size()) return;
    m_data[series] = values;
    trim(m_data[series]);
    update();
}

void LiveChart::clearData() {
    for (auto &v : m_data) v.clear();
    update();
}

QString LiveChart::formatBytes(double b) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (b >= 1024.0 && u < 4) { b /= 1024.0; ++u; }
    int decimals = (u == 0) ? 0 : (b < 10.0 ? 2 : 1);
    return QString::number(b, 'f', decimals) + " " + units[u];
}

QString LiveChart::formatRate(double bytesPerSec) { return formatBytes(bytesPerSec) + "/s"; }

QString LiveChart::formatValue(double v) const {
    switch (m_unit) {
        case Unit::Percent:     return QString::number(v, 'f', 1) + "%";
        case Unit::BytesPerSec: return formatRate(v);
        default:                return QString::number(v, 'f', 0);
    }
}

double LiveChart::niceMax(double maxValue) const {
    if (m_unit == Unit::Percent) return 100.0;
    const double floorValue = (m_unit == Unit::BytesPerSec) ? 10240.0 : 5.0;
    double v = maxValue * 1.15;
    if (v < floorValue) return floorValue;
    double mag = std::pow(10.0, std::floor(std::log10(v)));
    double f = v / mag;
    double nice = f <= 1.0 ? 1.0 : (f <= 2.0 ? 2.0 : (f <= 5.0 ? 5.0 : 10.0));
    return nice * mag;
}

void LiveChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette &pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));

    const int leftM = 72, rightM = 12, topM = 30, bottomM = 20;
    const QRectF plot(leftM, topM, width() - leftM - rightM, height() - topM - bottomM);
    if (plot.width() < 30 || plot.height() < 30) return;

    // tiêu đề + chú thích (giá trị hiện tại của từng đường)
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(pal.color(QPalette::Text));
    p.drawText(QPointF(8, 18), m_title);
    QFontMetrics fmBold(bold);
    double lx = 8 + fmBold.horizontalAdvance(m_title) + 24;
    p.setFont(font());
    QFontMetrics fm(font());
    for (int s = 0; s < m_names.size(); s++) {
        QColor c = s < m_colors.size() ? m_colors[s] : QColor(Qt::gray);
        double last = (s < m_data.size() && !m_data[s].isEmpty()) ? m_data[s].last() : 0.0;
        QString text = m_names[s] + ": " + formatValue(last);
        p.fillRect(QRectF(lx, 9, 10, 10), c);
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QPointF(lx + 14, 18), text);
        lx += 14 + fm.horizontalAdvance(text) + 18;
    }

    // trục Y
    double maxV = 0.0;
    for (const auto &d : m_data) for (double x : d) if (x > maxV) maxV = x;
    const double yMax = niceMax(maxV);

    QPen gridPen(pal.color(QPalette::Mid));
    gridPen.setStyle(Qt::DotLine);
    for (int i = 0; i <= 4; i++) {
        double y = plot.bottom() - plot.height() * i / 4.0;
        p.setPen(gridPen);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRectF(2, y - 8, leftM - 8, 16), Qt::AlignRight | Qt::AlignVCenter, formatValue(yMax * i / 4.0));
    }
    p.setPen(pal.color(QPalette::Mid));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // các đường dữ liệu: mẫu mới nhất ở mép phải
    const int n = m_maxPoints;
    const double stepX = plot.width() / (n - 1);
    for (int s = 0; s < m_data.size(); s++) {
        const QVector<double> &d = m_data[s];
        if (d.isEmpty()) continue;
        const int offset = n - d.size();
        QPainterPath line, area;
        for (int i = 0; i < d.size(); i++) {
            double x = plot.left() + (offset + i) * stepX;
            double y = plot.bottom() - plot.height() * std::min(d[i], yMax) / yMax;
            if (i == 0) {
                line.moveTo(x, y);
                area.moveTo(x, plot.bottom());
                area.lineTo(x, y);
            } else {
                line.lineTo(x, y);
                area.lineTo(x, y);
            }
        }
        area.lineTo(plot.left() + (offset + d.size() - 1) * stepX, plot.bottom());
        area.closeSubpath();

        QColor c = s < m_colors.size() ? m_colors[s] : QColor(Qt::gray);
        QColor fill = c;
        fill.setAlpha(45);
        p.fillPath(area, fill);
        p.setPen(QPen(c, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

    // nhãn trục thời gian
    p.setPen(pal.color(QPalette::Text));
    p.setFont(font());
    p.drawText(QRectF(plot.left(), plot.bottom() + 3, 120, 14), Qt::AlignLeft, QString("-%1 giây").arg((int)(m_intervalSec * (n - 1))));
    p.drawText(QRectF(plot.right() - 60, plot.bottom() + 3, 60, 14), Qt::AlignRight, "hiện tại");
}
