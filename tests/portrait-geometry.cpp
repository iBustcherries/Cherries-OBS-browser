#include "packaging/oauth/CherriesPortraitGeometry.hpp"
#include <cassert>
#include <cmath>
using namespace CherriesPortraitGeometry;
int main()
{
	// Resizing the dock changes letterboxing, not the portrait aspect ratio.
	auto tall = Fit(1080, 1920, 360, 640);
	assert(std::abs(tall.scale - 1.0 / 3) < 1e-9 && tall.x == 0 && tall.y == 0);
	auto wide = Fit(1080, 1920, 640, 360);
	assert(wide.x == 218.75 && wide.y == 0 && wide.scale == 0.1875);
	// Device-pixel scaling must preserve the mouse-to-canvas transform.
	auto hidpi = Fit(1080, 1920, 1280, 720);
	assert(hidpi.x == wide.x * 2 && hidpi.scale == wide.scale * 2);
	assert(std::abs(((wide.x + 100 * wide.scale) - wide.x) / wide.scale - 100) < 1e-9);
	assert(Fit(1080, 1920, 0, 360).scale == 0);
	assert(Fit(0, 1920, 640, 360).scale == 0);
	// Proportional resizing cannot invert, stretch, or divide by zero at the anchor.
	assert(ResizeRatio(100, 200, 200, 400) == 2);
	assert(ResizeRatio(100, 200, 50, 100) == .5);
	assert(ResizeRatio(100, 200, -100, -200) == .01);
	assert(ResizeRatio(0, 0, 100, 100) == 1);
}
