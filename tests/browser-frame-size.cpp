#include "panel/browser-panel-frame.hpp"

#include <cassert>

int main()
{
	for (qreal scale : {1.0, 1.25, 2.0}) {
		QSize logical(40, 24);
		QSize physical(qRound(logical.width() * scale), qRound(logical.height() * scale));
		QImage buffer(physical, QImage::Format_ARGB32_Premultiplied);
		buffer.fill(Qt::red);
		QImage frame = CopyBrowserFrame(buffer.constBits(), physical, logical, scale);
		assert(!frame.isNull());
		assert(frame.devicePixelRatio() == scale);
		// An old viewport frame must not acquire the new viewport's DPI/size.
		assert(CopyBrowserFrame(buffer.constBits(), physical, QSize(70, 40), scale).isNull());

		// A slightly delayed frame remains usable during continuous resizing,
        // but only at a recently requested size and the current DPI.
        std::deque<QSize> recent{QSize(70, 40), logical};
        assert(!CopyRecentBrowserFrame(buffer.constBits(), physical, recent, scale).isNull());
        assert(CopyRecentBrowserFrame(buffer.constBits(), physical, {QSize(70,40)}, scale).isNull());
        assert(CopyRecentBrowserFrame(buffer.constBits(), physical, recent, scale * 3).isNull());

		// Changing the canvas size must preserve the frame's logical extent.
		// Also exercise a popup's offset and painting to a high-DPI target.
		for (QSize canvasSize : {QSize(80, 60), QSize(25, 20)}) {
			QImage canvas(canvasSize * 2, QImage::Format_ARGB32_Premultiplied);
			canvas.setDevicePixelRatio(2);
			canvas.fill(Qt::blue);
			{
				QPainter painter(&canvas);
				PaintBrowserFrame(painter, QPointF(4, 3), frame);
			}
			assert(canvas.pixelColor(10, 10) == QColor(Qt::red));
			assert(canvas.pixelColor(0, 0) == QColor(Qt::blue));
			if (canvasSize.width() > 44) {
				assert(canvas.pixelColor(86, 20) == QColor(Qt::red));
				assert(canvas.pixelColor(90, 20) == QColor(Qt::blue));
			}
		}
	}
	qInfo("Browser frame resize and DPI regression checks passed");
}
