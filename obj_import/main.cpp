#define GLM_ENABLE_EXPERIMENTAL
#include <glad.h>
#include <glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include "shader.h"
#include "camera.h"
#include "model.h"
#include "imgui.h"
#include "geometry.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include "scene.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// Window dimensions
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Shadow map dimensions
const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
const int SHADOW_TEX_UNIT = 1;

const unsigned int RT_SHADOW_WIDTH = 960;  // в 4 раза меньше
const unsigned int RT_SHADOW_HEIGHT = 540;  // в 4 раза меньше

unsigned int mirrorFBO, mirrorTexture, mirrorRBO;

// Camera
Camera camera;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;


DisplayMode displayMode = NORMAL_MODE;
LightingMode lightingMode = NONE;
ShadowMode shadowMode = SHADOW_NONE;
ShadowNormalType shadowNormalType = SHADOW_VERTEX_NORMALS;
CameraMode cameraMode = BLENDER;

const glm::vec3 INITIAL_LIGHT_POS(0.0f, 10.0f, 0.0f); // Изначальная позиция света

// Object rotation for FIGURE_ROTATION mode
glm::vec3 objectRotation(0.0f);
glm::quat objectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

std::vector<SceneObject> sceneObjects;
int selectedObjectIndex = -1;
bool editSceneMode = true;
bool noTextures = false; // Флаг для отключения текстур

glm::vec3 ComputeSceneCenter() {
   if (sceneObjects.empty()) return glm::vec3(0.0f);
   glm::vec3 sum(0.0f);
   int validObjects = 0;
   for (const auto& obj : sceneObjects) {
      if (!std::isnan(obj.position.x) && !std::isnan(obj.position.y) && !std::isnan(obj.position.z)) {
         sum += obj.position;
         validObjects++;
      }
   }
   return validObjects > 0 ? sum / static_cast<float>(validObjects) : glm::vec3(0.0f);
}

// Генерация текстуры для Ray Tracing результатов
GLuint CreateRayTracingTexture(int width, int height) {
   GLuint textureID;
   glGenTextures(1, &textureID);
   glBindTexture(GL_TEXTURE_2D, textureID);
   std::vector<unsigned char> initialData(width * height, 255);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, initialData.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   return textureID;
}

bool TraceShadowRay(const glm::vec3& start, const glm::vec3& end, const std::vector<SceneObject>& objects, const SceneObject* ignoreObject) {
   glm::vec3 direction = glm::normalize(end - start);
   float lightDistance = glm::length(end - start);
   Ray ray(start, direction);

   // Сортируем объекты по расстоянию до начала луча
   std::vector<std::pair<float, const SceneObject*>> sortedObjects;
   for (const auto& obj : objects) {
      if (&obj == ignoreObject) continue;
      float dist = glm::length(obj.position - start);
      sortedObjects.push_back({ dist, &obj });
   }
   std::sort(sortedObjects.begin(), sortedObjects.end());

   for (const auto& [dist, obj] : sortedObjects) {
      // Вычисляем радиус сферы-обертки на основе реальных размеров объекта
      float maxScale = std::max({ obj->scale.x, obj->scale.y, obj->scale.z });
      float radius = glm::length(obj->model.GetBoundingSphere()) * maxScale;

      float sphereT;
      if (RaySphereIntersect(ray, obj->position, radius, sphereT) &&
         sphereT > 0.001f && sphereT < lightDistance) {
         // Остальной код проверки пересечений с треугольниками...
         glm::mat4 model = glm::mat4(1.0f);
         model = glm::translate(model, obj->position);
         model = glm::rotate(model, glm::radians(obj->rotation.x), glm::vec3(1, 0, 0));
         model = glm::rotate(model, glm::radians(obj->rotation.y), glm::vec3(0, 1, 0));
         model = glm::rotate(model, glm::radians(obj->rotation.z), glm::vec3(0, 0, 1));
         model = glm::scale(model, obj->scale);

         bool triangleHit = false;
         for (const auto& mesh : obj->model.meshes) {
            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
               glm::vec3 v0 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i]].Position, 1.0f));
               glm::vec3 v1 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 1]].Position, 1.0f));
               glm::vec3 v2 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 2]].Position, 1.0f));

               Triangle triangle(v0, v1, v2);
               float triT;

               if (RayTriangleIntersect(ray, triangle, triT) && triT > 0.01f && triT < lightDistance) {
                  static int triangleHitCounter = 0;
                  if (triangleHitCounter++ % 1000000 == 0) {
                     std::cout << "\nTriangle intersection found:" << std::endl;
                     std::cout << "Triangle vertices:" << std::endl;
                     std::cout << "V0: [" << v0.x << ", " << v0.y << ", " << v0.z << "]" << std::endl;
                     std::cout << "V1: [" << v1.x << ", " << v1.y << ", " << v1.z << "]" << std::endl;
                     std::cout << "V2: [" << v2.x << ", " << v2.y << ", " << v2.z << "]" << std::endl;
                     std::cout << "Intersection distance: " << triT << std::endl;
                     std::cout << "Light distance: " << lightDistance << std::endl;
                  }
                  triangleHit = true;
                  break; // Нашли пересечение, выходим из цикла треугольников
               }
            }
            if (triangleHit) break; // Выходим из цикла мешей
         }
         if (triangleHit) return true; // Точка в тени
      }
   }
   return false; // Точка освещена
}

int main()
{
   glfwInit();
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

   GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "obj_import", NULL, NULL);
   if (window == NULL)
   {
      std::cout << "Failed to create GLFW window" << std::endl;
      glfwTerminate();
      return -1;
   }

   glfwMakeContextCurrent(window);
   glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
   glfwSetCursorPosCallback(window, mouse_callback);
   glfwSetScrollCallback(window, scroll_callback);
   glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

   if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
   {
      std::cout << "Failed to initialize GLAD" << std::endl;
      return -1;
   }

   glEnable(GL_DEPTH_TEST);

   // Создаем текстуру для Ray Tracing
   GLuint rayTracingTexture = CreateRayTracingTexture(RT_SHADOW_WIDTH, RT_SHADOW_HEIGHT);

   GLuint depthMapFBO;
   glGenFramebuffers(1, &depthMapFBO);
   GLuint depthMap;
   glGenTextures(1, &depthMap);
   if (depthMap == 0) {
      std::cout << "Failed to generate depthMap texture" << std::endl;
   }
   glBindTexture(GL_TEXTURE_2D, depthMap);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
   float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

   glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::cout << "Framebuffer incomplete: " << status << std::endl;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   camera.Reset();
   camera.Target = ComputeSceneCenter();

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO(); (void)io;
   ImGui::StyleColorsDark();
   ImGui_ImplGlfw_InitForOpenGL(window, true);
   ImGui_ImplOpenGL3_Init("#version 330");

   Shader ourShader("1.model_loading.vs", "1.model_loading.fs");
   Shader depthShader("depth.vs", "depth.fs");
   Shader shaderNormalFace("line_vertex_shader.glsl", "line_fragment_shader.glsl");
   Shader shaderNormalVertex("v_line_vertex_shader.glsl", "v_line_fragment_shader.glsl");
   Shader lightShader("light_vertex_shader.glsl", "light_fragment_shader.glsl");
   Shader mirrorShader("shaders/mirror.vs", "shaders/mirror.fs");

   static std::string modelPath = "resources/objects/Crate/Crate1.obj";
   Model ourModel(modelPath);

   std::vector<glm::vec3> faceNormalLines;
   std::vector<glm::vec3> vertexNormalLines;


   std::vector<Vertex> mirrorVertices = {
       {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}}, // Левый нижний
       {{ 1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, // Правый нижний
       {{ 1.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}, // Правый верхний
       {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}  // Левый верхний
   };
   std::vector<unsigned int> mirrorIndices = { 0, 1, 2, 2, 3, 0 };
   std::vector<Texture> mirrorTextures; // Пока без текстур
   Mesh mirrorMesh(mirrorVertices, mirrorIndices, mirrorTextures);
   Model mirrorModel; // Пустой конструктор без файла
   mirrorModel.meshes.clear(); // Удаляем любые дефолтные меши
   mirrorModel.meshes.push_back(mirrorMesh); // Добавляем наш меш
   SceneObject mirror;
   mirror.name = "mirror"; // Имя для идентификации зеркала
   mirror.model = mirrorModel;
   mirror.position = glm::vec3(0.0f, 0.0f, -5.0f);
   mirror.scale = glm::vec3(2.0f, 2.0f, 1.0f);
   sceneObjects.push_back(mirror);


   for (auto& mesh : ourModel.meshes) {
      for (size_t i = 0; i < mesh.indices.size(); i += 3) {
         glm::vec3 v0 = mesh.vertices[mesh.indices[i]].Position;
         glm::vec3 v1 = mesh.vertices[mesh.indices[i + 1]].Position;
         glm::vec3 v2 = mesh.vertices[mesh.indices[i + 2]].Position;
         glm::vec3 center = (v0 + v1 + v2) / 3.0f;
         glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

         faceNormalLines.push_back(center);
         faceNormalLines.push_back(center + normal * 0.8f);

         for (int j = 0; j < 3; ++j) {
            glm::vec3 pos = mesh.vertices[mesh.indices[i + j]].Position;
            glm::vec3 norm = mesh.vertices[mesh.indices[i + j]].Normal;
            vertexNormalLines.push_back(pos);
            vertexNormalLines.push_back(pos + norm * 0.5f);
         }
      }
   }

   GLuint faceNormalsVAO, faceNormalsVBO;
   glGenVertexArrays(1, &faceNormalsVAO);
   glGenBuffers(1, &faceNormalsVBO);
   glBindVertexArray(faceNormalsVAO);
   glBindBuffer(GL_ARRAY_BUFFER, faceNormalsVBO);
   glBufferData(GL_ARRAY_BUFFER, faceNormalLines.size() * sizeof(glm::vec3), faceNormalLines.data(), GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
   glBindVertexArray(0);

   GLuint vertexNormalsVAO, vertexNormalsVBO;
   glGenVertexArrays(1, &vertexNormalsVAO);
   glGenBuffers(1, &vertexNormalsVBO);
   glBindVertexArray(vertexNormalsVAO);
   glBindBuffer(GL_ARRAY_BUFFER, vertexNormalsVBO);
   glBufferData(GL_ARRAY_BUFFER, vertexNormalLines.size() * sizeof(glm::vec3), vertexNormalLines.data(), GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
   glBindVertexArray(0);

   // Генерация VAO и VBO для сферы света
   std::vector<float> sphereVertices;
   std::vector<unsigned int> sphereIndices;
   GenerateSphere(0.5f, 32, 16, sphereVertices, sphereIndices); // радиус и детализация

   GLuint sphereVAO, sphereVBO, sphereEBO;
   glGenVertexArrays(1, &sphereVAO);
   glGenBuffers(1, &sphereVBO);
   glGenBuffers(1, &sphereEBO);

   glBindVertexArray(sphereVAO);
   glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
   glBufferData(GL_ARRAY_BUFFER, sphereVertices.size() * sizeof(float), sphereVertices.data(), GL_STATIC_DRAW);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereIndices.size() * sizeof(unsigned int), sphereIndices.data(), GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
   glBindVertexArray(0);

   // Инициализация FBO для зеркала
   unsigned int mirrorFBO;
   glGenFramebuffers(1, &mirrorFBO);

   // Вычисление отраженной камеры
   glm::vec3 mirrorPos = mirror.position;
   glm::vec3 mirrorNormal = glm::normalize(mirror.mirrorNormal);
   glm::vec3 cameraToMirror = camera.Position - mirrorPos;
   float distance = glm::dot(cameraToMirror, mirrorNormal);
   glm::vec3 reflectedCameraPos = camera.Position - 2.0f * distance * mirrorNormal;
   glm::vec3 reflectedFront = glm::reflect(camera.Front, mirrorNormal);
   glm::vec3 reflectedUp = glm::reflect(camera.Up, mirrorNormal);
   glm::mat4 reflectedView = glm::lookAt(reflectedCameraPos, reflectedCameraPos + reflectedFront, reflectedUp);
   glBindFramebuffer(GL_FRAMEBUFFER, mirrorFBO);

   // Создание текстуры для отражения
   unsigned int mirrorTexture;
   glGenTextures(1, &mirrorTexture);
   glBindTexture(GL_TEXTURE_2D, mirrorTexture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mirrorTexture, 0);

   // Создание буфера глубины
   unsigned int mirrorRBO;
   glGenRenderbuffers(1, &mirrorRBO);
   glBindRenderbuffer(GL_RENDERBUFFER, mirrorRBO);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mirrorRBO);

   // Проверка FBO
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::cout << "Framebuffer is not complete!" << std::endl;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   glm::vec3 lightPos = INITIAL_LIGHT_POS;
   glm::vec3 lightDir(0.0f, -1.0f, 0.0f);
   glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
   glm::vec3 backgroundColor(0.05f, 0.05f, 0.05f);
   glm::vec3 objectColor(1.0f, 1.0f, 1.0f);
   float ambientStrength = 0.2f;
   float lightIntensity = 1.0f;
   float diffuseStrength = 1.0f;
   float specularStrength = 0.2f;
   float shininess = 8.0f;

   char modelPathInput[256] = "resources/objects/Crate/Crate1.obj";
   bool reloadModel = false;

   while (!glfwWindowShouldClose(window))
   {
      float currentFrame = glfwGetTime();
      deltaTime = currentFrame - lastFrame;
      lastFrame = currentFrame;
      processInput(window);

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      ImGui::Begin("Control Panel");
      ImGui::Text("Camera Settings");
      ImGui::Separator();
      if (editSceneMode) {
         ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)",
            camera.Position.x, camera.Position.y, camera.Position.z);
         ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)",
            camera.Target.x, camera.Target.y, camera.Target.z);
      }

      ImGui::Checkbox("Edit Whole Scene", &editSceneMode);

      if (editSceneMode) selectedObjectIndex = -1;

      if (editSceneMode) {
         const char* cameraItems[] = { "Scene Rotation", "Strafe", "Quaternion", "Blender" };
         static int cameraCurrent = static_cast<int>(cameraMode);

         if (ImGui::Combo("Camera Mode", &cameraCurrent, cameraItems, IM_ARRAYSIZE(cameraItems))) {
            camera.SaveStateForMode(cameraMode);
            cameraMode = static_cast<CameraMode>(cameraCurrent);
            camera.RestoreStateForMode(cameraMode);

            if (cameraMode != FIGURE_ROTATION) {
               objectRotation = glm::vec3(0.0f);
            }
            if (cameraMode != QUATERNION) {
               objectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
         }
         if (ImGui::Button("Reset Camera")) {
            if (cameraMode == BLENDER) {
               camera.Reset();
               camera.Target = ComputeSceneCenter();
            }
            else {
               camera.RestoreInitialState();
            }
         }
         switch (cameraMode) {
         case FIGURE_ROTATION:
            ImGui::Text("Controls: X/Y/Z to rotate scene (hold Shift to reverse)");
            break;
         case STRAFE:
            ImGui::Text("Controls: WASD to move, Space/Ctrl to up/down)");
            break;
         case QUATERNION:
            ImGui::Text("Controls: Hold LMB to rotate scene)");
            break;
         case BLENDER:
            ImGui::Text("Controls: MMB to orbit/pan, Scroll to zoom, WASD to move)");
            break;
         }
      }
      else {
         ImGui::Text("Camera controls disabled in object mode)");
      }

      ImGui::Text("Rendering Settings");
      ImGui::Separator();
      ImGui::Checkbox("No Textures", &noTextures);

      ImGui::Text("Display Settings");
      ImGui::Separator();
      const char* displayItems[] = { "Normal", "Face Normals", "Vertex Normals" };
      static int displayCurrent = static_cast<int>(displayMode);
      if (ImGui::Combo("Display Mode", &displayCurrent, displayItems, IM_ARRAYSIZE(displayItems))) {
         displayMode = static_cast<DisplayMode>(displayCurrent);
      }

      ImGui::Text("Lighting Settings");
      ImGui::Separator();
      const char* lightingItems[] = { "None", "No Lighting", "Ambient", "Spotlight", "Directional", "Point" };
      static int lightingCurrent = static_cast<int>(lightingMode);
      if (ImGui::Combo("Lighting Mode", &lightingCurrent, lightingItems, IM_ARRAYSIZE(lightingItems))) {
         lightingMode = static_cast<LightingMode>(lightingCurrent);
         if (lightingMode == NO_LIGHTING) {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
         }
         else {
            glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);
         }
      }

      if (lightingMode == POINT || lightingMode == SPOTLIGHT || lightingMode == DIRECTIONAL) {
         ImGui::Text("Light Location");
         static float pos[3] = { lightPos.x, lightPos.y, lightPos.z };
         if (ImGui::DragFloat("Light X", &pos[0], 0.1f))
            lightPos.x = pos[0];
         if (ImGui::DragFloat("Light Y", &pos[1], 0.1f))
            lightPos.y = pos[1];
         if (ImGui::DragFloat("Light Z", &pos[2], 0.1f))
            lightPos.z = pos[2];
         if (ImGui::Button("Reset Light Location")) {
            lightPos = INITIAL_LIGHT_POS;
            pos[0] = lightPos.x;
            pos[1] = lightPos.y;
            pos[2] = lightPos.z;
         }
         ImGui::Separator();
      }

      ImGui::SliderFloat("Ambient Strength", &ambientStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Light Intensity", &lightIntensity, 0.0f, 5.0f, "%.2f");
      ImGui::SliderFloat("Diffuse Strength", &diffuseStrength, 0.0f, 2.0f, "%.2f");
      ImGui::SliderFloat("Specular Strength", &specularStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Shininess", &shininess, 2.0f, 64.0f, "%.1f");

      ImGui::Text("Color Settings");
      ImGui::Separator();
      ImGui::ColorEdit3("Object Color", (float*)&objectColor);
      ImGui::ColorEdit3("Background Color", (float*)&backgroundColor);

      ImGui::Text("Shadow Settings");
      ImGui::Separator();
      bool shadowControlsEnabled = (lightingMode == POINT || lightingMode == SPOTLIGHT || lightingMode == DIRECTIONAL);
      if (!shadowControlsEnabled) {
         ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
      }

      bool shadowMapping = (shadowMode == SHADOW_MAPPING);
      bool rayTracing = (shadowMode == SHADOW_RAYTRACING);

      if (ImGui::Checkbox("Shadow Mapping", &shadowMapping)) {
         if (shadowMapping && shadowControlsEnabled) {
            shadowMode = SHADOW_MAPPING;
         }
         else {
            shadowMode = SHADOW_NONE;
         }
      }

      if (shadowMode == SHADOW_MAPPING) {
         ImGui::Indent();
         const char* normalItems[] = { "Vertex Normals", "Face Normals" };
         static int normalCurrent = static_cast<int>(shadowNormalType);
         if (ImGui::Combo("Shadow Normal Type", &normalCurrent, normalItems, IM_ARRAYSIZE(normalItems))) {
            shadowNormalType = static_cast<ShadowNormalType>(normalCurrent);
         }
         ImGui::Unindent();
      }

      if (ImGui::Checkbox("Ray Tracing", &rayTracing)) {
         if (rayTracing && shadowControlsEnabled) {
            shadowMode = SHADOW_RAYTRACING;
            if (lightingMode == NONE || lightingMode == NO_LIGHTING || lightingMode == AMBIENT) {
               lightingMode = POINT;
            }
         }
         else {
            shadowMode = SHADOW_NONE;
         }
      }

      if (!shadowControlsEnabled) {
         ImGui::PopStyleVar();
         if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shadow settings are only available for Point, Spotlight and Directional lights");
         }
      }

      ImGui::Text("Model Loading");
      ImGui::Separator();
      ImGui::InputText("Model Path", modelPathInput, IM_ARRAYSIZE(modelPathInput));
      if (ImGui::Button("Load Model")) {
         try {
            std::string name = std::filesystem::path(modelPathInput).filename().string();
            sceneObjects.push_back({ name, Model(modelPathInput) });
         }
         catch (const std::exception& e) {
            std::cout << "Failed to load model: " << e.what() << std::endl;
         }
      }
      ImGui::End();

      ImGui::Begin("Object Management");
      for (int i = 0; i < sceneObjects.size(); ++i) {
         SceneObject& obj = sceneObjects[i];
         std::string label = std::to_string(i + 1) + "-" + obj.name;

         bool selected = (i == selectedObjectIndex);
         if (ImGui::Checkbox(("##select" + std::to_string(i)).c_str(), &selected)) {
            if (selected) {
               selectedObjectIndex = i;
               editSceneMode = false;
               camera.Target = sceneObjects[i].position;
               camera.Position = camera.Target + glm::vec3(0.0f, 0.0f, 10.0f);
               camera.Front = glm::normalize(camera.Target - camera.Position);
               camera.Right = glm::normalize(glm::cross(camera.Front, camera.WorldUp));
               camera.Up = glm::normalize(glm::cross(camera.Right, camera.Front));
               camera.Yaw = -90.0f;
               camera.Pitch = 0.0f;
            }
            else {
               selectedObjectIndex = -1;
               editSceneMode = true;
               camera.Target = ComputeSceneCenter();
            }
         }

         ImGui::SameLine();
         ImGui::Text("%s", label.c_str());

         ImGui::SameLine();
         if (ImGui::Button(("Delete##" + std::to_string(i)).c_str())) {
            sceneObjects.erase(sceneObjects.begin() + i);
            if (selectedObjectIndex == i) selectedObjectIndex = -1;
            else if (selectedObjectIndex > i) selectedObjectIndex--;
            break;
         }
      }

      ImGui::End();

      if (reloadModel) {
         try {
            ourModel = Model(modelPath);
            faceNormalLines.clear();
            vertexNormalLines.clear();
            for (auto& mesh : ourModel.meshes) {
               for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                  glm::vec3 v0 = mesh.vertices[mesh.indices[i]].Position;
                  glm::vec3 v1 = mesh.vertices[mesh.indices[i + 1]].Position;
                  glm::vec3 v2 = mesh.vertices[mesh.indices[i + 2]].Position;
                  glm::vec3 center = (v0 + v1 + v2) / 3.0f;
                  glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                  faceNormalLines.push_back(center);
                  faceNormalLines.push_back(center + normal * 0.8f);

                  for (int j = 0; j < 3; ++j) {
                     glm::vec3 pos = mesh.vertices[mesh.indices[i + j]].Position;
                     glm::vec3 norm = mesh.vertices[mesh.indices[i + j]].Normal;
                     vertexNormalLines.push_back(pos);
                     vertexNormalLines.push_back(pos + norm * 0.5f);
                  }
               }
            }
            glBindVertexArray(faceNormalsVAO);
            glBindBuffer(GL_ARRAY_BUFFER, faceNormalsVBO);
            glBufferData(GL_ARRAY_BUFFER, faceNormalLines.size() * sizeof(glm::vec3), faceNormalLines.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);

            glBindVertexArray(vertexNormalsVAO);
            glBindBuffer(GL_ARRAY_BUFFER, vertexNormalsVBO);
            glBufferData(GL_ARRAY_BUFFER, vertexNormalLines.size() * sizeof(glm::vec3), vertexNormalLines.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);
         }
         catch (const std::exception& e) {
            std::cout << "Failed to load model: " << e.what() << std::endl;
         }
         reloadModel = false;
      }

      glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
      glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(2.0f, 4.0f);
      glClear(GL_DEPTH_BUFFER_BIT);

      glm::mat4 lightProjection, lightView, lightSpaceMatrix;
      glm::vec3 sceneCenter = ComputeSceneCenter();

      if (lightingMode == DIRECTIONAL || lightingMode == POINT || lightingMode == SPOTLIGHT) {
         lightProjection = glm::perspective(glm::radians(90.0f), (float)SHADOW_WIDTH / (float)SHADOW_HEIGHT, 0.1f, 20.0f);
         lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
      }
      lightSpaceMatrix = lightProjection * lightView;

      depthShader.use();
      depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

      for (const SceneObject& obj : sceneObjects) {
         glm::mat4 model = glm::mat4(1.0f);
         if (cameraMode == FIGURE_ROTATION && editSceneMode && selectedObjectIndex == -1) {
            glm::vec3 center = ComputeSceneCenter();
            model = glm::translate(model, center);
            model = glm::rotate(model, glm::radians(objectRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(objectRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(objectRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, -center);
         }
         else if (cameraMode == QUATERNION && editSceneMode && selectedObjectIndex == -1) {
            glm::vec3 center = ComputeSceneCenter();
            model = glm::translate(model, center);
            model *= glm::mat4_cast(objectOrientation);
            model = glm::translate(model, -center);
         }
         model = glm::translate(model, obj.position);
         model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
         model = glm::scale(model, obj.scale);

         depthShader.setMat4("model", model);
         obj.model.Draw(depthShader);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDisable(GL_POLYGON_OFFSET_FILL);
      glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);

      // === 1. Рендер сцены в текстуру (отражение в mirror) ===
      glBindFramebuffer(GL_FRAMEBUFFER, mirrorFBO);
      glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);

      // Матрица проекции (одна и та же)
      glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
      glm::mat4 reflectedView;

      // Найдём объект-зеркало
      SceneObject* mirrorPtr = nullptr;
      for (SceneObject& obj : sceneObjects) {
         if (obj.name == "mirror") {
            mirrorPtr = &obj;
            break;
         }
      }

      if (mirrorPtr) {
         SceneObject& mirror = *mirrorPtr;

         // 1. Вычисление нормали зеркала в мировых координатах
         glm::mat4 rotMat = glm::mat4(1.0f);
         rotMat = glm::rotate(rotMat, glm::radians(mirror.rotation.x), glm::vec3(1, 0, 0));
         rotMat = glm::rotate(rotMat, glm::radians(mirror.rotation.y), glm::vec3(0, 1, 0));
         rotMat = glm::rotate(rotMat, glm::radians(mirror.rotation.z), glm::vec3(0, 0, 1));

         // Локальная нормаль зеркала — Z-ось (для XY-плоскости)
         glm::vec3 mirrorNormal = glm::normalize(glm::vec3(rotMat * glm::vec4(0, 0, 1, 0)));
         mirror.mirrorNormal = mirrorNormal;  // обновим в структуре на всякий случай

         // 2. Отразим позицию камеры относительно плоскости зеркала
         glm::vec3 cameraPos = camera.Position;
         glm::vec3 mirrorPoint = mirror.position;

         glm::vec3 reflectedCameraPos = cameraPos - 2.0f * glm::dot(cameraPos - mirrorPoint, mirrorNormal) * mirrorNormal;

         // 3. Отразим направления
         glm::vec3 reflectedFront = camera.Front - 2.0f * glm::dot(camera.Front, mirrorNormal) * mirrorNormal;
         glm::vec3 reflectedUp = camera.Up - 2.0f * glm::dot(camera.Up, mirrorNormal) * mirrorNormal;

         // 4. Построим отражённую матрицу вида
         reflectedView = glm::lookAt(reflectedCameraPos, reflectedCameraPos + reflectedFront, reflectedUp);

         // 5. Лог для проверки по оси Z
         std::cout << "Camera Z: " << cameraPos.z << std::endl;
         std::cout << "Mirror Z: " << mirrorPoint.z << std::endl;
         std::cout << "Normal Z: " << mirrorNormal.z << std::endl;
         std::cout << "ReflectedCameraPos Z: " << reflectedCameraPos.z << std::endl;


         // 1.2 Рендер всех объектов КРОМЕ зеркала в mirrorFBO
         for (const SceneObject& obj : sceneObjects) {
            if (obj.name == "mirror") continue; // не рисуем зеркало в его отражении

            ourShader.use();
            ourShader.setMat4("projection", projection);
            ourShader.setMat4("view", reflectedView);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
            model = glm::scale(model, obj.scale);

            ourShader.setMat4("model", model);
            obj.model.Draw(ourShader);
         }

         // Отвязываем текстуры после рендеринга в FBO
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, 0);
      }

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      // === 2. Рендер финальной сцены на экран ===
      glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);

      // Основной вид камеры
      glm::mat4 view = camera.GetViewMatrix();
      glm::mat4 reflectedProjection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);

      // 2.1 Рендер всех объектов (включая зеркало)
      for (const SceneObject& obj : sceneObjects) {
         glm::mat4 model = glm::mat4(1.0f);
         model = glm::translate(model, obj.position);
         model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
         model = glm::scale(model, obj.scale);

         if (obj.name == "mirror") {
            // Рендер зеркала (используем проекцию с текстурой)
            // Используем GL_TEXTURE1 для mirrorTexture
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mirrorTexture);
            mirrorShader.use();
            mirrorShader.setMat4("model", model);               // модель зеркала
            mirrorShader.setMat4("view", view);                 // обычная камера
            mirrorShader.setMat4("projection", projection);     // обычная проекция
            mirrorShader.setMat4("reflectedView", reflectedView);         // из отражённой камеры
            mirrorShader.setMat4("reflectedProj", reflectedProjection);   // правильная проекция
            mirrorShader.setInt("mirrorTexture", 1);            // если текстура привязана к GL_TEXTURE1 // Указываем юнит 1 для текстуры зеркала

            obj.model.Draw(mirrorShader);
         }
         else {
            // Остальные объекты
            ourShader.use();
            ourShader.setMat4("model", model);
            ourShader.setMat4("view", view);
            ourShader.setMat4("projection", projection);
            obj.model.Draw(ourShader);
         }
      }

      glm::mat4 invProjection = glm::inverse(projection);
      glm::mat4 invView = glm::inverse(view);
      // Ray Tracing
      if (shadowMode == SHADOW_RAYTRACING && (lightingMode == POINT || lightingMode == SPOTLIGHT || lightingMode == DIRECTIONAL)) {
         std::vector<unsigned char> shadowData(RT_SHADOW_WIDTH * RT_SHADOW_HEIGHT, 255);
         static int hitCount = 0; // Счетчик попаданий первичных лучей
         int localHitCount = 0; // Локальный счётчик для текущего кадра

         #pragma omp parallel for collapse(2)
         for (int y = 0; y < RT_SHADOW_HEIGHT; ++y) {
            for (int x = 0; x < RT_SHADOW_WIDTH; ++x) {
               float ndcX = (2.0f * x) / RT_SHADOW_WIDTH - 1.0f;
               float ndcY = 1.0f - (2.0f * y) / RT_SHADOW_HEIGHT;

               glm::vec4 clipPos = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
               glm::vec4 viewPos = invProjection * clipPos;
               viewPos /= viewPos.w; // Нормализация в пространстве вида
               glm::vec4 worldPos = invView * viewPos;
               worldPos /= worldPos.w;

               glm::vec3 rayOrigin = camera.Position;
               glm::vec3 rayDir = glm::normalize(glm::vec3(worldPos) - rayOrigin);
               glm::vec3 hitPoint = rayOrigin;
               float minT = std::numeric_limits<float>::max();
               bool hit = false;
               const SceneObject* hitObject = nullptr;

               for (const auto& obj : sceneObjects) {
                  glm::mat4 model = glm::mat4(1.0f);
                  model = glm::translate(model, obj.position);
                  model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
                  model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
                  model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
                  model = glm::scale(model, obj.scale);
                  //std::cout << "Processing object: " << obj.name << ", Meshes count: " << obj.model.meshes.size() << std::endl;

                  for (const auto& mesh : obj.model.meshes) {
                     for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                        glm::vec3 v0 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i]].Position, 1.0f));
                        glm::vec3 v1 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 1]].Position, 1.0f));
                        glm::vec3 v2 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 2]].Position, 1.0f));

                        Triangle triangle(v0, v1, v2);
                        float t;
                        if (RayTriangleIntersect(Ray(rayOrigin, rayDir), triangle, t) && t > 0.001f && t < minT) {
                           minT = t;
                           hitPoint = rayOrigin + rayDir * t;
                           hit = true;
                           hitObject = &obj;
                           //std::cout << "Hit object: " << obj.name << " at t = " << t << std::endl;
                        }
                     }
                  }
               }

               bool inShadow = hit ? TraceShadowRay(hitPoint, lightPos, sceneObjects, hitObject) : false;
               shadowData[y * RT_SHADOW_WIDTH + x] = inShadow ? 0 : 255;

               // Отладка центрального пикселя
               if (x == RT_SHADOW_WIDTH / 2 && y == RT_SHADOW_HEIGHT / 2) {
                  std::cout << "Center pixel debug:" << std::endl;
                  std::cout << "Ray origin: [" << rayOrigin.x << ", " << rayOrigin.y << ", " << rayOrigin.z << "]" << std::endl;
                  std::cout << "Ray direction: [" << rayDir.x << ", " << rayDir.y << ", " << rayDir.z << "]" << std::endl;
                  std::cout << "Hit: " << hit << std::endl;
                  if (hit) {
                     std::cout << "Hit point: [" << hitPoint.x << ", " << hitPoint.y << ", " << hitPoint.z << "]" << std::endl;
                     std::cout << "Shadow ray direction: [" << glm::normalize(lightPos - hitPoint).x << ", "
                        << glm::normalize(lightPos - hitPoint).y << ", "
                        << glm::normalize(lightPos - hitPoint).z << "]" << std::endl;
                     std::cout << "In shadow: " << inShadow << std::endl;
                  }
               }

               if (hit) {
                  #pragma omp atomic
                  localHitCount++;
               }
            }
         }

         hitCount = localHitCount; // Обновляем глобальный счётчик
         glBindTexture(GL_TEXTURE_2D, rayTracingTexture); // Явная привязка текстуры
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RT_SHADOW_WIDTH, RT_SHADOW_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, shadowData.data());
         glFinish();

         static int objectDebugCounter = 0;
         if (objectDebugCounter++ % 60 == 0) {
            std::cout << "Light position: [" << lightPos.x << ", " << lightPos.y << ", " << lightPos.z << "]" << std::endl;
            std::cout << "Camera position: [" << camera.Position.x << ", " << camera.Position.y << ", " << camera.Position.z << "]" << std::endl;
            std::cout << "Scene objects (" << sceneObjects.size() << "):" << std::endl;
            for (size_t i = 0; i < sceneObjects.size(); ++i) {
               const auto& obj = sceneObjects[i];
               std::cout << "Object " << i << " (" << obj.name << "): Position ["
                  << obj.position.x << ", " << obj.position.y << ", " << obj.position.z
                  << "], Scale [" << obj.scale.x << ", " << obj.scale.y << ", " << obj.scale.z
                  << std::endl;
            }
         }

      }

      ourShader.setMat4("projection", projection);
      ourShader.setMat4("view", view);
      ourShader.setInt("lightingMode", static_cast<int>(lightingMode));
      ourShader.setInt("normalMode", static_cast<int>(displayMode));
      ourShader.setVec3("viewPos", camera.Position);
      ourShader.setVec3("lightPos", lightPos);
      ourShader.setVec3("lightDir", glm::normalize(lightDir));
      ourShader.setVec3("lightColor", lightColor * lightIntensity);
      ourShader.setVec3("backgroundColor", backgroundColor);
      ourShader.setVec3("objectColor", objectColor);
      ourShader.setFloat("ambientStrength", ambientStrength);
      ourShader.setFloat("diffuseStrength", diffuseStrength);
      ourShader.setFloat("specularStrength", specularStrength);
      ourShader.setFloat("shininess", shininess);
      ourShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
      ourShader.setBool("noTextures", noTextures);
      ourShader.setBool("useRayTracing", shadowMode == SHADOW_RAYTRACING);
      ourShader.setBool("useShadowMapping", shadowMode == SHADOW_MAPPING);
      ourShader.setBool("useFaceNormals", shadowMode == SHADOW_MAPPING && shadowNormalType == SHADOW_FACE_NORMALS);
      ourShader.setFloat("rtShadowWidth", static_cast<float>(RT_SHADOW_WIDTH));
      ourShader.setFloat("rtShadowHeight", static_cast<float>(RT_SHADOW_HEIGHT));
      ourShader.setFloat("screenWidth", static_cast<float>(SCR_WIDTH));
      ourShader.setFloat("screenHeight", static_cast<float>(SCR_HEIGHT));

      if (shadowMode == SHADOW_RAYTRACING) {
         glActiveTexture(GL_TEXTURE0 + SHADOW_TEX_UNIT);
         glBindTexture(GL_TEXTURE_2D, rayTracingTexture);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         ourShader.setInt("shadowMap", SHADOW_TEX_UNIT);
      }
      if (shadowMode == SHADOW_MAPPING) {
         glActiveTexture(GL_TEXTURE0 + SHADOW_TEX_UNIT);
         glBindTexture(GL_TEXTURE_2D, depthMap);
         ourShader.setInt("shadowMap", SHADOW_TEX_UNIT);
      }

      for (const SceneObject& obj : sceneObjects) {
         glm::mat4 model = glm::mat4(1.0f);
         if (cameraMode == FIGURE_ROTATION && editSceneMode && selectedObjectIndex == -1) {
            glm::vec3 center = ComputeSceneCenter();
            model = glm::translate(model, center);
            model = glm::rotate(model, glm::radians(objectRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(objectRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(objectRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, -center);
         }
         else if (cameraMode == QUATERNION && editSceneMode && selectedObjectIndex == -1) {
            glm::vec3 center = ComputeSceneCenter();
            model = glm::translate(model, center);
            model *= glm::mat4_cast(objectOrientation);
            model = glm::translate(model, -center);
         }
         model = glm::translate(model, obj.position);
         model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
         model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
         model = glm::scale(model, obj.scale);

         ourShader.setMat4("model", model);
         obj.model.Draw(ourShader);
      }

      if (lightingMode == POINT || lightingMode == SPOTLIGHT || lightingMode == DIRECTIONAL) {
         lightShader.use();
         lightShader.setMat4("projection", projection);
         lightShader.setMat4("view", view);
         glm::mat4 lightModel = glm::mat4(1.0f);
         lightModel = glm::translate(lightModel, lightPos);
         lightShader.setMat4("model", lightModel);
         lightShader.setVec3("lightColor", lightColor);
         glBindVertexArray(sphereVAO);
         glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphereIndices.size()), GL_UNSIGNED_INT, 0);
         glBindVertexArray(0);
      }



      if (editSceneMode && (displayMode == FACE_NORMALS || displayMode == VERTEX_NORMALS)) {
         std::vector<glm::vec3> allFaceNormalLines;
         std::vector<glm::vec3> allVertexNormalLines;

         for (const SceneObject& obj : sceneObjects) {
            glm::mat4 model = glm::mat4(1.0f);
            if (cameraMode == FIGURE_ROTATION && editSceneMode && selectedObjectIndex == -1) {
               glm::vec3 center = ComputeSceneCenter();
               model = glm::translate(model, center);
               model = glm::rotate(model, glm::radians(objectRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
               model = glm::rotate(model, glm::radians(objectRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
               model = glm::rotate(model, glm::radians(objectRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
               model = glm::translate(model, -center);
            }
            else if (cameraMode == QUATERNION && editSceneMode && selectedObjectIndex == -1) {
               glm::vec3 center = ComputeSceneCenter();
               model = glm::translate(model, center);
               model *= glm::mat4_cast(objectOrientation);
               model = glm::translate(model, -center);
            }
            model = glm::translate(model, obj.position);
            model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
            model = glm::scale(model, obj.scale);

            std::vector<glm::vec3> faceNormalLines;
            std::vector<glm::vec3> vertexNormalLines;

            for (auto& mesh : obj.model.meshes) {
               for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                  glm::vec3 v0 = mesh.vertices[mesh.indices[i]].Position;
                  glm::vec3 v1 = mesh.vertices[mesh.indices[i + 1]].Position;
                  glm::vec3 v2 = mesh.vertices[mesh.indices[i + 2]].Position;
                  glm::vec3 center = (v0 + v1 + v2) / 3.0f;
                  glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                  glm::vec4 transformedCenter = model * glm::vec4(center, 1.0f);
                  glm::vec4 transformedNormal = model * glm::vec4(normal, 0.0f);
                  faceNormalLines.push_back(glm::vec3(transformedCenter) / transformedCenter.w);
                  faceNormalLines.push_back(glm::vec3(transformedCenter + transformedNormal * 0.8f) / transformedCenter.w);

                  for (int j = 0; j < 3; ++j) {
                     glm::vec3 pos = mesh.vertices[mesh.indices[i + j]].Position;
                     glm::vec3 norm = mesh.vertices[mesh.indices[i + j]].Normal;
                     glm::vec4 transformedPos = model * glm::vec4(pos, 1.0f);
                     glm::vec4 transformedNorm = model * glm::vec4(norm, 0.0f);
                     vertexNormalLines.push_back(glm::vec3(transformedPos) / transformedPos.w);
                     vertexNormalLines.push_back(glm::vec3(transformedPos + transformedNorm * 0.5f) / transformedPos.w);
                  }
               }
            }

            allFaceNormalLines.insert(allFaceNormalLines.end(), faceNormalLines.begin(), faceNormalLines.end());
            allVertexNormalLines.insert(allVertexNormalLines.end(), vertexNormalLines.begin(), vertexNormalLines.end());
         }

         if (!allFaceNormalLines.empty()) {
            glBindVertexArray(faceNormalsVAO);
            glBindBuffer(GL_ARRAY_BUFFER, faceNormalsVBO);
            glBufferData(GL_ARRAY_BUFFER, allFaceNormalLines.size() * sizeof(glm::vec3), allFaceNormalLines.data(), GL_STREAM_DRAW);
         }

         if (!allVertexNormalLines.empty()) {
            glBindVertexArray(vertexNormalsVAO);
            glBindBuffer(GL_ARRAY_BUFFER, vertexNormalsVBO);
            glBufferData(GL_ARRAY_BUFFER, allVertexNormalLines.size() * sizeof(glm::vec3), allVertexNormalLines.data(), GL_STREAM_DRAW);
         }

         if (displayMode == FACE_NORMALS) {
            shaderNormalFace.use();
            shaderNormalFace.setMat4("projection", projection);
            shaderNormalFace.setMat4("view", view);
            shaderNormalFace.setMat4("model", glm::mat4(1.0f));
            glBindVertexArray(faceNormalsVAO);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(allFaceNormalLines.size()));
            glBindVertexArray(0);
         }

         if (displayMode == VERTEX_NORMALS) {
            shaderNormalVertex.use();
            shaderNormalVertex.setMat4("projection", projection);
            shaderNormalVertex.setMat4("view", view);
            shaderNormalVertex.setMat4("model", glm::mat4(1.0f));
            glBindVertexArray(vertexNormalsVAO);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(allVertexNormalLines.size()));
            glBindVertexArray(0);
         }
      }

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      glfwSwapBuffers(window);
      glfwPollEvents();
   }

   glDeleteFramebuffers(1, &depthMapFBO);
   glDeleteTextures(1, &depthMap);
   glDeleteVertexArrays(1, &sphereVAO);
   glDeleteBuffers(1, &sphereVBO);
   glDeleteBuffers(1, &sphereEBO);
   glDeleteVertexArrays(1, &faceNormalsVAO);
   glDeleteBuffers(1, &faceNormalsVBO);
   glDeleteVertexArrays(1, &vertexNormalsVAO);
   glDeleteBuffers(1, &vertexNormalsVBO);
   if (rayTracingTexture) {
      glDeleteTextures(1, &rayTracingTexture);
   }

   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();

   glfwDestroyWindow(window);
   glfwTerminate();
   return 0;
}

void processInput(GLFWwindow* window)
{
   if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

   if (ImGui::GetIO().WantCaptureKeyboard) return;

   if (editSceneMode && selectedObjectIndex == -1) {
      if (cameraMode == STRAFE || cameraMode == BLENDER) {
         if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
         if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
         if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
         if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
         if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            camera.ProcessKeyboard(UP, deltaTime);
         if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            camera.ProcessKeyboard(DOWN, deltaTime);
      }
      if (cameraMode == FIGURE_ROTATION) {
         float rotationSpeed = 50.0f * deltaTime;
         if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
            objectRotation.x += (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? -rotationSpeed : rotationSpeed);
         if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS)
            objectRotation.y += (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? -rotationSpeed : rotationSpeed);
         if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
            objectRotation.z += (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? -rotationSpeed : rotationSpeed);
      }
   }

   if (selectedObjectIndex >= 0) {
      SceneObject& obj = sceneObjects[selectedObjectIndex];
      float moveSpeed = 2.5f * deltaTime;
      float rotateSpeed = 50.0f * deltaTime;

      if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
         obj.position.y += moveSpeed;
      if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
         obj.position.y -= moveSpeed;
      if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
         obj.position.x -= moveSpeed;
      if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
         obj.position.x += moveSpeed;
      if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS)
         obj.position.z -= moveSpeed;
      if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS)
         obj.position.z += moveSpeed;

      if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
         obj.rotation.x += rotateSpeed;
      if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS)
         obj.rotation.y += rotateSpeed;
      if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
         obj.rotation.z += rotateSpeed;
   }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
   glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
   if (ImGui::GetIO().WantCaptureMouse) return;

   if (firstMouse)
   {
      lastX = xpos;
      lastY = ypos;
      firstMouse = false;
   }

   float xoffset = xpos - lastX;
   float yoffset = lastY - ypos;
   lastX = xpos;
   lastY = ypos;

   if (editSceneMode && selectedObjectIndex == -1) {
      if (cameraMode == BLENDER) {
         if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
               camera.ProcessMousePan(xoffset, yoffset);
            }
            else {
               camera.ProcessMouseOrbit(xoffset, yoffset);
            }
         }
      }
      else if (cameraMode == QUATERNION) {
         if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            float sensitivity = 0.002f;
            glm::quat rotX = glm::angleAxis(yoffset * sensitivity, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::quat rotY = glm::angleAxis(-xoffset * sensitivity, glm::vec3(0.0f, 1.0f, 0.0f));
            objectOrientation = glm::normalize(rotX * rotY * objectOrientation);
         }
      }
      camera.Target = ComputeSceneCenter();
   }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
   if (ImGui::GetIO().WantCaptureMouse) return;
   if (editSceneMode) {
      if (cameraMode == BLENDER || cameraMode == FIGURE_ROTATION) {
         camera.ProcessMouseScroll(yoffset);
      }
   }
   else if (selectedObjectIndex >= 0) {
      SceneObject& obj = sceneObjects[selectedObjectIndex];
      float scaleDelta = 0.1f * yoffset;
      obj.scale += glm::vec3(scaleDelta);
      obj.scale = glm::max(obj.scale, glm::vec3(0.01f));
   }
}