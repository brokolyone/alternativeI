#include "SparklineWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace gui {

SparklineWidget::SparklineWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(60);
}

void SparklineWidget::addSample(double value) {
    samples_.push_back(value);
    while (samples_.size() > capacity_) {
        samples_.pop_front();
    }
    update();
}

void SparklineWidget::setFixedMaximum(double max) {
    fixedMax_ = max;
    update();
}

void SparklineWidget::setColor(const QColor &color) {
    color_ = color;
    update();
}

void SparklineWidget::setCapacity(size_t capacity) {
    capacity_ = capacity == 0 ? 1 : capacity;
    while (samples_.size() > capacity_) {
        samples_.pop_front();
    }
}

QSize SparklineWidget::minimumSizeHint() const {
    return QSize(120, 60);
}

void SparklineWidget::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());

    if (samples_.empty()) {
        return;
    }

    double maxValue = fixedMax_;
    if (maxValue < 0.0) {
        maxValue = *std::max_element(samples_.begin(), samples_.end());
    }
    if (maxValue <= 0.0) {
        maxValue = 1.0; // avoid a div-by-zero flatline when everything's idle
    }

    const int w = width();
    const int h = height();
    const int n = static_cast<int>(samples_.size());

    QPolygonF linePoints;
    linePoints.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double x = n == 1 ? w : (static_cast<double>(i) / static_cast<double>(capacity_ - 1)) * w;
        const double normalized = std::min(1.0, samples_[static_cast<size_t>(i)] / maxValue);
        const double y = h - normalized * h;
        linePoints << QPointF(x, y);
    }

    QPainterPath fillPath;
    fillPath.moveTo(linePoints.first().x(), h);
    for (const QPointF &pt : linePoints) {
        fillPath.lineTo(pt);
    }
    fillPath.lineTo(linePoints.last().x(), h);
    fillPath.closeSubpath();

    QColor fillColor = color_;
    fillColor.setAlpha(60);
    painter.fillPath(fillPath, fillColor);

    QPen pen(color_);
    pen.setWidthF(1.5);
    painter.setPen(pen);
    painter.drawPolyline(linePoints);
}

} // namespace gui
