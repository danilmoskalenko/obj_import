#define GLM_ENABLE_EXPERIMENTAL
#include <glad.h>
#include <glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "shader.h"
#include "camera.h"
#include "model.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <filesystem>
#include <iostream>
#include "stb_image.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// Window dimensions
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Shadow map dimensions
const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
const int SHADOW_TEX_UNIT = 10;

// Camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Display and lighting modes
enum DisplayMode { NORMAL, FACE_NORMALS, VERTEX_NORMALS };
DisplayMode displayMode = NORMAL;

enum LightingMode { NONE, NO_LIGHTING, AMBIENT, SPOTLIGHT, DIRECTIONAL, POINT };
LightingMode lightingMode = NONE;

// Camera modes
enum CameraMode { FIGURE_ROTATION, STRAFE, QUATERNION, BLENDER };
CameraMode cameraMode = BLENDER;

// Object rotation for FIGURE_ROTATION mode
glm::vec3 objectRotation(0.0f);
glm::quat objectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

struct SceneObject {
   std::string name;
   Model model;
   glm::vec3 position = glm::vec3(0.0f);
   glm::vec3 rotation = glm::vec3(0.0f);
   glm::vec3 scale = glm::vec3(1.0f);
   bool collision = true;
};

std::vector<SceneObject> sceneObjects;
int selectedObjectIndex = -1;

bool editSceneMode = true;

glm::vec3 ComputeSceneCenter() {
   if (sceneObjects.empty()) return glm::vec3(0.0f);
   glm::vec3 sum(0.0f);
   int validObjects = 0;
   for (const SceneObject& obj : sceneObjects) {
      if (!std::isnan(obj.position.x) && !std::isnan(obj.position.y) && !std::isnan(obj.position.z)) {
         sum += obj.position;
         validObjects++;
      }
   }
   return validObjects > 0 ? sum / static_cast<float>(validObjects) : glm::vec3(0.0f);
}

float cubeVertices[] = {
    -0.1f, -0.1f, -0.1f,
     0.1f, -0.1f, -0.1f,
     0.1f,  0.1f, -0.1f,
    -0.1f,  0.1f, -0.1f,
    -0.1f, -0.1f,  0.1f,
     0.1f, -0.1f,  0.1f,
     0.1f,  0.1f,  0.1f,
    -0.1f,  0.1f,  0.1f
};

unsigned int cubeIndices[] = {
    0, 1, 2, 2, 3, 0,
    1, 5, 6, 6, 2, 1,
    5, 4, 7, 7, 6, 5,
    4, 0, 3, 3, 7, 4,
    3, 2, 6, 6, 7, 3,
    4, 5, 1, 1, 0, 4
};

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

   static std::string modelPath = "resources/objects/Crate/Crate1.obj";
   Model ourModel(modelPath);

   std::vector<glm::vec3> faceNormalLines;
   std::vector<glm::vec3> vertexNormalLines;

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

   GLuint lightVAO, lightVBO, lightEBO;
   glGenVertexArrays(1, &lightVAO);
   glGenBuffers(1, &lightVBO);
   glGenBuffers(1, &lightEBO);

   glBindVertexArray(lightVAO);
   glBindBuffer(GL_ARRAY_BUFFER, lightVBO);
   glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lightEBO);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
   glBindVertexArray(0);

   glm::vec3 lightPos(1.2f, 1.0f, 2.0f);
   glm::vec3 lightDir(0.0f, -1.0f, 0.0f);
   glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
   glm::vec3 backgroundColor(0.05f, 0.05f, 0.05f);
   glm::vec3 objectColor(1.0f, 1.0f, 1.0f);
   float ambientStrength = 0.2f;
   float diffuseStrength = 1.0f;
   float specularStrength = 0.2f;
   float shininess = 8.0f;
   float spotCutOff = glm::cos(glm::radians(12.5f));
   float spotOuterCutOff = glm::cos(glm::radians(17.5f));
   float constant = 1.0f;
   float linear = 0.09f;
   float quadratic = 0.032f;

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

      ImGui::Checkbox("Edit Whole Scene", &editSceneMode);

      if (editSceneMode) selectedObjectIndex = -1;

      if (editSceneMode) {
         const char* cameraItems[] = { "Scene Rotation", "Strafe", "Quaternion", "Blender" };
         static int cameraCurrent = static_cast<int>(cameraMode);
         if (ImGui::Combo("Camera Mode", &cameraCurrent, cameraItems, IM_ARRAYSIZE(cameraItems))) {
            cameraMode = static_cast<CameraMode>(cameraCurrent);
            if (cameraMode != FIGURE_ROTATION) {
               objectRotation = glm::vec3(0.0f);
            }
            if (cameraMode != QUATERNION) {
               objectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
         }
         if (ImGui::Button("Reset Camera")) {
            camera.Reset();
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
      ImGui::SliderFloat("Ambient Strength", &ambientStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Diffuse Strength", &diffuseStrength, 0.0f, 2.0f, "%.2f");
      ImGui::SliderFloat("Specular Strength", &specularStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Shininess", &shininess, 2.0f, 64.0f, "%.1f");

      ImGui::Text("Color Settings");
      ImGui::Separator();
      ImGui::ColorEdit3("Object Color", (float*)&objectColor);
      ImGui::ColorEdit3("Background Color", (float*)&backgroundColor);

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
         ImGui::Checkbox(("##collision" + std::to_string(i)).c_str(), &obj.collision);

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
      glPolygonOffset(1.1f, 4.0f);
      glClear(GL_DEPTH_BUFFER_BIT);
      glm::vec3 sceneCenter = ComputeSceneCenter();
      glm::mat4 lightProjection, lightView, lightSpaceMatrix;

      if (lightingMode == DIRECTIONAL) {
         lightProjection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 10.0f);
         lightView = glm::lookAt(sceneCenter - glm::normalize(lightDir) * 5.0f, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
      }
      else {
         lightProjection = glm::perspective(glm::radians(90.0f), (float)SHADOW_WIDTH / (float)SHADOW_HEIGHT, 0.1f, 10.0f);
         lightView = glm::lookAt(sceneCenter + glm::vec3(0.0f, 2.0f, 0.0f), sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
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

      glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
      glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      ourShader.use();
      glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
      glm::mat4 view = camera.GetViewMatrix();
      ourShader.setMat4("projection", projection);
      ourShader.setMat4("view", view);
      ourShader.setInt("lightingMode", static_cast<int>(lightingMode));
      ourShader.setInt("normalMode", static_cast<int>(displayMode));
      ourShader.setVec3("viewPos", camera.Position);
      ourShader.setVec3("lightPos", sceneCenter + glm::vec3(0.0f, 2.0f, 0.0f));
      ourShader.setVec3("lightDir", glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f)));
      ourShader.setVec3("lightColor", lightColor);
      ourShader.setVec3("backgroundColor", backgroundColor);
      ourShader.setVec3("objectColor", objectColor);
      ourShader.setFloat("ambientStrength", ambientStrength);
      ourShader.setFloat("diffuseStrength", diffuseStrength);
      ourShader.setFloat("specularStrength", specularStrength);
      ourShader.setFloat("shininess", shininess);
      ourShader.setFloat("spotCutOff", spotCutOff);
      ourShader.setFloat("spotOuterCutOff", spotOuterCutOff);
      ourShader.setFloat("constant", constant);
      ourShader.setFloat("linear", linear);
      ourShader.setFloat("quadratic", quadratic);
      ourShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
      glActiveTexture(GL_TEXTURE0 + SHADOW_TEX_UNIT);
      glBindTexture(GL_TEXTURE_2D, depthMap);
      ourShader.setInt("shadowMap", SHADOW_TEX_UNIT);

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
         obj.model.Draw(ourShader, SHADOW_TEX_UNIT);
      }

      if (lightingMode == POINT || lightingMode == SPOTLIGHT || lightingMode == DIRECTIONAL) {
         lightShader.use();
         lightShader.setMat4("projection", projection);
         lightShader.setMat4("view", view);
         glm::mat4 lightModel = glm::mat4(1.0f);
         if (lightingMode == POINT || lightingMode == SPOTLIGHT) {
            lightModel = glm::translate(lightModel, sceneCenter + glm::vec3(0.0f, 2.0f, 0.0f));
         }
         else if (lightingMode == DIRECTIONAL) {
            lightModel = glm::translate(lightModel, sceneCenter - glm::normalize(lightDir) * 5.0f);
         }
         lightShader.setMat4("model", lightModel);
         lightShader.setVec3("lightColor", lightColor);
         glBindVertexArray(lightVAO);
         glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
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
   glDeleteVertexArrays(1, &lightVAO);
   glDeleteBuffers(1, &lightVBO);
   glDeleteBuffers(1, &lightEBO);
   glDeleteVertexArrays(1, &faceNormalsVAO);
   glDeleteBuffers(1, &faceNormalsVBO);
   glDeleteVertexArrays(1, &vertexNormalsVAO);
   glDeleteBuffers(1, &vertexNormalsVBO);

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