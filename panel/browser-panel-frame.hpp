#pragma once

#include <QImage>
#include <QPainter>
#include <QSize>
#include <cmath>

inline QImage CopyBrowserFrame(const void *buffer, QSize pixels, QSize logicalSize, qreal scale)
{
	if (!buffer || pixels.isEmpty() || logicalSize.isEmpty() || scale <= 0)
		return {};
	// CEF rounds its scaled viewport to physical pixels. Ignore frames from
	// an older viewport/DPI request; allow one pixel for rounding differences.
	if (std::abs(pixels.width() - std::ceil(logicalSize.width() * scale)) > 1 ||
	    std::abs(pixels.height() - std::ceil(logicalSize.height() * scale)) > 1)
		return {};
	QImage frame = QImage(static_cast<const uchar *>(buffer), pixels.width(), pixels.height(),
			      QImage::Format_ARGB32_Premultiplied)
			       .copy();
	frame.setDevicePixelRatio(scale);
	return frame;
}

inline void PaintBrowserFrame(QPainter &painter, QPointF origin, const QImage &frame)
{
	// Use the frame's own logical size. The widget clips excess pixels and
	// fills newly exposed space until CEF renders the resized viewport.
	if (!frame.isNull())
		painter.drawImage(origin, frame);
}
