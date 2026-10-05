#pragma once
#include <algorithm>
#include <cmath>

namespace CherriesPortraitGeometry {
struct Viewport {
	double x = 0, y = 0, scale = 0;
};
inline Viewport Fit(double canvasWidth, double canvasHeight, double width, double height)
{
	if (canvasWidth <= 0 || canvasHeight <= 0 || width <= 0 || height <= 0)
		return {};
	const double scale = std::min(width / canvasWidth, height / canvasHeight);
	return {(width - canvasWidth * scale) / 2, (height - canvasHeight * scale) / 2, scale};
}
inline double ResizeRatio(double startX, double startY, double x, double y)
{
	const double length = startX * startX + startY * startY;
	return length < 1 ? 1 : std::clamp((startX * x + startY * y) / length, 0.01, 100.0);
}
} // namespace CherriesPortraitGeometry
