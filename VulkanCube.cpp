#include "VulkanCube.hpp"
/*
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
*/

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "ShaderCompiler.hpp"

const uint32_t WIDTH = 1920;
const uint32_t HEIGHT = 1080;

//const std::string MODEL_PATH = "models/viking_room.obj";
//const std::string TEXTURE_PATH = "textures/viking_room.png";

const int MAX_FRAMES_IN_FLIGHT = 2;
static constexpr float s_fDet = 0.001f;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

static bool fEqual(float _1, float _2) {
    return std::abs(_1 - _2) < s_fDet;
}

static std::array<VkVertexInputBindingDescription, 2> getBindingDescription() {
    std::array<VkVertexInputBindingDescription, 2> bindingDescription{};
    bindingDescription[0].binding = 0;
    bindingDescription[0].stride = sizeof(glm::vec3);
    bindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindingDescription[1].binding = 1;
    bindingDescription[1].stride = sizeof(glm::vec3);
    bindingDescription[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    return bindingDescription;
}

static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 1;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = 0;

    return attributeDescriptions;
}

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, 
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

static bool isSamePointF(const glm::vec3& p1, const glm::vec3& p2) {
    if (std::abs(p1.x - p2.x) <= s_fDet && std::abs(p1.y - p2.y) <= s_fDet && std::abs(p1.z - p2.z) <= s_fDet)
        return true;
    return false;
}

/// <summary>
/// 
/// </summary>
VulkanCube::VulkanCube()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
    SetConsoleOutputCP(CP_UTF8);
#endif
    spdlog::set_default_logger(spdlog::stdout_color_mt("console", spdlog::color_mode::automatic));
#ifdef NDEBUG
    spdlog::set_level(spdlog::level::info);
#else
    spdlog::set_level(spdlog::level::debug);
#endif
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    initWindow();
    initVulkan();
}

VulkanCube::~VulkanCube()
{
    cleanup();
}

void VulkanCube::run() 
{
    spdlog::info("按 空格键 展开或折叠立方体");
    spdlog::info("按 S 暂停或继续动画");
    spdlog::info("按 R 居中展开图的位置");
    spdlog::info("展开时, 鼠标左键点击两个正方形, 将会自动验证第一个点击的正方形能否滚动到第二个选中的正方形旁, 如果验证通过会播放动画, 验证没通过则会提示错误");
    int width = 0, height = 0;
    bool minimized = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window, &width, &height);
        minimized = (width == 0 || height == 0);

        if (minimized && !previousWindowMinimizedStatus) {
            previousWindowMinimizedStatus = true;
            currentTime = std::chrono::high_resolution_clock::now();
            periodTimeMS += std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
            glfwWaitEvents();
            continue;
        }
        else if (!minimized && previousWindowMinimizedStatus) {
            startTime = std::chrono::high_resolution_clock::now();
            previousWindowMinimizedStatus = false;
        }

        processAnimation();

        drawFrame();
    }

    vkDeviceWaitIdle(device);
}

void VulkanCube::processAnimation()
{
    if (isPaused || previousWindowMinimizedStatus) return;
    if (animationQueues.empty()) {
        if (rotating && is2D) {
            rotating = false;
            initUBO();
        }
        return;
    }
    const auto& animation = animationQueues.front();
    float clockWiseR = animation.clockWise ? -1.f : 1.f;
    if (!moving) {
        interactive = animation.interactive;
        moving = true;
        startTime = std::chrono::high_resolution_clock::now();
        moved_vertices = vertices;
    }
    currentTime = std::chrono::high_resolution_clock::now();
    auto timeI = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() + periodTimeMS;
    if (timeI == 0) return;
    if (timeI > s_msCount) {
        moving = false;
        interactive = false;
        periodTimeMS = 0;
        for (const uint32_t faceId : animation.faceIds) {
            for (uint32_t i = 0; i < 4u; ++i) {
                size_t id = faceId * 4u + i;
                vertices[id] = glm::vec3(std::round(moved_vertices[id].x), std::round(moved_vertices[id].y), std::round(moved_vertices[id].z));
            }
        }
        memcpy(vertexBufferMappedPtr, vertices.data(), vertices.size() * sizeof(vertices[0]));
        animationQueues.pop();
    }
    else {
        float time = static_cast<float>(timeI) / 1000.f;
        glm::mat4 transformMat = glm::translate(glm::mat4(1.f), animation.rotateCenter);
        transformMat = glm::rotate(transformMat, clockWiseR * time * glm::radians(30.f), animation.rotateAxis);
        transformMat = glm::translate(transformMat, -animation.rotateCenter);
        for (const uint32_t faceId : animation.faceIds) {
            for (uint32_t i = 0; i < 4u; ++i) {
                size_t id = faceId * 4u + i;
                moved_vertices[id] = glm::vec3(transformMat * glm::vec4(vertices[id], 1.f));
            }
        }
        memcpy(vertexBufferMappedPtr, moved_vertices.data(), moved_vertices.size() * sizeof(moved_vertices[0]));
    }

}

void VulkanCube::resetFaceToCenter(TranslateType mask)
{
    glm::vec3 translateDis(0.f);
    if (mask == TranslateType::Flod) {
        const auto& edges = getEdges(0);
        translateDis = glm::vec3(0.f, -1.f, 0.f) - vertices[edges[0][0]];
    }
    else if (mask == TranslateType::Open) {
        translateDis = glm::vec3(-4.f, 0.f, 0.f);
    }
    else {
        if (!is2D) return;
        std::array<float, 4> minmax{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::lowest(),
        };
        for (const auto& v : vertices) {
            if (minmax[0] > v.x) minmax[0] = v.x;
            if (minmax[1] < v.x) minmax[1] = v.x;
            if (minmax[2] > v.y) minmax[2] = v.y;
            if (minmax[3] < v.y) minmax[3] = v.y;
        }

        if (minmax[0] < -4.f) {
            translateDis.x = -4.f - minmax[0];
            assert(minmax[1] < 4.f);
        }
        else if (minmax[1] > 4.f) {
            translateDis.x = 4.f - minmax[1];
            assert(minmax[0] > -4.f);
        }

        if (minmax[2] < -5.f) {
            translateDis.y = -5.f - minmax[2];
            assert(minmax[3] < 5.f);
        }
        else if (minmax[3] > 5.f) {
            translateDis.y = 5.f - minmax[3];
            assert(minmax[2] > -5.f);
        }
    }

    if (translateDis == glm::vec3(0.f)) return;

    for (auto& v : vertices) {
        v += translateDis;
    }
    memcpy(vertexBufferMappedPtr, vertices.data(), sizeof(vertices[0]) * vertices.size());

    spdlog::debug("Reset face position to center!");
}

void VulkanCube::addCubeAnimation()
{
    if (is2D) {
        const auto faceInfos = getAdjacentInfo();

        resetFaceToCenter(TranslateType::Flod);

        Animation animation;
        for (int dirValue = 0; dirValue < 4; ++dirValue) {
            auto adjInfos = getAllAdjacentFaces(faceInfos, 0, static_cast<Direction>(dirValue));
            std::array<std::vector<uint32_t>, 6> childrenList;
            for (size_t i = adjInfos.size(); i > 0; --i) {
                const auto& v = adjInfos[i - 1];
                childrenList[v.AdjacentId].push_back(static_cast<uint32_t>(v.AdjacentId));
                childrenList[v.faceId].insert(childrenList[v.faceId].end(), childrenList[v.AdjacentId].begin(), childrenList[v.AdjacentId].end());
                switch (v.direction) {
                case Direction::Left:
                    animation.rotateAxis = Animation::yAxis;
                    animation.rotateCenter = vertices[faceInfos[v.faceId].edges[static_cast<size_t>(Direction::Left)][0]];
                    animation.clockWise = true;
                    break;
                case Direction::Right:
                    animation.rotateAxis = Animation::yAxis;
                    animation.rotateCenter = vertices[faceInfos[v.faceId].edges[static_cast<size_t>(Direction::Right)][0]];
                    animation.clockWise = false;
                    break;
                case Direction::Top:
                    animation.rotateAxis = Animation::xAxis;
                    animation.rotateCenter = vertices[faceInfos[v.faceId].edges[static_cast<size_t>(Direction::Top)][0]];
                    animation.clockWise = true;
                    break;
                case Direction::Bottom:
                    animation.rotateAxis = Animation::xAxis;
                    animation.rotateCenter = vertices[faceInfos[v.faceId].edges[static_cast<size_t>(Direction::Bottom)][0]];
                    animation.clockWise = false;
                    break;
                default:
                    break;
                }
                animation.faceIds = childrenList[v.AdjacentId];
                animationQueues.push(animation);
            }
        }

        rotating = true;
        rotateStartTime = std::chrono::high_resolution_clock::now();
    }
    else {
        initUBO();
        const auto faceIDs = getDirectionFaceIds();
        resetFaceToCenter(TranslateType::Open);

        Animation animation;
        animation.faceIds = { faceIDs[static_cast<size_t>(Direction::Bottom)], faceIDs[static_cast<size_t>(Direction::Right)], 
            faceIDs[static_cast<size_t>(Direction::Back)], faceIDs[static_cast<size_t>(Direction::Left)], faceIDs[static_cast<size_t>(Direction::Top)] };
        animation.rotateAxis = Animation::yAxis;
        animation.rotateCenter = glm::vec3(-2.f, -1.f, 0.f);
        animation.clockWise = true;
        animationQueues.push(animation);

        animation.faceIds = { faceIDs[static_cast<size_t>(Direction::Bottom)] };
        animation.rotateAxis = Animation::xAxis;
        animation.rotateCenter = glm::vec3(0.f, -1.f, 0.f);
        animation.clockWise = true;
        animationQueues.push(animation);

        animation.faceIds = { faceIDs[static_cast<size_t>(Direction::Back)], faceIDs[static_cast<size_t>(Direction::Left)], 
            faceIDs[static_cast<size_t>(Direction::Top)] };
        animation.rotateAxis = Animation::yAxis;
        animation.rotateCenter = glm::vec3(0.f, -1.f, 0.f);
        animation.clockWise = true;
        animationQueues.push(animation);

        animation.faceIds = { faceIDs[static_cast<size_t>(Direction::Left)] };
        animation.rotateAxis = Animation::yAxis;
        animation.rotateCenter = glm::vec3(2.f, 1.f, 0.f);
        animation.clockWise = true;
        animationQueues.push(animation);

        animation.faceIds = { faceIDs[static_cast<size_t>(Direction::Top)] };
        animation.rotateAxis = Animation::xAxis;
        animation.rotateCenter = glm::vec3(2.f, 1.f, 0.f);
        animation.clockWise = false;
        animationQueues.push(animation);
    }
    is2D = !is2D;
}

std::array<VulkanCube::FaceInfo, 6> VulkanCube::getAdjacentInfo() const noexcept
{
    std::array<FaceInfo, 6> result{};
    for (size_t i = 0; i < 6; ++i) {
        result[i].edges = getEdges(i);
    }

    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 6; ++j) {
            if (i != j) {
                if (isSamePointF(vertices[result[i].edges[0][0]], vertices[result[j].edges[1][0]]) &&
                    isSamePointF(vertices[result[i].edges[0][1]], vertices[result[j].edges[1][1]])) {
                    result[i].left = j;
                    continue;
                }

                if (isSamePointF(vertices[result[i].edges[1][0]], vertices[result[j].edges[0][0]]) &&
                    isSamePointF(vertices[result[i].edges[1][1]], vertices[result[j].edges[0][1]])) {
                    result[i].right = j;
                    continue;
                }

                if (isSamePointF(vertices[result[i].edges[2][0]], vertices[result[j].edges[3][0]]) &&
                    isSamePointF(vertices[result[i].edges[2][1]], vertices[result[j].edges[3][1]])) {
                    result[i].top = j;
                    continue;
                }

                if (isSamePointF(vertices[result[i].edges[3][0]], vertices[result[j].edges[2][0]]) &&
                    isSamePointF(vertices[result[i].edges[3][1]], vertices[result[j].edges[2][1]])) {
                    result[i].bottom = j;
                    continue;
                }
            }
        }
    }

    return result;
}

std::array<std::array<size_t, 2>, 4> VulkanCube::getEdges(size_t faceId) const noexcept
{
    const size_t startIndex = faceId * 4;
    std::array<std::array<size_t, 2>, 4> edges;

    // 获取当前面的顶点索引
    std::array<size_t, 4> ids = {
        startIndex,
        startIndex + 1,
        startIndex + 2,
        startIndex + 3
    };

    // 按X坐标排序
    std::sort(ids.begin(), ids.end(), [this](size_t a, size_t b) {
        return vertices[a].x < vertices[b].x;
        });

    // 处理左列（X较小的两个顶点）
    size_t leftBottom = ids[0];
    size_t leftTop = ids[1];
    if (vertices[leftBottom].y > vertices[leftTop].y) {
        std::swap(leftBottom, leftTop);
    }

    // 处理右列（X较大的两个顶点）
    size_t rightBottom = ids[2];
    size_t rightTop = ids[3];
    if (vertices[rightBottom].y > vertices[rightTop].y) {
        std::swap(rightBottom, rightTop);
    }

    // 定义四条边
    edges[0] = { leftBottom, leftTop };     // 左边（竖）
    edges[1] = { rightBottom, rightTop };   // 右边（竖）
    edges[2] = { leftTop, rightTop };       // 上边（横）
    edges[3] = { leftBottom, rightBottom }; // 下边（横）

    return edges;
}

std::vector<VulkanCube::AdjacentInfo> VulkanCube::getAllAdjacentFaces(const std::array<FaceInfo, 6>& faceInfos,
    size_t faceID, Direction direction) const noexcept
{
    std::vector<AdjacentInfo> result;
    std::queue<AdjacentInfo> queues;
    switch (direction)
    {
    case Direction::Left:
        if (faceInfos[faceID].left != 6) queues.push(AdjacentInfo(faceID, faceInfos[faceID].left, Direction::Left));
        while (!queues.empty()) {
            AdjacentInfo idDir = queues.front();
            result.push_back(idDir);
            queues.pop();
            if (faceInfos[idDir.AdjacentId].left != 6 && faceInfos[idDir.AdjacentId].left != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].left, Direction::Left));
            if (faceInfos[idDir.AdjacentId].right != 6 && faceInfos[idDir.AdjacentId].right != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].right, Direction::Right));
            if (faceInfos[idDir.AdjacentId].top != 6 && faceInfos[idDir.AdjacentId].top != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].top, Direction::Top));
            if (faceInfos[idDir.AdjacentId].bottom != 6 && faceInfos[idDir.AdjacentId].bottom != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].bottom, Direction::Bottom));
        }
        break;
    case Direction::Right:
        if (faceInfos[faceID].right != 6) queues.push(AdjacentInfo(faceID, faceInfos[faceID].right, Direction::Right));
        while (!queues.empty()) {
            AdjacentInfo idDir = queues.front();
            result.push_back(idDir);
            queues.pop();
            if (faceInfos[idDir.AdjacentId].left != 6 && faceInfos[idDir.AdjacentId].left != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].left, Direction::Left));
            if (faceInfos[idDir.AdjacentId].right != 6 && faceInfos[idDir.AdjacentId].right != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].right, Direction::Right));
            if (faceInfos[idDir.AdjacentId].top != 6 && faceInfos[idDir.AdjacentId].top != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].top, Direction::Top));
            if (faceInfos[idDir.AdjacentId].bottom != 6 && faceInfos[idDir.AdjacentId].bottom != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].bottom, Direction::Bottom));
        }
        break;
    case Direction::Top:
        if (faceInfos[faceID].top != 6) queues.push(AdjacentInfo(faceID, faceInfos[faceID].top, Direction::Top));
        while (!queues.empty()) {
            AdjacentInfo idDir = queues.front();
            result.push_back(idDir);
            queues.pop();
            if (faceInfos[idDir.AdjacentId].left != 6 && faceInfos[idDir.AdjacentId].left != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].left, Direction::Left));
            if (faceInfos[idDir.AdjacentId].right != 6 && faceInfos[idDir.AdjacentId].right != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].right, Direction::Right));
            if (faceInfos[idDir.AdjacentId].top != 6 && faceInfos[idDir.AdjacentId].top != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].top, Direction::Top));
            if (faceInfos[idDir.AdjacentId].bottom != 6 && faceInfos[idDir.AdjacentId].bottom != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].bottom, Direction::Bottom));
        }
        break;
    case Direction::Bottom:
        if (faceInfos[faceID].bottom != 6) queues.push(AdjacentInfo(faceID, faceInfos[faceID].bottom, Direction::Bottom));
        while (!queues.empty()) {
            AdjacentInfo idDir = queues.front();
            result.push_back(idDir);
            queues.pop();
            if (faceInfos[idDir.AdjacentId].left != 6 && faceInfos[idDir.AdjacentId].left != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].left, Direction::Left));
            if (faceInfos[idDir.AdjacentId].right != 6 && faceInfos[idDir.AdjacentId].right != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].right, Direction::Right));
            if (faceInfos[idDir.AdjacentId].top != 6 && faceInfos[idDir.AdjacentId].top != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].top, Direction::Top));
            if (faceInfos[idDir.AdjacentId].bottom != 6 && faceInfos[idDir.AdjacentId].bottom != idDir.faceId)
                queues.push(AdjacentInfo(idDir.AdjacentId, faceInfos[idDir.AdjacentId].bottom, Direction::Bottom));
        }
        break;
    default:
        break;
    }
    return result;
}

std::array<uint32_t, 6> VulkanCube::getDirectionFaceIds() const noexcept
{
    std::array<uint32_t, 6> result = { 6 };
    result[static_cast<size_t>(Direction::Front)] = 0;
    for (uint32_t i = 1; i < 6; ++i) {
        if (std::abs(vertices[i * 4].z + 2.f) <= s_fDet && std::abs(vertices[i * 4 + 1].z + 2.f) <= s_fDet 
            && std::abs(vertices[i * 4 + 2].z + 2.f) <= s_fDet && std::abs(vertices[i * 4 + 3].z + 2.f) <= s_fDet) {
            result[static_cast<size_t>(Direction::Back)] = i;
            continue;
        }
        if (std::abs(vertices[i * 4].x) <= s_fDet && std::abs(vertices[i * 4 + 1].x) <= s_fDet
            && std::abs(vertices[i * 4 + 2].x) <= s_fDet && std::abs(vertices[i * 4 + 3].x) <= s_fDet) {
            result[static_cast<size_t>(Direction::Left)] = i;
            continue;
        }
        if (std::abs(vertices[i * 4].x - 2.f) <= s_fDet && std::abs(vertices[i * 4 + 1].x - 2.f) <= s_fDet
            && std::abs(vertices[i * 4 + 2].x - 2.f) <= s_fDet && std::abs(vertices[i * 4 + 3].x - 2.f) <= s_fDet) {
            result[static_cast<size_t>(Direction::Right)] = i;
            continue;
        }
        if (std::abs(vertices[i * 4].y - 1.f) <= s_fDet && std::abs(vertices[i * 4 + 1].y - 1.f) <= s_fDet
            && std::abs(vertices[i * 4 + 2].y - 1.f) <= s_fDet && std::abs(vertices[i * 4 + 3].y - 1.f) <= s_fDet) {
            result[static_cast<size_t>(Direction::Top)] = i;
            continue;
        }
        if (std::abs(vertices[i * 4].y + 1.f) <= s_fDet && std::abs(vertices[i * 4 + 1].y + 1.f) <= s_fDet
            && std::abs(vertices[i * 4 + 2].y + 1.f) <= s_fDet && std::abs(vertices[i * 4 + 3].y + 1.f) <= s_fDet) {
            result[static_cast<size_t>(Direction::Bottom)] = i;
            continue;
        }
    }
    return result;
}

bool VulkanCube::getFaceID(double x, double y, size_t& id) const noexcept
{
    float halfWidth = static_cast<float>(swapChainExtent.width) / 2.f;
    float halfHeight = static_cast<float>(swapChainExtent.height) / 2.f;
    float aspect = halfWidth / halfHeight;
    float wx = (static_cast<float>(x) - halfWidth) / halfWidth * aspect;
    float wy = (halfHeight - static_cast<float>(y)) / halfHeight;
    std::array<glm::vec3, 4> faceVerts;
    for (size_t i = 0; i < 6; ++i) {
        faceVerts[0] = vertices[i * 4] * scale;
        faceVerts[1] = vertices[i * 4 + 1] * scale;
        faceVerts[2] = vertices[i * 4 + 2] * scale;
        faceVerts[3] = vertices[i * 4 + 3] * scale;
        std::sort(faceVerts.begin(), faceVerts.end(), [](const glm::vec3& _1, const glm::vec3& _2) {return _1.x < _2.x; });
        if (wx >= faceVerts[0].x && wx < faceVerts[3].x &&
            ((wy >= faceVerts[0].y && wy < faceVerts[1].y) || (wy >= faceVerts[1].y && wy < faceVerts[0].y)))
        {
            id = i;
            spdlog::debug("Click position in face: {}", id);
            return true;
        }
    }
    spdlog::debug("Click position not in any square!");
    return false;
}

bool VulkanCube::addRotateAnimation()
{
    auto f0Edges = getEdges(selectedFace[0]);
    auto f1Edges = getEdges(selectedFace[1]);
    const auto faceInfos = getAdjacentInfo();
    if (fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].x, vertices[f1Edges[static_cast<size_t>(Direction::Right)][0]].x) &&
        !fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].y, vertices[f1Edges[static_cast<size_t>(Direction::Right)][0]].y)) {
        if (connectAlongAxis(faceInfos, selectedFace[0], Direction::Left)) {
            Animation animation;
            animation.interactive = true;
            animation.rotateAxis = Animation::zAxis;

            animation.faceIds = getFaceInHalfPlane(vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].x, true, true);

            size_t rTimes = 0;
            if (vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].y > vertices[f1Edges[static_cast<size_t>(Direction::Right)][0]].y) {
                animation.clockWise = true;
                rTimes = static_cast<size_t>(vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].y -
                    vertices[f1Edges[static_cast<size_t>(Direction::Right)][0]].y + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]]
                        + glm::vec3(0.f, -2.f * static_cast<float>(i), 0.f);
                    animationQueues.push(animation);
                }
            }
            else {
                animation.clockWise = false;
                rTimes = static_cast<size_t>(vertices[f1Edges[static_cast<size_t>(Direction::Right)][0]].y -
                    vertices[f0Edges[static_cast<size_t>(Direction::Left)][0]].y + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Left)][1]]
                        + glm::vec3(0.f, 2.f * static_cast<float>(i), 0.f);
                    animationQueues.push(animation);
                }
            }
            return true;
        }
    }
    if (fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].x, vertices[f1Edges[static_cast<size_t>(Direction::Left)][0]].x) &&
        !fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].y, vertices[f1Edges[static_cast<size_t>(Direction::Left)][0]].y)) {
        if (connectAlongAxis(faceInfos, selectedFace[0], Direction::Right)){
            Animation animation;
            animation.interactive = true;
            animation.rotateAxis = Animation::zAxis;

            animation.faceIds = getFaceInHalfPlane(vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].x, false, true);

            size_t rTimes = 0;
            if (vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].y > vertices[f1Edges[static_cast<size_t>(Direction::Left)][0]].y) {
                animation.clockWise = false;
                rTimes = static_cast<size_t>(vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].y -
                    vertices[f1Edges[static_cast<size_t>(Direction::Left)][0]].y + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]]
                        + glm::vec3(0.f, -2.f * static_cast<float>(i), 0.f);
                    animationQueues.push(animation);
                }
            }
            else {
                animation.clockWise = true;
                rTimes = static_cast<size_t>(vertices[f1Edges[static_cast<size_t>(Direction::Left)][0]].y -
                    vertices[f0Edges[static_cast<size_t>(Direction::Right)][0]].y + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Right)][1]]
                        + glm::vec3(0.f, 2.f * static_cast<float>(i), 0.f);
                    animationQueues.push(animation);
                }
            }
            return true;
        }
    }
    if (fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].y, vertices[f1Edges[static_cast<size_t>(Direction::Bottom)][0]].y) &&
        !fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].x, vertices[f1Edges[static_cast<size_t>(Direction::Bottom)][0]].x)) {
        if (connectAlongAxis(faceInfos, selectedFace[0], Direction::Top)) {
            Animation animation;
            animation.interactive = true;
            animation.rotateAxis = Animation::zAxis;

            animation.faceIds = getFaceInHalfPlane(vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].y, false, false);

            size_t rTimes = 0;
            if (vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].x > vertices[f1Edges[static_cast<size_t>(Direction::Bottom)][0]].x) {
                animation.clockWise = true;
                rTimes = static_cast<size_t>(vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].x -
                    vertices[f1Edges[static_cast<size_t>(Direction::Bottom)][0]].x + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]]
                        + glm::vec3(-2.f * static_cast<float>(i), 0.f, 0.f);
                    animationQueues.push(animation);
                }
            }
            else {
                animation.clockWise = false;
                rTimes = static_cast<size_t>(vertices[f1Edges[static_cast<size_t>(Direction::Bottom)][0]].x -
                    vertices[f0Edges[static_cast<size_t>(Direction::Top)][0]].x + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Top)][1]]
                        + glm::vec3(2.f * static_cast<float>(i), 0.f, 0.f);
                    animationQueues.push(animation);
                }
            }
            return true;
        }
    }
    if (fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].y, vertices[f1Edges[static_cast<size_t>(Direction::Top)][0]].y) &&
        !fEqual(vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].x, vertices[f1Edges[static_cast<size_t>(Direction::Top)][0]].x)) {
        if (connectAlongAxis(faceInfos, selectedFace[0], Direction::Bottom)) {
            Animation animation;
            animation.interactive = true;
            animation.rotateAxis = Animation::zAxis;

            animation.faceIds = getFaceInHalfPlane(vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].y, true, false);

            size_t rTimes = 0;
            if (vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].x > vertices[f1Edges[static_cast<size_t>(Direction::Top)][0]].x) {
                animation.clockWise = false;
                rTimes = static_cast<size_t>(vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].x -
                    vertices[f1Edges[static_cast<size_t>(Direction::Top)][0]].x + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]]
                        + glm::vec3(-2.f * static_cast<float>(i), 0.f, 0.f);
                    animationQueues.push(animation);
                }
            }
            else {
                animation.clockWise = true;
                rTimes = static_cast<size_t>(vertices[f1Edges[static_cast<size_t>(Direction::Top)][0]].x -
                    vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][0]].x + s_fDet) / 2;
                for (size_t i = 0; i < rTimes; ++i) {
                    animation.rotateCenter = vertices[f0Edges[static_cast<size_t>(Direction::Bottom)][1]]
                        + glm::vec3(2.f * static_cast<float>(i), 0.f, 0.f);
                    animationQueues.push(animation);
                }
            }
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> VulkanCube::getFaceInHalfPlane(float v, bool greater, bool isX) const noexcept
{
    std::vector<uint32_t> result;
    if (isX) {
        for (size_t i = 0; i < 6ull; ++i) {
            bool isOk = true;
            for (size_t j = 0; j < 4ull; ++j) {
                if (greater) {
                    if (vertices[i * 4ull + j].x < v - s_fDet) {
                        isOk = false;
                        break;
                    }
                }
                else {
                    if (vertices[i * 4ull + j].x > v + s_fDet) {
                        isOk = false;
                        break;
                    }
                }
            }
            if (isOk)
                result.push_back(static_cast<uint32_t>(i));
        }
    }
    else {
        for (size_t i = 0; i < 6ull; ++i) {
            bool isOk = true;
            for (size_t j = 0; j < 4ull; ++j) {
                if (greater) {
                    if (vertices[i * 4ull + j].y < v - s_fDet) {
                        isOk = false;
                        break;
                    }
                }
                else {
                    if (vertices[i * 4ull + j].y > v + s_fDet) {
                        isOk = false;
                        break;
                    }
                }
            }
            if (isOk)
                result.push_back(static_cast<uint32_t>(i));
        }
    }
    return result;
}

bool VulkanCube::connectAlongAxis(const std::array<FaceInfo, 6>& faceInfos, size_t firstId, Direction direction) const noexcept
{
    switch (direction) {
    case Direction::Left:
        return faceInfos[firstId].left != 6;
        break;
    case Direction::Right:
        return faceInfos[firstId].right != 6;
        break;
    case Direction::Top:
        return faceInfos[firstId].top != 6;
        break;
    case Direction::Bottom:
        return faceInfos[firstId].bottom != 6;
        break;
    default:
        return false;
        break;
    }
}

void VulkanCube::initWindow()
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanCube", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
}

void VulkanCube::framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    auto app = reinterpret_cast<VulkanCube*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void VulkanCube::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto app = reinterpret_cast<VulkanCube*>(glfwGetWindowUserPointer(window));
    if (action == GLFW_RELEASE) {
        switch (key) {
        case GLFW_KEY_SPACE:
            if (app->readyToAddAnimation()) {
                app->addCubeAnimation();
            }
            break;
        case GLFW_KEY_R:
            if (app->is2D && app->animationQueues.empty())
                app->resetFaceToCenter(TranslateType::Reset);
            break;
        case GLFW_KEY_S:
            app->isPaused = !app->isPaused;
            if (!app->animationQueues.empty()) {
                if (app->isPaused) {
                    app->currentTime = std::chrono::high_resolution_clock::now();
                    app->periodTimeMS += std::chrono::duration_cast<std::chrono::milliseconds>(app->currentTime - app->startTime).count();
                }
                else {
                    app->startTime = std::chrono::high_resolution_clock::now();
                }
            }
            break;
        default:
            break;
        }
    }
}

void VulkanCube::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto app = reinterpret_cast<VulkanCube*>(glfwGetWindowUserPointer(window));
    if (app->is2D && app->animationQueues.empty()) {
        if (button == GLFW_MOUSE_BUTTON_1) {
            if (action == GLFW_PRESS) {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                size_t faceId = 6;
                if (app->getFaceID(x, y, faceId)) {
                    if (app->clickTime == 0 || (app->clickTime == 1 && faceId != app->selectedFace[0])) {
                        app->selectedFace[app->clickTime] = faceId;
                        ++app->clickTime;
                        app->clickTime = app->clickTime % 2ull;
                        if (app->clickTime == 0) {
                            if (app->addRotateAnimation())
                            {
                                spdlog::debug("Succeed to add animations!");
                            }
                            else {
                                spdlog::error("------------------ Wrong input! ------------------");
                            }
                        }
                    }
                }
            }
        }
    }
}

void VulkanCube::cleanupSwapChain()
{
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    vkDestroyImageView(device, colorImageView, nullptr);
    vkDestroyImage(device, colorImage, nullptr);
    vkFreeMemory(device, colorImageMemory, nullptr);

    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void VulkanCube::cleanup()
{
    cleanupSwapChain();

    vkDestroyPipeline(device, trianglePipeline, nullptr);
    vkDestroyPipeline(device, linePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    /*
    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);

    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);
    */

    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    if (indexBufferMapperPtr != nullptr) {
        vkUnmapMemory(device, indexBufferMemory);
        indexBufferMapperPtr = nullptr;
    }
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);

    if (vertexBufferMappedPtr != nullptr) {
        vkUnmapMemory(device, vertexBufferMemory);
        vertexBufferMappedPtr = nullptr;
    }

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    vkDestroyBuffer(device, colorBuffer, nullptr);
    vkFreeMemory(device, colorBufferMemory, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);

    glfwTerminate();
}

void VulkanCube::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createColorResources();
    createDepthResources();
    createFramebuffers();
    initUBO();
}

void VulkanCube::createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

void VulkanCube::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void VulkanCube::setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void VulkanCube::createSurface()
{
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanCube::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice = device;
            msaaSamples = getMaxUsableSampleCount();
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void VulkanCube::createLogicalDevice()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, queueFamilyIndices.presentFamily.value(), 0, &presentQueue);
}

void VulkanCube::createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIds[] = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };

    if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIds;
    }
    else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void VulkanCube::createImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());

    for (uint32_t i = 0; i < swapChainImages.size(); i++) {
        swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void VulkanCube::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = msaaSamples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = msaaSamples;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = swapChainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void VulkanCube::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void VulkanCube::createGraphicsPipeline()
{
    auto& glslCompiler = ShaderCompiler::getInstance();
    auto vertShaderCode = glslCompiler.compileGLSL(std::filesystem::path("shaders/vert.glsl"), EShLangVertex);
    auto fragShaderCode = glslCompiler.compileGLSL(std::filesystem::path("shaders/frag.glsl"), EShLangFragment);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = getBindingDescription();
    auto attributeDescriptions = getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size());
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescription.data();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 5.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = msaaSamples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanCube::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array<VkImageView, 3> attachments = {
            colorImageView,
            depthImageView,
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VulkanCube::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics command pool!");
    }
}

void VulkanCube::createColorResources()
{
    VkFormat colorFormat = swapChainImageFormat;

    createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
    colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void VulkanCube::createDepthResources()
{
    VkFormat depthFormat = findDepthFormat();

    createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

VkFormat VulkanCube::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat VulkanCube::findDepthFormat()
{
    return findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

/*
void VulkanCube::createTextureImage()
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    if (!pixels) {
        throw std::runtime_error("failed to load texture image!");
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    stbi_image_free(pixels);

    createImage(texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
    copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    //transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevels);
}
*/

/*
void VulkanCube::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    endSingleTimeCommands(commandBuffer);
}
*/

VkSampleCountFlagBits VulkanCube::getMaxUsableSampleCount()
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}

/*
void VulkanCube::createTextureSampler()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}
*/

VkImageView VulkanCube::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

void VulkanCube::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = numSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

void VulkanCube::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
}

/*
void VulkanCube::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
}

void VulkanCube::loadModel()
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    std::string warn;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())) {
        throw std::runtime_error(err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            vertex.color = { 1.0f, 1.0f, 1.0f };

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
}
*/

void VulkanCube::createVertexBuffer()
{
    vertices = {
        glm::vec3(-4.f, -1.f, 0.f), // 0
        glm::vec3(-2.f, -1.f, 0.f), // 1
        glm::vec3(-2.f,  1.f, 0.f), // 2
        glm::vec3(-4.f,  1.f, 0.f), // 3
        glm::vec3(-2.f, -3.f, 0.f), // 4
        glm::vec3( 0.f, -3.f, 0.f), // 5
        glm::vec3( 0.f, -1.f, 0.f), // 6
        glm::vec3(-2.f, -1.f, 0.f), // 7
        glm::vec3(-2.f, -1.f, 0.f), // 8
        glm::vec3( 0.f, -1.f, 0.f), // 9
        glm::vec3( 0.f,  1.f, 0.f), // 10
        glm::vec3(-2.f,  1.f, 0.f), // 11
        glm::vec3( 0.f, -1.f, 0.f), // 12
        glm::vec3( 2.f, -1.f, 0.f), // 13
        glm::vec3( 2.f,  1.f, 0.f), // 14
        glm::vec3( 0.f,  1.f, 0.f), // 15
        glm::vec3( 0.f,  1.f, 0.f), // 16
        glm::vec3( 2.f,  1.f, 0.f), // 17
        glm::vec3( 2.f,  3.f, 0.f), // 18
        glm::vec3( 0.f,  3.f, 0.f), // 19
        glm::vec3( 2.f, -1.f, 0.f), // 20
        glm::vec3( 4.f, -1.f, 0.f), // 21
        glm::vec3( 4.f,  1.f, 0.f), // 22
        glm::vec3( 2.f,  1.f, 0.f), // 23
    };

    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);

    vkMapMemory(device, vertexBufferMemory, 0, vertexBufferSize, 0, &vertexBufferMappedPtr);
    memcpy(vertexBufferMappedPtr, vertices.data(), (size_t)vertexBufferSize);

    color = {
        glm::vec3(0.8f, 0.8f, 0.0f), // f
        glm::vec3(0.0f, 0.8f, 0.8f), // f
        glm::vec3(0.8f, 0.0f, 0.8f), // f
        glm::vec3(0.8f, 0.3f, 0.0f), // f
        glm::vec3(0.0f, 0.8f, 0.0f), // f
        glm::vec3(0.0f, 0.0f, 0.8f), // f
        glm::vec3(0.8f, 0.8f, 0.8f), // s
        glm::vec3(0.8f, 0.0f, 0.0f), // i
    };

    VkDeviceSize colorBufferSize = sizeof(color[0]) * color.size();
    createBuffer(colorBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, colorBuffer, colorBufferMemory);

    void* data;
    vkMapMemory(device, colorBufferMemory, 0, colorBufferSize, 0, &data);
    memcpy(data, color.data(), (size_t)colorBufferSize);
    vkUnmapMemory(device, colorBufferMemory);
}

void VulkanCube::createIndexBuffer()
{
    indices = {
         0,  1,  2,  0,  2,  3,
         4,  5,  6,  4,  6,  7,
         8,  9, 10,  8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
         0,  2
    };
    std::vector<uint16_t> uploadIndices;
    uploadIndices.resize(indexMaxCount, 0);
    for (size_t i = 0; i < indices.size(); ++i) {
        uploadIndices[i] = indices[i];
    }
    VkDeviceSize bufferSize = sizeof(uploadIndices[0]) * indexMaxCount;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);

    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &indexBufferMapperPtr);
    memcpy(indexBufferMapperPtr, uploadIndices.data(), (size_t)bufferSize);
}

void VulkanCube::createUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);

        vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void VulkanCube::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void VulkanCube::createDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);
        /*
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureImageView;
        imageInfo.sampler = textureSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;
        */
        VkWriteDescriptorSet descriptorWrite{};

        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1u, &descriptorWrite, 0, nullptr);
        //vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void VulkanCube::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkCommandBuffer VulkanCube::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanCube::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanCube::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

uint32_t VulkanCube::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanCube::createCommandBuffers()
{
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanCube::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    std::vector<VkBuffer> vertexBuffers = { vertexBuffer, colorBuffer };
    std::vector<VkDeviceSize> offsets = { 0, 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(), offsets.data());

    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

    for (uint32_t i = 0; i < 6; ++i) {
        vkCmdDrawIndexed(commandBuffer, 6, 1, 6 * i, 0, i);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
    vkCmdDrawIndexed(commandBuffer, 2, 1, 36, 0, 6);

    if (clickTime == 1) {
        std::array<uint16_t, 8> appendIds{};
        uint16_t offset = selectedFace[0] * 4;
        for (uint16_t i = 0; i < 4; ++i) {
            if (i != 3) {
                appendIds[i * 2] = offset + i;
                appendIds[i * 2 + 1] = offset + i + 1;
            }
            else {
                appendIds[i * 2] = offset + i;
                appendIds[i * 2 + 1] = offset;
            }
        }
        memcpy(reinterpret_cast<uint16_t*>(indexBufferMapperPtr) + 38, appendIds.data(), sizeof(uint16_t) * appendIds.size());

        vkCmdDrawIndexed(commandBuffer, 8, 1, 38, 0, 7);
    }
    if (interactive) {
        std::array<uint16_t, 16> appendIds{};
        uint16_t offset = selectedFace[0] * 4;
        for (uint16_t i = 0; i < 4; ++i) {
            if (i != 3) {
                appendIds[i * 2] = offset + i;
                appendIds[i * 2 + 1] = offset + i + 1;
            }
            else {
                appendIds[i * 2] = offset + i;
                appendIds[i * 2 + 1] = offset;
            }
        }
        offset = selectedFace[1] * 4;
        for (uint16_t i = 0; i < 4; ++i) {
            if (i != 3) {
                appendIds[i * 2 + 8] = offset + i;
                appendIds[i * 2 + 1 + 8] = offset + i + 1;
            }
            else {
                appendIds[i * 2 + 8] = offset + i;
                appendIds[i * 2 + 1 + 8] = offset;
            }
        }
        memcpy(reinterpret_cast<uint16_t*>(indexBufferMapperPtr) + 38, appendIds.data(), sizeof(uint16_t) * appendIds.size());

        vkCmdDrawIndexed(commandBuffer, 16, 1, 38, 0, 7);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VulkanCube::createSyncObjects()
{
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VulkanCube::updateUniformBuffer(uint32_t currentImage)
{
    if (rotating)
    {
        currentTime = std::chrono::high_resolution_clock::now();
        //float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - rotateStartTime).count();
        float time = -4.5f;
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(10.0f), glm::vec3(-1.0f, 1.0f, 0.0f));
        memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
    }
}

void VulkanCube::drawFrame()
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateUniformBuffer(currentFrame);

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], /*VkCommandBufferResetFlagBits*/ 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkShaderModule VulkanCube::createShaderModule(const std::vector<uint32_t>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

VkSurfaceFormatKHR VulkanCube::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VulkanCube::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanCube::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

SwapChainSupportDetails VulkanCube::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

bool VulkanCube::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return queueFamilyIndices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool VulkanCube::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices VulkanCube::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices queueFamilyIndices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    uint32_t i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport) {
            queueFamilyIndices.presentFamily = i;
        }

        if (queueFamilyIndices.isComplete()) {
            break;
        }

        ++i;
    }

    return queueFamilyIndices;
}

std::vector<const char*> VulkanCube::getRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanCube::checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanCube::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        spdlog::error("validation layer: {}", pCallbackData->pMessage);
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        spdlog::warn("validation layer: {}", pCallbackData->pMessage);
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        spdlog::debug("validation layer: {}", pCallbackData->pMessage);
    else
        spdlog::trace("validation layer: {}", pCallbackData->pMessage);
    return VK_FALSE;
}
