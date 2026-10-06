#include "packaging/gamecapture/CherriesCaptureEligibility.h"
#include <assert.h>
int main(void)
{
	assert(!cherries_automatic_vulkan_allowed(NULL, NULL, NULL));
	assert(!cherries_automatic_vulkan_allowed(NULL, "0", "000"));
	assert(!cherries_automatic_vulkan_allowed("0", "bad", "-1"));
	assert(cherries_automatic_vulkan_allowed("1", NULL, NULL));
	assert(cherries_automatic_vulkan_allowed(NULL, "588650", NULL));
	assert(cherries_automatic_vulkan_allowed(NULL, NULL, "7656119887766"));
}
