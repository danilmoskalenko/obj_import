#version 330 core

layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 reflectedView;
uniform mat4 reflectedProj;

out vec4 clipReflectionCoord;

void main()
{
    // �������� ����������� ������� � ������� MVP
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // ���������� � ������������ ��������� ������ (��� ������� ��������)
    clipReflectionCoord = reflectedProj * reflectedView * model * vec4(aPos, 1.0);
}
