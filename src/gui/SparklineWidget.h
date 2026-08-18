#pragma once

#include <QColor>
#include <QWidget>
#include <deque>

namespace gui {

// Rolling history line/area chart, Process-Hacker-style: newest sample on
// the right, auto-scaling to the largest value currently in the window
// unless a fixed maximum is set (e.g. CPU% capped at 100).
class SparklineWidget : public QWidget {
    Q_OBJECT

public:
    explicit SparklineWidget(QWidget *parent = nullptr);

    void addSample(double value);
    void setFixedMaximum(double max);
    void setColor(const QColor &color);
    void setCapacity(size_t capacity);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::deque<double> samples_;
    size_t capacity_ = 120;
    double fixedMax_ = -1.0; // negative means auto-scale to the window's own max
    QColor color_ = QColor(80, 160, 255);
};

} // namespace gui
