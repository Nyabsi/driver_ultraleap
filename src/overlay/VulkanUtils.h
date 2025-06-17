#pragma once

#include <vector>
#include <sstream>

#include <vulkan/vulkan.h>
#include <openvr.h>

#define VK_VALIDATE_RESULT(e)                                  \
    if (e != VK_SUCCESS)                                       \
        fprintf(stderr, "[vulkan] Error: VkResult = %d\n", e); \
    if (e > 0)                                                 \
        abort(); \

static auto GetVulkanInstanceExtensionsRequiredByOpenVR() -> std::vector<std::string>
{
    std::vector<std::string> result{};

    if (!vr::VRCompositor())
    {
        std::exit(EXIT_FAILURE);
    }

    auto IsExtensionAvailable = [](const std::vector<VkExtensionProperties>& properties, std::string extension) {
        for (const VkExtensionProperties& p : properties)
            if (strcmp(p.extensionName, extension.data()) == 0)
                return true;
        return false;
    };

    uint32_t properties_count;
    std::vector<VkExtensionProperties> properties;

    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);

    if (properties_count > 0)
    {
        properties.resize(properties_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.data());
    } else {
        std::exit(EXIT_FAILURE);
    }

    uint32_t buffer_len = vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(nullptr, 0);
    if (buffer_len > 0) 
    {
        std::vector<char> buffer(buffer_len + 1);
        vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(buffer.data(), buffer_len);
        buffer[buffer_len] = '\0';

        std::string token{};
        std::istringstream token_stream(buffer.data());
        while (std::getline(token_stream, token, ' ')) {
            if (IsExtensionAvailable(properties, token)) {
                printf("%s Instance Extension asked by OpenVR was available\n", token.data());
                result.push_back(token);
            } else {
                printf("ERROR! %s Instance Extension asked by OpenVR was NOT available\n", token.data());
                std::exit(EXIT_FAILURE);
            }
        }
    } else {
        std::exit(EXIT_FAILURE);
    }

    return result;
}

static auto GetVulkanDeviceExtensionsRequiredByOpenVR(const VkPhysicalDevice& device) -> std::vector<std::string> 
{
    std::vector<std::string> result{};

    if (!vr::VRCompositor()) {
        std::exit(EXIT_FAILURE);
    }

    auto IsExtensionAvailable = [](const std::vector<VkExtensionProperties>& properties, std::string extension) {
        for (const VkExtensionProperties& p : properties)
            if (strcmp(p.extensionName, extension.data()) == 0)
                return true;
        return false;
    };

    uint32_t properties_count;
    std::vector<VkExtensionProperties> properties;

    vkEnumerateDeviceExtensionProperties(device, nullptr, &properties_count, nullptr);

    if (properties_count > 0) {
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &properties_count, properties.data());
    } else {
        std::exit(EXIT_FAILURE);
    }

    uint32_t buffer_len = vr::VRCompositor()->GetVulkanDeviceExtensionsRequired(device, nullptr, 0);
    if (buffer_len > 0) {
        std::vector<char> buffer(buffer_len + 1);
        vr::VRCompositor()->GetVulkanDeviceExtensionsRequired(device, buffer.data(), buffer_len);
        buffer[buffer_len] = '\0';

        std::string token{};
        std::istringstream token_stream(buffer.data());
        while (std::getline(token_stream, token, ' ')) {
            if (IsExtensionAvailable(properties, token.data())) {
                printf("%s Instance Extension asked by OpenVR was available\n", token.data());
                result.push_back(token);
            } else {
                printf("ERROR! %s Instance Extension asked by OpenVR was NOT available\n", token.data());
                std::exit(EXIT_FAILURE);
            }
        }
    } else {
        std::exit(EXIT_FAILURE);
    }

    return result;
}