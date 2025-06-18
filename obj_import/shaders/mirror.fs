#version 330 core

in vec4 clipReflectionCoord;
out vec4 FragColor;

uniform sampler2D mirrorTexture;

void main()
{
    // Деление на W и перенос из [-1,1] в [0,1]
    vec2 uv = clipReflectionCoord.xy / clipReflectionCoord.w;
    uv = uv * 0.5 + 0.5;

    // Отбрасываем, если координаты вышли за границы
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        discard;

    FragColor = texture(mirrorTexture, uv);
}
