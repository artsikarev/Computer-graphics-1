#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform Transform {
    mat4 mvp;
} transform;

void main() {
    gl_Position = transform.mvp * vec4(inPosition, 1.0);
}
