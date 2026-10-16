#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include "application.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <imgui.h>

namespace application {
namespace {

float rotation_angle = 0.0f;
float rotation_speed = 1.0f;
float cube_position_x = 0.0f;
float cube_position_y = 0.0f;
float cube_position_z = 0.0f;
float cube_scale = 1.0f;
bool use_perspective = true;

struct Mat4 {
    float m[16]{};
};

Mat4 identityMatrix() {
    Mat4 result{};
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Mat4 scaleMatrix(float scale) {
    Mat4 result{};
    result.m[0] = scale;
    result.m[5] = scale;
    result.m[10] = scale;
    result.m[15] = 1.0f;
    return result;
}


Mat4 multiplyMatrices(const Mat4& a, const Mat4& b) {
    Mat4 result{};

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[column * 4 + row] +=
                    a.m[k * 4 + row] * b.m[column * 4 + k];
            }
        }
    }

    return result;
}


Mat4 rotationY(float angle) {
    Mat4 result = identityMatrix();

    const float c = std::cos(angle);
    const float s = std::sin(angle);

    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;

    return result;
}


Mat4 translationMatrix(float x, float y, float z) {
    Mat4 result = identityMatrix();

    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;

    return result;
}


Mat4 perspectiveMatrix(float fov, float aspect, float near_plane, float far_plane);
Mat4 orthographicMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane);


Mat4 createMvp(float angle, float aspect, bool perspective) {
    const Mat4 model = multiplyMatrices(
    translationMatrix(
        cube_position_x,
        cube_position_y,
        cube_position_z - 2.0f),
    multiplyMatrices(
        rotationY(angle),
        scaleMatrix(cube_scale)));

    Mat4 projection{};

    if (perspective) {
        projection = perspectiveMatrix(
            1.0472f, aspect, 0.1f, 100.0f);
    } else {
        const float half_height = 1.5f;
        const float half_width = half_height * aspect;

        projection = orthographicMatrix(
            -half_width, half_width,
            -half_height, half_height,
            0.1f, 100.0f);
    }

    return multiplyMatrices(projection, model);
}


Mat4 perspectiveMatrix(float fov, float aspect, float near_plane, float far_plane) {
    Mat4 result{};

    const float f = 1.0f / std::tan(fov * 0.5f);

    result.m[0] = f / aspect;
    result.m[5] = -f;
    result.m[10] = far_plane / (near_plane - far_plane);
    result.m[11] = -1.0f;
    result.m[14] =
        (near_plane * far_plane) / (near_plane - far_plane);

    return result;
}


Mat4 orthographicMatrix(
    float left,
    float right,
    float bottom,
    float top,
    float near_plane,
    float far_plane) {

    Mat4 result = identityMatrix();

    result.m[0] = 2.0f / (right - left);
    result.m[5] = -2.0f / (top - bottom);
    result.m[10] = 1.0f / (near_plane - far_plane);

    result.m[12] = -(right + left) / (right - left);
    result.m[13] = (top + bottom) / (top - bottom);
    result.m[14] = near_plane / (near_plane - far_plane);

    return result;
}



VkShaderModule vertex_shader = VK_NULL_HANDLE;
VkShaderModule fragment_shader = VK_NULL_HANDLE;
VkPipeline graphics_pipeline = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

const VkVertexInputBindingDescription vertex_binding = {
    .binding = 0,
    .stride = sizeof(float) * 3,
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

const VkVertexInputAttributeDescription vertex_attribute = {
    .location = 0,
    .binding = 0,
    .format = VK_FORMAT_R32G32B32_SFLOAT,
    .offset = 0,
};

std::vector<char> readShaderFile(const char* filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return {};
    }

    const size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);

    file.seekg(0);
    file.read(buffer.data(), file_size);

    return buffer;
}

VkShaderModule createShaderModule(const std::vector<char>& code) {
    if (code.empty() || code.size() % 4 != 0) {
        return VK_NULL_HANDLE;
    }

    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;

    if (vkCreateShaderModule(
            graphics::internal::context.device,
            &create_info,
            nullptr,
            &shader_module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return shader_module;
}

bool createGraphicsPipeline() {
    auto& context = graphics::internal::context;

    if (vertex_shader == VK_NULL_HANDLE ||
        fragment_shader == VK_NULL_HANDLE) {
        return false;
    }

    const VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &vertex_attribute,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };


    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };


    const VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float) * 16,
    };

    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    if (vkCreatePipelineLayout(
            graphics::internal::context.device,
            &pipeline_layout_info,
            nullptr,
            &pipeline_layout) != VK_SUCCESS) {
        return false;
    }


    const VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };


    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };


    const VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    const VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };



    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
        .renderPass = graphics::internal::context.render_pass,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(
            graphics::internal::context.device,
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &graphics_pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(
            graphics::internal::context.device,
            pipeline_layout,
            nullptr);
        pipeline_layout = VK_NULL_HANDLE;
        return false;
    }

    return true;

}

struct Vertex {
    float x;
    float y;
    float z;
};


const std::array<Vertex, 8> cube_vertices = {{
    {-0.5f, -0.5f, -0.5f},
    { 0.5f, -0.5f, -0.5f},
    { 0.5f,  0.5f, -0.5f},
    {-0.5f,  0.5f, -0.5f},
    {-0.5f, -0.5f,  0.5f},
    { 0.5f, -0.5f,  0.5f},
    { 0.5f,  0.5f,  0.5f},
    {-0.5f,  0.5f,  0.5f},
}};

const std::array<uint32_t, 36> cube_indices = {{
    0, 2, 1,  0, 3, 2,
    4, 5, 6,  4, 6, 7,
    0, 1, 5,  0, 5, 4,
    2, 3, 7,  2, 7, 6,
    0, 4, 7,  0, 7, 3,
    1, 2, 6,  1, 6, 5
}};
VkBuffer vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_buffer_allocation = VK_NULL_HANDLE;

VkBuffer index_buffer = VK_NULL_HANDLE;
VmaAllocation index_buffer_allocation = VK_NULL_HANDLE;

bool createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VmaAllocation& allocation
) {
    auto& context = graphics::internal::context;

    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    const VmaAllocationCreateInfo allocation_info = {
    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
             VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO,
};

    return vmaCreateBuffer(
        context.allocator,
        &buffer_info,
        &allocation_info,
        &buffer,
        &allocation,
        nullptr
    ) == VK_SUCCESS;
}
} // namespace
bool initialize() {
    const VkDeviceSize vertex_buffer_size =
        sizeof(cube_vertices);

    const VkDeviceSize index_buffer_size =
        sizeof(cube_indices);

    if (!createBuffer(
            vertex_buffer_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertex_buffer,
            vertex_buffer_allocation)) {
        return false;
    }

    if (!createBuffer(
            index_buffer_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            index_buffer,
            index_buffer_allocation)) {
        return false;
    }

VmaAllocationInfo vertex_info{};
vmaGetAllocationInfo(
    graphics::internal::context.allocator,
    vertex_buffer_allocation,
    &vertex_info
);
std::memcpy(vertex_info.pMappedData, cube_vertices.data(), sizeof(cube_vertices));

VmaAllocationInfo index_info{};
vmaGetAllocationInfo(
    graphics::internal::context.allocator,
    index_buffer_allocation,
    &index_info
);
std::memcpy(index_info.pMappedData, cube_indices.data(), sizeof(cube_indices));

    const auto vertex_code = readShaderFile("shaders/cube.vert.spv");
    const auto fragment_code = readShaderFile("shaders/cube.frag.spv");

    vertex_shader = createShaderModule(vertex_code);
    fragment_shader = createShaderModule(fragment_code);

    if (vertex_shader == VK_NULL_HANDLE ||
        fragment_shader == VK_NULL_HANDLE) {
        return false;
    }

if (!createGraphicsPipeline()) {
    std::cerr << "Failed to create graphics pipeline\n";
    return false;
}

std::cout << "Graphics pipeline created successfully\n";


    return true;
}
void shutdown() {
    auto& context = graphics::internal::context;

    vkQueueWaitIdle(context.graphics_queue);

    if (graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
        graphics_pipeline = VK_NULL_HANDLE;
    }

    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }


    if (vertex_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(context.device, vertex_shader, nullptr);
        vertex_shader = VK_NULL_HANDLE;
    }

    if (fragment_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(context.device, fragment_shader, nullptr);
        fragment_shader = VK_NULL_HANDLE;
    }

    if (vertex_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            context.allocator,
            vertex_buffer,
            vertex_buffer_allocation
        );
        vertex_buffer = VK_NULL_HANDLE;
        vertex_buffer_allocation = VK_NULL_HANDLE;
    }

    if (index_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            context.allocator,
            index_buffer,
            index_buffer_allocation
        );
        index_buffer = VK_NULL_HANDLE;
        index_buffer_allocation = VK_NULL_HANDLE;
    }
}

void update(double time) {
rotation_angle = static_cast<float>(time) * rotation_speed;

ImGui::Begin("Cube settings");

ImGui::Checkbox("Perspective projection", &use_perspective);
ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f);
ImGui::SliderFloat("Position X", &cube_position_x, -2.0f, 2.0f);
ImGui::SliderFloat("Position Y", &cube_position_y, -2.0f, 2.0f);
ImGui::SliderFloat("Position Z", &cube_position_z, -1.5f, 1.5f);
ImGui::SliderFloat("Scale", &cube_scale, 0.2f, 2.0f);
    ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    const VkClearValue clear_values[] = {
        {
            .color = {
                { 0.1f, 0.1f, 0.1f, 1.0f }
            }
        },
        {
            .depthStencil = {
                1.0f, 0
            }
        }
    };

    const VkRenderPassBeginInfo render_pass_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = graphics::internal::context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = graphics::internal::context.swapchain_extent,
        },
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };

    
    vkCmdBeginRenderPass(
        fd.command_buffer,
        &render_pass_begin,
        VK_SUBPASS_CONTENTS_INLINE
    );

    auto& context = graphics::internal::context;

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(context.swapchain_extent.width),
        .height = static_cast<float>(context.swapchain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = context.swapchain_extent,
    };

    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(
        fd.command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphics_pipeline
    );

    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(
        fd.command_buffer, 0, 1, &vertex_buffer, &vertex_offset
    );

    vkCmdBindIndexBuffer(
        fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32
    );

    const float aspect =
        static_cast<float>(context.swapchain_extent.width) /
        static_cast<float>(context.swapchain_extent.height);

    const Mat4 mvp = createMvp(rotation_angle, aspect, use_perspective);

    vkCmdPushConstants(
        fd.command_buffer,
        pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(mvp),
        &mvp
    );

    vkCmdDrawIndexed(
        fd.command_buffer,
        static_cast<uint32_t>(cube_indices.size()),
        1, 0, 0, 0
    );

    vkCmdEndRenderPass(fd.command_buffer);

    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application