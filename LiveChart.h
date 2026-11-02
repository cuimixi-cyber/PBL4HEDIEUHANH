#ifndef LIVECHART_H
#define LIVECHART_H

#include <QColor>
#include <QStringList>
#include <QVector>
#include <QWidget>

/* LiveChart: biểu đồ đường chạy theo thời gian thực, TỰ VẼ bằng QPainter
 * (không phụ thuộc module Qt Charts -> chỉ cần qtbase5-dev như phần còn lại).
 * Mẫu mới nhất nằm bên phải, biểu đồ trôi dần sang trái. Trục Y tự co giãn. */
class LiveChart : public QWidget {
public:
    enum class Unit { Percent, BytesPerSec, Count };

    explicit LiveChart(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setUnit(Unit unit);
    void setMaxPoints(int n);
    void setSampleIntervalSec(double sec);                  // chỉ dùng để ghi nhãn trục thời gian
    void setSeries(const QStringList &names, const QVector<QColor> &colors);
    void addSample(const QVector<double> &valuePerSeries);  // thêm 1 mẫu cho tất cả đường
    void setHistory(int series, const QVector<double> &values);   // thay toàn bộ lịch sử 1 đường
    void clearData();

    static QString formatBytes(double bytes);
    static QString formatRate(double bytesPerSec);

    QSize minimumSizeHint() const override { return QSize(220, 120); }
    QSize sizeHint() const override { return QSize(420, 180); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString formatValue(double v) const;
    double niceMax(double maxValue) const;
    void trim(QVector<double> &v) const;

    QString m_title;
    Unit m_unit = Unit::Percent;
    int m_maxPoints = 120;
    double m_intervalSec = 2.0;
    QStringList m_names;
    QVector<QColor> m_colors;
    QVector<QVector<double>> m_data;
};

#endif
