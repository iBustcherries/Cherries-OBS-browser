#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
	if (getenv("OBS_VKCAPTURE"))
		return 2;
	VkInstance instance;
	VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	VkResult result = vkCreateInstance(&info, NULL, &instance);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "Automatic Vulkan instance creation failed: %d\n", result);
		return 3;
	}
	vkDestroyInstance(instance, NULL);
	puts("Vulkan instance created with no capture launch environment or explicitly requested layers");
	return 0;
}
