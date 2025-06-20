#ifndef MODEL_H
#define MODEL_H

#include <glad.h> 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "stb_image.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh.h"
#include "shader.h"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
using namespace std;

unsigned int TextureFromFile(const char* path, const string& directory, bool gamma = false);

class Model
{
public:
   // ������ ������ 
   vector<Texture> textures_loaded; // (�����������) ��������� ��� ����������� ��������, ����� ���������, ��� ��� �� ��������� ����� ������ ����
   vector<Mesh> meshes;
   string directory;
   bool gammaCorrection;
   bool useOriginalTextures = true;

   Model() : gammaCorrection(false), useOriginalTextures(true) {}
   // ����������� � �������� ��������� ���������� ���� � 3D-������
   Model(string const& path, bool gamma = false) : gammaCorrection(gamma)
   {
      loadModel(path);
   }

   // ������������ ������, � ������ � ��� � ����
   void Draw(const Shader& shader, int reservedTextureUnit = -1) const {
      for (unsigned int i = 0; i < meshes.size(); i++) {
         meshes[i].Draw(shader, useOriginalTextures, reservedTextureUnit);
      }
   }

   void setUseOriginalTextures(bool use) {
      useOriginalTextures = use;
   }

   glm::vec3 GetBoundingSphere() const {
      glm::vec3 minBounds(std::numeric_limits<float>::max());
      glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

      // ������� ������� ������
      for (const auto& mesh : meshes) {
         for (const auto& vertex : mesh.vertices) {
            minBounds = glm::min(minBounds, vertex.Position);
            maxBounds = glm::max(maxBounds, vertex.Position);
         }
      }

      // ��������� ������ ��� �������� ���������
      glm::vec3 diagonal = maxBounds - minBounds;
      float radius = glm::length(diagonal) * 0.5f;

      // ���������� ������, ��� x,y,z - ����� �����, � ����� ������� - ������
      return (maxBounds + minBounds) * 0.5f + glm::vec3(radius);
   }
private:
   // ��������� ������ � ������� Assimp � ��������� ���������� ���� � ������� meshes
   void loadModel(string const& path)
   {
      // ������ ����� � ������� Assimp
      Assimp::Importer importer;
      const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals);

      // �������� �� ������
      if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // ���� �� 0
      {
         cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
         return;
      }

      // ��������� ���� � �����
      directory = path.substr(0, path.find_last_of('/'));

      // ����������� ��������� ��������� ���� Assimp
      processNode(scene->mRootNode, scene);
   }

   // ����������� ��������� ����. ������������ ������ ��������� ���, ������������� � ����, � ��������� ���� ������� ��� ����� �������� ����� (���� ������ ������ �������)
   void processNode(aiNode* node, const aiScene* scene)
   {
      // ������������ ������ ��� �������� ����
      for (unsigned int i = 0; i < node->mNumMeshes; i++)
      {
         // ���� �������� ������ ������� �������� � �����.
         // ����� �� �������� ��� ������; ���� - ��� ���� ������ ����������� ������
         aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
         meshes.push_back(processMesh(mesh, scene));
      }
      // ����� ����, ��� �� ���������� ��� ���� (���� ������� �������), �� �������� ���������� ������������ ������ �� �������� �����
      for (unsigned int i = 0; i < node->mNumChildren; i++)
      {
         processNode(node->mChildren[i], scene);
      }

   }

   Mesh processMesh(aiMesh* mesh, const aiScene* scene)
   {
      // ������ ��� ����������
      vector<Vertex> vertices;
      vector<unsigned int> indices;
      vector<Texture> textures;

      // ���� �� ���� �������� ����
      for (unsigned int i = 0; i < mesh->mNumVertices; i++)
      {
         Vertex vertex;
         glm::vec3 vector; // �� ��������� ������������� ������, �.�. Assimp ���������� ���� ����������� ��������� �����, ������� �� ������������� �������� � ��� glm::vec3, ������� ������� �� �������� ������ � ���� ������������� ������ ���� glm::vec3

         // ����������
         vector.x = mesh->mVertices[i].x;
         vector.y = mesh->mVertices[i].y;
         vector.z = mesh->mVertices[i].z;
         vertex.Position = vector;

         // �������
         vector.x = mesh->mNormals[i].x;
         vector.y = mesh->mNormals[i].y;
         vector.z = mesh->mNormals[i].z;
         vertex.Normal = vector;

         // ���������� ����������
         if (mesh->mTextureCoords[0]) // ���� ��� �������� ���������� ����������
         {
            glm::vec2 vec;

            // ������� ����� ��������� �� 8 ��������� ���������� ���������. �� ������������, ��� �� �� ����� ������������ ������,
            // � ������� ������� ����� ��������� ��������� ���������� ���������, ������� �� ������ ����� ������ ����� (0)
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TexCoords = vec;
         }
         else
            vertex.TexCoords = glm::vec2(0.0f, 0.0f);

         // ����������� ������
         if (mesh->mTangents) {
            vector.x = mesh->mTangents[i].x;
            vector.y = mesh->mTangents[i].y;
            vector.z = mesh->mTangents[i].z;
            vertex.Tangent = vector;
         } else {
            vertex.Tangent = glm::vec3(0.0f, 0.0f, 0.0f); // Default or fallback value
         }

         // ������ ���������
         if (mesh->mBitangents) {
            vector.x = mesh->mBitangents[i].x;
            vector.y = mesh->mBitangents[i].y;
            vector.z = mesh->mBitangents[i].z;
            vertex.Bitangent = vector;
         } else {
            vertex.Bitangent = glm::vec3(0.0f, 0.0f, 0.0f); // Default or fallback value
         }
         vertices.push_back(vertex);
      }
      // ������ ���������� �� ������ ����� ���� (����� - ��� ����������� ����) � ��������� ��������������� ������� ������
      for (unsigned int i = 0; i < mesh->mNumFaces; i++)
      {
         aiFace face = mesh->mFaces[i];

         // �������� ��� ������� ������ � ��������� �� � ������� indices
         for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
      }

      // ������������ ���������
      aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

      // �� ������ ���������� �� ������ ��������� � ��������. ������ ��������� �������� ����� ���������� 'texture_diffuseN',
      // ��� N - ���������� ����� �� 1 �� MAX_SAMPLER_NUMBER. 
      // ���� ����� ��������� � � ������ ���������:
      // ��������� - texture_diffuseN
      // ��������� - texture_specularN
      // ������� - texture_normalN

      // 1. ��������� �����
      vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
      textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

      // 2. ����� ���������
      vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
      textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

      // 3. ����� ��������
      std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
      textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

      // 4. ����� �����
      std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
      textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

      // ���������� ���-������, ��������� �� ������ ���������� ������
      return Mesh(vertices, indices, textures);
   }

   // ��������� ��� �������� ���������� ��������� ���� � �������� ��������, ���� ��� ��� �� ���� ���������.
   // ����������� ���������� ������������ � ���� ��������� Texture
   vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, string typeName)
   {
      vector<Texture> textures;
      for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
      {
         aiString str;
         mat->GetTexture(type, i, &str);

         // ���������, �� ���� �� �������� ��������� �����, � ���� - ��, �� ���������� �������� ����� �������� � ��������� � ��������� ��������
         bool skip = false;
         for (unsigned int j = 0; j < textures_loaded.size(); j++)
         {
            if (std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)
            {
               textures.push_back(textures_loaded[j]);
               skip = true; // �������� � ��� �� ����� � ����� ��� ���������, ��������� � ��������� (�����������)
               break;
            }
         }
         if (!skip)
         {   // ���� �������� ��� �� ���� ���������, �� ��������� �
            Texture texture;
            texture.id = TextureFromFile(str.C_Str(), this->directory);
            texture.type = typeName;
            texture.path = str.C_Str();
            textures.push_back(texture);
            textures_loaded.push_back(texture); // ��������� �������� � ������� � ��� ������������ ����������, ��� ����� ����������, ��� � ��� �� �������� ��� ������������� ��������� �������
         }
      }
      return textures;
   }
};


unsigned int TextureFromFile(const char* path, const string& directory, bool gamma)
{
   string filename = string(path);
   filename = directory + '/' + filename;

   unsigned int textureID;
   glGenTextures(1, &textureID);

   int width, height, nrComponents;
   //stbi_set_flip_vertically_on_load(true);
   unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
   if (data)
   {
      GLenum format;
      if (nrComponents == 1)
         format = GL_RED;
      else if (nrComponents == 3)
         format = GL_RGB;
      else if (nrComponents == 4)
         format = GL_RGBA;

      glBindTexture(GL_TEXTURE_2D, textureID);
      glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
      glGenerateMipmap(GL_TEXTURE_2D);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      stbi_image_free(data);
   }
   else
   {
      std::cout << "Texture failed to load at path: " << path << std::endl;
      stbi_image_free(data);
   }

   return textureID;
}
#endif