#include "QtShim.h"
import std;

namespace {
struct SwapchainSupport {
  VkSurfaceCapabilitiesKHR capabilities{};
  QVector<VkSurfaceFormatKHR> formats;
  QVector<VkPresentModeKHR> presentModes;
};

QByteArray loadShaderBytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QString terminalVertexShaderPath() {
  return QStringLiteral("shaders/terminal_quad.vert.spv");
}

QString terminalFragmentShaderPath() {
  return QStringLiteral("shaders/terminal_quad.frag.spv");
}
}

VulkanRenderer::VulkanRenderer() = default;

VulkanRenderer::~VulkanRenderer() {
  cleanup();
}

bool VulkanRenderer::initialize(QVulkanInstance *instance, QWindow *window,
                                const TerminalConfig::TerminalProfile &profile,
                                const QFont &font) {
  if (!instance || !window) {
    return false;
  }

  m_instance = instance;
  m_window = window;
  m_profile = profile;

  m_surface = m_instance->surfaceForWindow(m_window);
  if (m_surface == VK_NULL_HANDLE) {
    return false;
  }

  buildGlyphAtlas(font);

  if (!createDevice()) {
    return false;
  }
  if (!createSwapchain()) {
    return false;
  }
  if (!createRenderPass()) {
    return false;
  }
  if (!createPipeline()) {
    return false;
  }
  if (!createFramebuffers()) {
    return false;
  }
  if (!createCommandPool()) {
    return false;
  }
  if (!createBuffers()) {
    return false;
  }
  if (!createAtlas()) {
    return false;
  }
  if (!createDescriptorSet()) {
    return false;
  }
  if (!createCommandBuffers()) {
    return false;
  }
  if (!createSyncObjects()) {
    return false;
  }

  m_ready = true;
  return true;
}

void VulkanRenderer::cleanup() {
  if (!m_device) {
    return;
  }

  vkDeviceWaitIdle(m_device);

  cleanupSwapchain();

  if (m_atlasSampler) {
    vkDestroySampler(m_device, m_atlasSampler, nullptr);
  }
  if (m_atlasView) {
    vkDestroyImageView(m_device, m_atlasView, nullptr);
  }
  if (m_atlasImage) {
    vkDestroyImage(m_device, m_atlasImage, nullptr);
  }
  if (m_atlasMemory) {
    vkFreeMemory(m_device, m_atlasMemory, nullptr);
  }

  if (m_descriptorPool) {
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
  }
  if (m_descriptorSetLayout) {
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
  }

  if (m_instanceBuffer) {
    vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
  }
  if (m_instanceMemory) {
    vkFreeMemory(m_device, m_instanceMemory, nullptr);
  }
  if (m_vertexBuffer) {
    vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
  }
  if (m_vertexMemory) {
    vkFreeMemory(m_device, m_vertexMemory, nullptr);
  }

  if (m_commandPool) {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  }

  if (m_renderFinished) {
    vkDestroySemaphore(m_device, m_renderFinished, nullptr);
  }
  if (m_imageAvailable) {
    vkDestroySemaphore(m_device, m_imageAvailable, nullptr);
  }
  if (m_inFlight) {
    vkDestroyFence(m_device, m_inFlight, nullptr);
  }

  vkDestroyDevice(m_device, nullptr);
  m_device = VK_NULL_HANDLE;
  m_ready = false;
}

bool VulkanRenderer::isReady() const {
  return m_ready;
}

void VulkanRenderer::resize(int width, int height) {
  if (!m_ready || width <= 0 || height <= 0) {
    return;
  }
  if (m_surfaceSize.width() == width && m_surfaceSize.height() == height) {
    return;
  }
  m_surfaceSize = QSize(width, height);

  vkDeviceWaitIdle(m_device);
  cleanupSwapchain();
  createSwapchain();
  createRenderPass();
  createPipeline();
  createFramebuffers();
  createCommandBuffers();
}

void VulkanRenderer::updateFromBuffer(const TerminalBuffer *buffer,
                                      int scrollOffset,
                                      const Selection &selection) {
  if (!buffer) {
    return;
  }

  const QStringList lines = buffer->snapshot(scrollOffset);
  m_instances.clear();
  m_instances.reserve(lines.size() * buffer->columns());

  Selection normalized = selection;
  if (normalized.active) {
    if (normalized.startRow > normalized.endRow ||
        (normalized.startRow == normalized.endRow &&
         normalized.startCol > normalized.endCol)) {
      std::swap(normalized.startRow, normalized.endRow);
      std::swap(normalized.startCol, normalized.endCol);
    }
  }

  const bool cursorVisible = (scrollOffset == 0);
  const int cursorRow = buffer->cursorRow();
  const int cursorCol = buffer->cursorColumn();

  for (int row = 0; row < lines.size(); ++row) {
    for (int col = 0; col < buffer->columns(); ++col) {
      const TerminalBuffer::Cell cell =
          buffer->cellAtVisible(row, col, scrollOffset);
      QChar ch = cell.ch;
      uint codepoint = ch.unicode();

      // Use bold glyph variant when cell is bold
      uint glyphKey = codepoint;
      if (cell.bold) {
        glyphKey = codepoint | 0x80000000u;
        if (!m_glyphs.contains(glyphKey)) {
          if (!rasterizeGlyph(codepoint, true)) {
            glyphKey = codepoint;
          }
        }
      }
      if (!m_glyphs.contains(glyphKey)) {
        if (!rasterizeGlyph(codepoint, false)) {
          glyphKey = QLatin1Char(' ').unicode();
        }
      }
      const GlyphInfo glyph = m_glyphs.value(glyphKey);

      TerminalQuadInstance instance{};
      instance.posX = static_cast<float>(col * m_cellSize.width());
      instance.posY = static_cast<float>(row * m_cellSize.height());
      instance.sizeX = static_cast<float>(m_cellSize.width());
      instance.sizeY = static_cast<float>(m_cellSize.height());
      instance.uvMinX = glyph.uvMinX;
      instance.uvMinY = glyph.uvMinY;
      instance.uvMaxX = glyph.uvMaxX;
      instance.uvMaxY = glyph.uvMaxY;
      QColor fg = cell.fg;
      QColor bg = cell.bg;

      if (normalized.active) {
        bool inRange = false;
        if (row > normalized.startRow && row < normalized.endRow) {
          inRange = true;
        } else if (row == normalized.startRow && row == normalized.endRow) {
          inRange = (col >= normalized.startCol && col < normalized.endCol);
        } else if (row == normalized.startRow) {
          inRange = (col >= normalized.startCol);
        } else if (row == normalized.endRow) {
          inRange = (col < normalized.endCol);
        }
        if (inRange) {
          bg = m_profile.selection;
        }
      }

      if (cursorVisible && row == cursorRow && col == cursorCol) {
        bg = m_profile.cursor;
        fg = m_profile.background;
      }

      instance.fgR = fg.redF();
      instance.fgG = fg.greenF();
      instance.fgB = fg.blueF();
      instance.fgA = 1.0f;
      instance.bgR = bg.redF();
      instance.bgG = bg.greenF();
      instance.bgB = bg.blueF();
      instance.bgA = 1.0f;

      m_instances.push_back(instance);

      // Underline decoration: thin quad at bottom of cell
      if (cell.underline) {
        const GlyphInfo spaceGlyph = m_glyphs.value(QLatin1Char(' ').unicode());
        TerminalQuadInstance underline{};
        underline.posX = instance.posX;
        underline.posY = instance.posY + instance.sizeY - 2.0f;
        underline.sizeX = instance.sizeX;
        underline.sizeY = 1.0f;
        underline.uvMinX = spaceGlyph.uvMinX;
        underline.uvMinY = spaceGlyph.uvMinY;
        underline.uvMaxX = spaceGlyph.uvMaxX;
        underline.uvMaxY = spaceGlyph.uvMaxY;
        underline.fgR = fg.redF();
        underline.fgG = fg.greenF();
        underline.fgB = fg.blueF();
        underline.fgA = 1.0f;
        underline.bgR = fg.redF();
        underline.bgG = fg.greenF();
        underline.bgB = fg.blueF();
        underline.bgA = 1.0f;
        m_instances.push_back(underline);
      }

      // Strikethrough decoration: thin quad at middle of cell
      if (cell.strikethrough) {
        const GlyphInfo spaceGlyph = m_glyphs.value(QLatin1Char(' ').unicode());
        TerminalQuadInstance strike{};
        strike.posX = instance.posX;
        strike.posY = instance.posY + instance.sizeY * 0.5f;
        strike.sizeX = instance.sizeX;
        strike.sizeY = 1.0f;
        strike.uvMinX = spaceGlyph.uvMinX;
        strike.uvMinY = spaceGlyph.uvMinY;
        strike.uvMaxX = spaceGlyph.uvMaxX;
        strike.uvMaxY = spaceGlyph.uvMaxY;
        strike.fgR = fg.redF();
        strike.fgG = fg.greenF();
        strike.fgB = fg.blueF();
        strike.fgA = 1.0f;
        strike.bgR = fg.redF();
        strike.bgG = fg.greenF();
        strike.bgB = fg.blueF();
        strike.bgA = 1.0f;
        m_instances.push_back(strike);
      }
    }
  }

  updateInstanceBuffer();
}

void VulkanRenderer::render() {
  if (!m_ready || m_swapchain == VK_NULL_HANDLE) {
    return;
  }

  reuploadAtlas();

  vkWaitForFences(m_device, 1, &m_inFlight, VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_inFlight);

  uint32_t imageIndex = 0;
  VkResult acquireResult =
      vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                            m_imageAvailable, VK_NULL_HANDLE, &imageIndex);
  if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
    resize(m_window->width(), m_window->height());
    return;
  }
  if (acquireResult != VK_SUCCESS) {
    return;
  }

  recordCommandBuffer(imageIndex);

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &m_imageAvailable;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &m_renderFinished;

  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlight);

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &m_renderFinished;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &m_swapchain;
  presentInfo.pImageIndices = &imageIndex;

  VkResult presentResult = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
      presentResult == VK_SUBOPTIMAL_KHR) {
    resize(m_window->width(), m_window->height());
  }
}

bool VulkanRenderer::createDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance->vkInstance(), &deviceCount, nullptr);
  if (deviceCount == 0) {
    return false;
  }

  QVector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance->vkInstance(), &deviceCount,
                             devices.data());

  for (VkPhysicalDevice device : devices) {
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    QVector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount,
                                             queues.data());

    for (uint32_t i = 0; i < queueCount; ++i) {
      VkBool32 presentSupported = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface,
                                           &presentSupported);
      if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
        m_physicalDevice = device;
        m_graphicsQueueFamily = i;
        break;
      }
    }
    if (m_physicalDevice != VK_NULL_HANDLE) {
      break;
    }
  }

  if (m_physicalDevice == VK_NULL_HANDLE) {
    return false;
  }

  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;

  const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo deviceInfo{};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = 1;
  deviceInfo.ppEnabledExtensionNames = extensions;

  if (vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device) !=
      VK_SUCCESS) {
    return false;
  }

  vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
  return true;
}

bool VulkanRenderer::createSwapchain() {
  SwapchainSupport support{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface,
                                            &support.capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface,
                                       &formatCount, nullptr);
  support.formats.resize(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface,
                                       &formatCount, support.formats.data());

  uint32_t presentCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface,
                                            &presentCount, nullptr);
  support.presentModes.resize(presentCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface,
                                            &presentCount,
                                            support.presentModes.data());

  VkSurfaceFormatKHR surfaceFormat = support.formats.first();
  for (const VkSurfaceFormatKHR &format : support.formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      surfaceFormat = format;
      break;
    }
  }

  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (const VkPresentModeKHR &mode : support.presentModes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      presentMode = mode;
      break;
    }
  }

  VkExtent2D extent = support.capabilities.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent.width = std::max(1, m_window->width());
    extent.height = std::max(1, m_window->height());
  }

  uint32_t imageCount = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0 &&
      imageCount > support.capabilities.maxImageCount) {
    imageCount = support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = support.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) !=
      VK_SUCCESS) {
    return false;
  }

  m_swapchainFormat = surfaceFormat.format;
  m_swapchainExtent = extent;

  uint32_t swapchainImageCount = 0;
  vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr);
  m_swapchainImages.resize(swapchainImageCount);
  vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount,
                          m_swapchainImages.data());

  m_swapchainImageViews.resize(swapchainImageCount);
  for (int i = 0; i < m_swapchainImages.size(); ++i) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_swapchainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_swapchainFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr,
                          &m_swapchainImageViews[i]) != VK_SUCCESS) {
      return false;
    }
  }

  return true;
}

void VulkanRenderer::cleanupSwapchain() {
  for (VkFramebuffer framebuffer : m_framebuffers) {
    vkDestroyFramebuffer(m_device, framebuffer, nullptr);
  }
  m_framebuffers.clear();

  if (m_pipeline) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
  }
  if (m_pipelineLayout) {
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
  }
  if (m_renderPass) {
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
  }

  for (VkImageView view : m_swapchainImageViews) {
    vkDestroyImageView(m_device, view, nullptr);
  }
  m_swapchainImageViews.clear();

  if (m_swapchain) {
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
  }
}

bool VulkanRenderer::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = m_swapchainFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  return vkCreateRenderPass(m_device, &renderPassInfo, nullptr,
                            &m_renderPass) == VK_SUCCESS;
}

bool VulkanRenderer::createPipeline() {
  QByteArray vertCode = loadShaderBytes(terminalVertexShaderPath());
  QByteArray fragCode = loadShaderBytes(terminalFragmentShaderPath());
  if (vertCode.isEmpty() || fragCode.isEmpty()) {
    return false;
  }

  VkShaderModule vertModule = createShaderModule(vertCode);
  VkShaderModule fragModule = createShaderModule(fragCode);
  if (!vertModule || !fragModule) {
    return false;
  }

  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragModule;
  fragStage.pName = "main";

  VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

  VkVertexInputBindingDescription bindings[2] = {};
  bindings[0].binding = 0;
  bindings[0].stride = sizeof(TerminalQuadVertex);
  bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  bindings[1].binding = 1;
  bindings[1].stride = sizeof(TerminalQuadInstance);
  bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

  VkVertexInputAttributeDescription attributes[6] = {};
  attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
  attributes[1] = {1, 1, VK_FORMAT_R32G32_SFLOAT,
                   static_cast<uint32_t>(offsetof(TerminalQuadInstance, posX))};
  attributes[2] = {2, 1, VK_FORMAT_R32G32_SFLOAT,
                   static_cast<uint32_t>(offsetof(TerminalQuadInstance, sizeX))};
  attributes[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                   static_cast<uint32_t>(offsetof(TerminalQuadInstance, uvMinX))};
  attributes[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                   static_cast<uint32_t>(offsetof(TerminalQuadInstance, fgR))};
  attributes[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                   static_cast<uint32_t>(offsetof(TerminalQuadInstance, bgR))};

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 2;
  vertexInput.pVertexBindingDescriptions = bindings;
  vertexInput.vertexAttributeDescriptionCount = 6;
  vertexInput.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(m_swapchainExtent.width);
  viewport.height = static_cast<float>(m_swapchainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = m_swapchainExtent;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                   VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT;
  blendAttachment.blendEnable = VK_TRUE;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo blendState{};
  blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blendState.attachmentCount = 1;
  blendState.pAttachments = &blendAttachment;

  if (m_descriptorSetLayout == VK_NULL_HANDLE) {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
      return false;
    }
  }

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(float) * 2;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;

  if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr,
                             &m_pipelineLayout) != VK_SUCCESS) {
    return false;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pColorBlendState = &blendState;
  pipelineInfo.layout = m_pipelineLayout;
  pipelineInfo.renderPass = m_renderPass;
  pipelineInfo.subpass = 0;

  bool ok = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                      &pipelineInfo, nullptr, &m_pipeline) ==
            VK_SUCCESS;

  vkDestroyShaderModule(m_device, vertModule, nullptr);
  vkDestroyShaderModule(m_device, fragModule, nullptr);

  return ok;
}

bool VulkanRenderer::createFramebuffers() {
  m_framebuffers.resize(m_swapchainImageViews.size());
  for (int i = 0; i < m_swapchainImageViews.size(); ++i) {
    VkImageView attachments[] = {m_swapchainImageViews[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = m_swapchainExtent.width;
    framebufferInfo.height = m_swapchainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
                            &m_framebuffers[i]) != VK_SUCCESS) {
      return false;
    }
  }
  return true;
}

bool VulkanRenderer::createCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

  return vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) ==
         VK_SUCCESS;
}

bool VulkanRenderer::createCommandBuffers() {
  if (!m_commandPool) {
    return false;
  }

  m_commandBuffers.resize(m_swapchainImages.size());

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

  return vkAllocateCommandBuffers(m_device, &allocInfo,
                                  m_commandBuffers.data()) == VK_SUCCESS;
}

bool VulkanRenderer::createBuffers() {
  VkBufferCreateInfo vertexInfo{};
  vertexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertexInfo.size = sizeof(kTerminalQuadVertices);
  vertexInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  vertexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &vertexInfo, nullptr, &m_vertexBuffer) !=
      VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &requirements);
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexMemory) !=
      VK_SUCCESS) {
    return false;
  }

  vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexMemory, 0);
  void *mapped = nullptr;
  vkMapMemory(m_device, m_vertexMemory, 0, vertexInfo.size, 0, &mapped);
  memcpy(mapped, kTerminalQuadVertices, vertexInfo.size);
  vkUnmapMemory(m_device, m_vertexMemory);

  VkBufferCreateInfo instanceInfo{};
  instanceInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  instanceInfo.size = sizeof(TerminalQuadInstance) * 4;
  instanceInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  instanceInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &instanceInfo, nullptr, &m_instanceBuffer) !=
      VK_SUCCESS) {
    return false;
  }

  vkGetBufferMemoryRequirements(m_device, m_instanceBuffer, &requirements);
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_instanceMemory) !=
      VK_SUCCESS) {
    return false;
  }

  vkBindBufferMemory(m_device, m_instanceBuffer, m_instanceMemory, 0);
  m_instanceCapacity = instanceInfo.size / sizeof(TerminalQuadInstance);

  return true;
}

bool VulkanRenderer::createAtlas() {
  if (m_atlasImageCpu.isNull()) {
    return false;
  }

  const uint32_t width = static_cast<uint32_t>(m_atlasImageCpu.width());
  const uint32_t height = static_cast<uint32_t>(m_atlasImageCpu.height());
  const uint32_t rowLength =
      static_cast<uint32_t>(m_atlasImageCpu.bytesPerLine());
  const VkDeviceSize imageSize =
      static_cast<VkDeviceSize>(m_atlasImageCpu.sizeInBytes());

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

  VkBufferCreateInfo stagingInfo{};
  stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingInfo.size = imageSize;
  stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &stagingInfo, nullptr, &stagingBuffer) !=
      VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements stagingRequirements{};
  vkGetBufferMemoryRequirements(m_device, stagingBuffer,
                                &stagingRequirements);
  VkMemoryAllocateInfo stagingAlloc{};
  stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  stagingAlloc.allocationSize = stagingRequirements.size;
  stagingAlloc.memoryTypeIndex =
      findMemoryType(stagingRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) !=
      VK_SUCCESS) {
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    return false;
  }

  vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

  void *mapped = nullptr;
  vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
  memcpy(mapped, m_atlasImageCpu.constBits(),
         static_cast<size_t>(imageSize));
  vkUnmapMemory(m_device, stagingMemory);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device, &imageInfo, nullptr, &m_atlasImage) !=
      VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(m_device, m_atlasImage, &requirements);
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_atlasMemory) !=
      VK_SUCCESS) {
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
    return false;
  }

  vkBindImageMemory(m_device, m_atlasImage, m_atlasMemory, 0);

  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  transitionImageLayout(commandBuffer, m_atlasImage, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(commandBuffer, stagingBuffer, m_atlasImage, width, height,
                    rowLength);
  transitionImageLayout(commandBuffer, m_atlasImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  endSingleTimeCommands(commandBuffer);

  vkDestroyBuffer(m_device, stagingBuffer, nullptr);
  vkFreeMemory(m_device, stagingMemory, nullptr);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_atlasImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_atlasView) !=
      VK_SUCCESS) {
    return false;
  }

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  return vkCreateSampler(m_device, &samplerInfo, nullptr, &m_atlasSampler) ==
         VK_SUCCESS;
}

bool VulkanRenderer::createDescriptorSet() {
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr,
                             &m_descriptorPool) != VK_SUCCESS) {
    return false;
  }

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descriptorSetLayout;

  if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) !=
      VK_SUCCESS) {
    return false;
  }

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = m_atlasView;
  imageInfo.sampler = m_atlasSampler;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = m_descriptorSet;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
  return true;
}

bool VulkanRenderer::createSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailable) !=
          VK_SUCCESS ||
      vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinished) !=
          VK_SUCCESS ||
      vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlight) != VK_SUCCESS) {
    return false;
  }

  return true;
}

void VulkanRenderer::recordCommandBuffer(uint32_t imageIndex) {
  VkCommandBuffer cmd = m_commandBuffers[imageIndex];

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkClearValue clear{};
  clear.color.float32[0] = m_profile.background.redF();
  clear.color.float32[1] = m_profile.background.greenF();
  clear.color.float32[2] = m_profile.background.blueF();
  clear.color.float32[3] = 1.0f;

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = m_renderPass;
  renderPassInfo.framebuffer = m_framebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = m_swapchainExtent;
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clear;

  vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

  VkDeviceSize offsets[] = {0, 0};
  VkBuffer buffers[] = {m_vertexBuffer, m_instanceBuffer};
  vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                          0, 1, &m_descriptorSet, 0, nullptr);

  float screenSize[2] = {static_cast<float>(m_swapchainExtent.width),
                         static_cast<float>(m_swapchainExtent.height)};
  vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(screenSize), screenSize);

  vkCmdDraw(cmd, 4, static_cast<uint32_t>(m_instances.size()), 0, 0);

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

void VulkanRenderer::buildGlyphAtlas(const QFont &font) {
  QFontMetrics metrics(font);
  m_cellSize = QSize(metrics.horizontalAdvance(QLatin1Char('M')),
                     metrics.height());
  m_atlasFont = font;

  const int atlasWidth = 2048;
  const int atlasHeight = 2048;
  m_atlasImageCpu = QImage(atlasWidth, atlasHeight, QImage::Format_Grayscale8);
  m_atlasImageCpu.fill(0);

  QPainter painter(&m_atlasImageCpu);
  painter.setFont(font);
  painter.setPen(Qt::white);

  int x = 0;
  int y = 0;
  const int cellWidth = m_cellSize.width();
  const int cellHeight = m_cellSize.height();

  // Unicode ranges to pre-rasterize
  struct Range { uint start; uint end; };
  static const Range ranges[] = {
      {0x0020, 0x007E},   // Basic Latin
      {0x00A0, 0x00FF},   // Latin-1 Supplement
      {0x0100, 0x024F},   // Latin Extended-A & B
      {0x0370, 0x03FF},   // Greek and Coptic
      {0x0400, 0x04FF},   // Cyrillic
      {0x2000, 0x206F},   // General Punctuation
      {0x2190, 0x21FF},   // Arrows
      {0x2200, 0x22FF},   // Mathematical Operators
      {0x2300, 0x23FF},   // Miscellaneous Technical
      {0x2500, 0x257F},   // Box Drawing
      {0x2580, 0x259F},   // Block Elements
      {0x25A0, 0x25FF},   // Geometric Shapes
      {0x2600, 0x26FF},   // Miscellaneous Symbols
      {0x2700, 0x27BF},   // Dingbats
      {0xE000, 0xE0FF},   // Private Use (Powerline, Nerd Fonts start)
  };

  auto insertGlyph = [&](uint codepoint) {
    if (x + cellWidth > atlasWidth) {
      x = 0;
      y += cellHeight;
    }
    if (y + cellHeight > atlasHeight) {
      return;
    }
    if (m_glyphs.contains(codepoint)) {
      return;
    }

    QPoint baseline(x, y + metrics.ascent());
    painter.drawText(baseline, QString(QChar(codepoint)));

    GlyphInfo glyph{};
    glyph.uvMinX = static_cast<float>(x) / atlasWidth;
    glyph.uvMinY = static_cast<float>(y) / atlasHeight;
    glyph.uvMaxX = static_cast<float>(x + cellWidth) / atlasWidth;
    glyph.uvMaxY = static_cast<float>(y + cellHeight) / atlasHeight;
    m_glyphs.insert(codepoint, glyph);

    x += cellWidth;
  };

  for (const Range &range : ranges) {
    for (uint cp = range.start; cp <= range.end; ++cp) {
      insertGlyph(cp);
    }
  }

  m_atlasCursorX = x;
  m_atlasCursorY = y;

  // Rasterize bold variants for pre-loaded glyphs
  QFont boldFont = font;
  boldFont.setBold(true);
  painter.setFont(boldFont);

  QList<uint> regularKeys = m_glyphs.keys();
  for (uint cp : regularKeys) {
    uint boldKey = cp | 0x80000000u;
    if (m_glyphs.contains(boldKey)) {
      continue;
    }
    if (m_atlasCursorX + cellWidth > atlasWidth) {
      m_atlasCursorX = 0;
      m_atlasCursorY += cellHeight;
    }
    if (m_atlasCursorY + cellHeight > atlasHeight) {
      break;
    }

    QFontMetrics boldMetrics(boldFont);
    QPoint baseline(m_atlasCursorX, m_atlasCursorY + boldMetrics.ascent());
    painter.drawText(baseline, QString(QChar(cp)));

    GlyphInfo glyph{};
    glyph.uvMinX = static_cast<float>(m_atlasCursorX) / atlasWidth;
    glyph.uvMinY = static_cast<float>(m_atlasCursorY) / atlasHeight;
    glyph.uvMaxX = static_cast<float>(m_atlasCursorX + cellWidth) / atlasWidth;
    glyph.uvMaxY = static_cast<float>(m_atlasCursorY + cellHeight) / atlasHeight;
    m_glyphs.insert(boldKey, glyph);

    m_atlasCursorX += cellWidth;
  }

  if (!m_glyphs.contains(QLatin1Char(' ').unicode())) {
    m_glyphs.insert(QLatin1Char(' ').unicode(), {});
  }
}

bool VulkanRenderer::rasterizeGlyph(uint codepoint, bool bold) {
  uint key = bold ? (codepoint | 0x80000000u) : codepoint;
  if (m_glyphs.contains(key)) {
    return true;
  }

  const int atlasWidth = m_atlasImageCpu.width();
  const int atlasHeight = m_atlasImageCpu.height();
  const int cellWidth = m_cellSize.width();
  const int cellHeight = m_cellSize.height();

  if (m_atlasCursorX + cellWidth > atlasWidth) {
    m_atlasCursorX = 0;
    m_atlasCursorY += cellHeight;
  }
  if (m_atlasCursorY + cellHeight > atlasHeight) {
    return false;
  }

  QFont drawFont = m_atlasFont;
  if (bold) {
    drawFont.setBold(true);
  }
  QFontMetrics metrics(drawFont);
  QPainter painter(&m_atlasImageCpu);
  painter.setFont(drawFont);
  painter.setPen(Qt::white);
  QPoint baseline(m_atlasCursorX, m_atlasCursorY + metrics.ascent());
  painter.drawText(baseline, QString(QChar(codepoint)));
  painter.end();

  GlyphInfo glyph{};
  glyph.uvMinX = static_cast<float>(m_atlasCursorX) / atlasWidth;
  glyph.uvMinY = static_cast<float>(m_atlasCursorY) / atlasHeight;
  glyph.uvMaxX = static_cast<float>(m_atlasCursorX + cellWidth) / atlasWidth;
  glyph.uvMaxY = static_cast<float>(m_atlasCursorY + cellHeight) / atlasHeight;
  m_glyphs.insert(key, glyph);

  m_atlasCursorX += cellWidth;
  m_atlasDirty = true;
  return true;
}

void VulkanRenderer::reuploadAtlas() {
  if (!m_atlasDirty || !m_device || m_atlasImageCpu.isNull()) {
    return;
  }
  m_atlasDirty = false;

  const uint32_t width = static_cast<uint32_t>(m_atlasImageCpu.width());
  const uint32_t height = static_cast<uint32_t>(m_atlasImageCpu.height());
  const uint32_t rowLength =
      static_cast<uint32_t>(m_atlasImageCpu.bytesPerLine());
  const VkDeviceSize imageSize =
      static_cast<VkDeviceSize>(m_atlasImageCpu.sizeInBytes());

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

  VkBufferCreateInfo stagingInfo{};
  stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingInfo.size = imageSize;
  stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &stagingInfo, nullptr, &stagingBuffer) !=
      VK_SUCCESS) {
    return;
  }

  VkMemoryRequirements stagingReqs{};
  vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingReqs);
  VkMemoryAllocateInfo stagingAlloc{};
  stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  stagingAlloc.allocationSize = stagingReqs.size;
  stagingAlloc.memoryTypeIndex =
      findMemoryType(stagingReqs.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) !=
      VK_SUCCESS) {
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    return;
  }

  vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

  void *mapped = nullptr;
  vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
  memcpy(mapped, m_atlasImageCpu.constBits(),
         static_cast<size_t>(imageSize));
  vkUnmapMemory(m_device, stagingMemory);

  VkCommandBuffer cmd = beginSingleTimeCommands();
  transitionImageLayout(cmd, m_atlasImage,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(cmd, stagingBuffer, m_atlasImage, width, height, rowLength);
  transitionImageLayout(cmd, m_atlasImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  endSingleTimeCommands(cmd);

  vkDestroyBuffer(m_device, stagingBuffer, nullptr);
  vkFreeMemory(m_device, stagingMemory, nullptr);
}

void VulkanRenderer::updateInstanceBuffer() {
  if (m_instanceBuffer == VK_NULL_HANDLE) {
    return;
  }

  if (m_instances.isEmpty()) {
    return;
  }

  // Grow buffer if needed
  size_t needed = static_cast<size_t>(m_instances.size());
  if (needed > m_instanceCapacity) {
    vkDeviceWaitIdle(m_device);
    vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
    vkFreeMemory(m_device, m_instanceMemory, nullptr);
    m_instanceBuffer = VK_NULL_HANDLE;
    m_instanceMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = needed * sizeof(TerminalQuadInstance);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufInfo, nullptr, &m_instanceBuffer) !=
        VK_SUCCESS) {
      return;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_instanceBuffer, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_instanceMemory) !=
        VK_SUCCESS) {
      vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
      m_instanceBuffer = VK_NULL_HANDLE;
      return;
    }

    vkBindBufferMemory(m_device, m_instanceBuffer, m_instanceMemory, 0);
    m_instanceCapacity = needed;
  }

  void *data = nullptr;
  const size_t size = m_instances.size() * sizeof(TerminalQuadInstance);
  vkMapMemory(m_device, m_instanceMemory, 0, size, 0, &data);
  memcpy(data, m_instances.data(), size);
  vkUnmapMemory(m_device, m_instanceMemory);
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = m_commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_graphicsQueue);

  vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanRenderer::transitionImageLayout(VkCommandBuffer commandBuffer,
                                           VkImage image,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

void VulkanRenderer::copyBufferToImage(VkCommandBuffer commandBuffer,
                                       VkBuffer buffer, VkImage image,
                                       uint32_t width, uint32_t height,
                                       uint32_t rowLength) {
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = rowLength;
  region.bufferImageHeight = height;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties{};
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }

  return 0;
}

VkShaderModule VulkanRenderer::createShaderModule(const QByteArray &code) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = static_cast<size_t>(code.size());
  info.pCode = reinterpret_cast<const uint32_t *>(code.constData());

  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(m_device, &info, nullptr, &module) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return module;
}
