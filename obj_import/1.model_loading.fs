#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;
in vec4 FragPosLightSpace;

uniform sampler2D texture_diffuse1;
uniform sampler2D shadowMap;
uniform int lightingMode;
uniform int normalMode;
uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightDir;
uniform vec3 backgroundColor;
uniform vec3 objectColor;
uniform vec3 lightColor;
uniform float ambientStrength;
uniform float diffuseStrength;
uniform float specularStrength;
uniform float shininess;
uniform float spotCutOff;
uniform float spotOuterCutOff;
uniform bool noTextures;
uniform bool useShadowMapping;
uniform bool useFaceNormals;
uniform bool useRayTracing;
uniform float rtShadowWidth;
uniform float rtShadowHeight;
uniform float screenWidth;  
uniform float screenHeight;
uniform mat4 lightSpaceMatrix;  // Добавляем uniform для матрицы пространства света

float ShadowCalculation(vec4 fragPosLightSpace, vec3 lightDirNorm, vec3 norm)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0)
        return 0.0;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(norm, lightDirNorm)), 0.0005);

    // PCF for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

void main()
{
    vec3 norm = normalize(Normal);
    if (length(norm) < 0.001) {
        norm = vec3(0.0, 1.0, 0.0);
    }
    vec3 effectiveNormal; // Нормали, которые будут использоваться для вычисления теней
    if (useFaceNormals) {
      // Ищем центр текущего треугольника и его нормаль
      vec3 v1 = dFdx(FragPos);
      vec3 v2 = dFdy(FragPos);
      effectiveNormal = normalize(cross(v1, v2));
    } else {
      // Используем уже имеющиеся vertex normals из меша
      effectiveNormal = normalize(Normal);
    }
    // Получаем базовый цвет из текстуры или из uniform-переменной
    vec4 texColor;
    if (noTextures) {
        texColor = vec4(objectColor, 1.0);
    } else {
        texColor = texture(texture_diffuse1, TexCoords);
    }

    vec3 result = vec3(0.0);

    if (lightingMode == 0) { // NONE
        result = texColor.rgb * objectColor;
    }
    else if (lightingMode == 1) { // NO_LIGHTING
        result = backgroundColor;
    }
    else if (lightingMode == 2) { // AMBIENT
        result = ambientStrength * texColor.rgb * objectColor;
    }
    else {
        vec3 effectiveLightColor = length(lightColor) < 0.001 ? vec3(1.0) : lightColor;
        vec3 ambient = ambientStrength * effectiveLightColor * texColor.rgb * objectColor;

        vec3 lightDirNorm;
        float attenuation = 1.0;
        if (lightingMode == 3 || lightingMode == 5) { // SPOTLIGHT or POINT
            lightDirNorm = normalize(lightPos - FragPos);
            float distance = length(lightPos - FragPos);
            attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
        } else { // DIRECTIONAL
            lightDirNorm = normalize(-lightDir);
            if (length(lightDirNorm) < 0.001) {
                lightDirNorm = vec3(0.0, -1.0, 0.0);
            }
        }

        float lambertFactor = max(dot(norm, lightDirNorm), 0.0);
        vec3 diffuse = diffuseStrength * lambertFactor * effectiveLightColor * texColor.rgb * objectColor;

        vec3 specular = vec3(0.0);
        if (lightingMode == 3 || lightingMode == 4 || lightingMode == 5) { // SPOTLIGHT, DIRECTIONAL, or POINT
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDirNorm, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
            specular = specularStrength * spec * effectiveLightColor;
        }

        float shadow = 0.0;
        if (lightingMode == 3 || lightingMode == 4 || lightingMode == 5) { // SPOTLIGHT, DIRECTIONAL, или POINT
            if (useShadowMapping) {
               shadow = ShadowCalculation(FragPosLightSpace, lightDirNorm, effectiveNormal);
            }
            else if (useRayTracing) {
                vec2 texCoord;
                // Используем проекцию из мирового пространства в пространство экрана
                vec4 projCoord = lightSpaceMatrix * vec4(FragPos, 1.0);
                projCoord.xyz /= projCoord.w;
                projCoord.xyz = projCoord.xyz * 0.5 + 0.5;
    
                float currentDepth = projCoord.z;
                float shadowValue = texture(shadowMap, projCoord.xy).r;
    
                shadow = (shadowValue < 0.5) ? 1.0 : 0.0;
            }
        }

        if (lightingMode == 3) { // SPOTLIGHT
            float theta = dot(lightDirNorm, normalize(-lightDir));
            float epsilon = spotCutOff - spotOuterCutOff;
            float intensity = clamp((theta - spotOuterCutOff) / epsilon, 0.0, 1.0);
            diffuse *= intensity;
            specular *= intensity;
        }
        result = (ambient + (1.0 - shadow) * (diffuse + specular)) * attenuation * texColor.rgb * objectColor;
    }

    FragColor = vec4(result, texColor.a);
}