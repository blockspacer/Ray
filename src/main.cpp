// Implementation of CUDA simpleCUDA2GL sample - based on Cuda Samples 9.0
// Dependencies: GLFW, GLEW

#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif

// OpenGL
#include <GL/glew.h> // Take care: GLEW should be included before GLFW
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
// CUDA
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include "libs/helper_cuda.h"
#include "libs/helper_cuda_gl.h"
// C++ libs
#include <string>
#include <filesystem>
#include "shader_tools/GLSLProgram.h"
#include "shader_tools/GLSLShader.h"
#include "gl_tools.h"
#include "glfw_tools.h"


#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


#include <iostream>
#include <chrono>
#include <ctime>

#include "sharedStructs.h"


using namespace std;

// GLFW
GLFWwindow* window;


int num_texels = WIDTH * HEIGHT;
int num_values = num_texels * 4;
int size_tex_data = sizeof(GLuint) * num_values;


int size_light_data = sizeof(GLuint) * LIGHT_BUFFER_WIDTH * LIGHT_BUFFER_WIDTH * LIGHT_BUFFER_THICKNESS;


// camera
double currYaw = 270;
double currPitch = 0;
glm::vec3 currFront = glm::vec3(0, 0, -1);

inputStruct input;

// OpenGL
GLuint VBO, VAO, EBO;
GLSLShader drawtex_f; // GLSL fragment shader
GLSLShader drawtex_v; // GLSL fragment shader
GLSLProgram shdrawtex; // GLSLS program for textured draw

// Cuda <-> OpenGl interop resources
void* cuda_dev_render_buffer; // stores initial
void* cuda_dev_render_buffer_2; // stores final output
void* cuda_ping_buffer; // used for storing intermediate effects

void* cuda_light_buffer; // result of the light pass
void* cuda_light_buffer_2; // result of the light pass


void* cuda_custom_objects_buffer; 
void* cuda_mesh_buffer; 

void** gridMeshes;
void** gridObjects;
void* gridMeshesSizes;
void* gridObjectsSizes;
void* outOfBoundsMeshes;
void* outOfBoundsObjects;


struct cudaGraphicsResource* cuda_tex_resource;
GLuint opengl_tex_cuda;  // OpenGL Texture for cuda result


extern "C" void
// Forward declaration of CUDA render
launch_cudaRender(dim3 grid, dim3 block, int sbytes, inputPointers pointers, int imgw, int imgh, float currTime, inputStruct input);
extern "C" void
launch_cudaLight(dim3 grid, dim3 block, int sbytes, inputPointers pointers, int imgw, int imgh, float currTime, inputStruct input);
extern "C" void
launch_cudaClear(dim3 grid, dim3 block, int sbytes, int imgw, unsigned int* buffer);
extern "C" void
launch_cudaBloomSample(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, PostProcessPointers pointers);
extern "C" void
launch_cudaBloomOutput(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, PostProcessPointers pointers);
extern "C" void
launch_cudaBlur(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, int currRatio, PostProcessPointers pointers);
extern "C" void
launch_cudaBlur2(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, bool isHorizontal, int currRatio, PostProcessPointers pointers, int dataInterval);
extern "C" void
launch_cudaBlur2SingleChannel(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, bool isHorizontal, int currRatio, PostProcessPointers pointers, int dataInterval);
extern "C" void
launch_cudaDownSampleToHalfRes(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, int currRatio, PostProcessPointers pointers);
extern "C" void
launch_cudaUpSampleToDoubleRes(dim3 grid, dim3 block, int sbytes, int imgw, int imgh, int currRatio, PostProcessPointers pointers);

size_t size_elements_data;

size_t size_meshes_data;
unsigned int num_meshes;

static const char* glsl_drawtex_vertshader_src =
"#version 330 core\n"
"layout (location = 0) in vec3 position;\n"
"layout (location = 1) in vec2 texCoord;\n"
"\n"
"out vec2 ourTexCoord;\n"
"\n"
"void main()\n"
"{\n"
"	gl_Position = vec4(position, 1.0f);\n"
"	ourTexCoord = texCoord;\n"
"}\n";

static const char* glsl_drawtex_fragshader_src =
"#version 330 core\n"
"uniform usampler2D tex;\n"
"in vec2 ourTexCoord;\n"
"out vec4 color;\n"
"void main()\n"
"{\n"
"   	vec4 c = texture(tex, ourTexCoord);\n"
"   	color = c / 255.0;\n"
"}\n";

// QUAD GEOMETRY
GLfloat vertices[] = {
	// Positions             // Texture Coords
	1.0f, 1.0f, 0.5f,1.0f, 1.0f,  // Top Right
	1.0f, -1.0f, 0.5f, 1.0f, 0.0f,  // Bottom Right
	-1.0f, -1.0f, 0.5f, 0.0f, 0.0f,  // Bottom Left
	-1.0f, 1.0f, 0.5f,  0.0f, 1.0f // Top Left 
};
// you can also put positions, colors and coordinates in seperate VBO's
GLuint indices[] = {  
	0, 1, 3,  
	1, 2, 3  
};

// Create 2D OpenGL texture in gl_tex and bind it to CUDA in cuda_tex
void createGLTextureForCUDA(GLuint* gl_tex, cudaGraphicsResource** cuda_tex, unsigned int size_x, unsigned int size_y)
{
	// create an OpenGL texture
	glGenTextures(1, gl_tex); // generate 1 texture
	glBindTexture(GL_TEXTURE_2D, *gl_tex); // set it as current target
	// set basic texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // clamp s coordinate
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // clamp t coordinate
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// Specify 2D texture
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI_EXT, size_x, size_y, 0, GL_RGB_INTEGER_EXT, GL_UNSIGNED_BYTE, NULL);
	// Register this texture with CUDA
	checkCudaErrors(cudaGraphicsGLRegisterImage(cuda_tex, *gl_tex, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard));
	SDK_CHECK_ERROR_GL();
}

void initGLBuffers()
{
	// create texture that will receive the result of cuda kernel
	createGLTextureForCUDA(&opengl_tex_cuda, &cuda_tex_resource, WIDTH, HEIGHT);
	// create shader program
	drawtex_v = GLSLShader("Textured draw vertex shader", glsl_drawtex_vertshader_src, GL_VERTEX_SHADER);
	drawtex_f = GLSLShader("Textured draw fragment shader", glsl_drawtex_fragshader_src, GL_FRAGMENT_SHADER);
	shdrawtex = GLSLProgram(&drawtex_v, &drawtex_f);
	shdrawtex.compile();
	SDK_CHECK_ERROR_GL();
}

float WPressed = 0.0;
float SPressed = 0.0;
float DPressed = 0.0;
float APressed = 0.0;
float QPressed = 0.0;
float EPressed = 0.0;

bool isMovingObject = false;
int selectedIndex;

float Pressed1 = 0.0;
float Pressed2 = 0.0;
float Pressed3 = 0.0;
float Pressed4 = 0.0;
float Pressed5 = 0.0;
float Pressed6 = 0.0;

bool blurEnabled = false;

#define PRESSED_RELEASED_MACRO(inKey, variable) if (key == GLFW_KEY_##inKey) { \
if (action == GLFW_PRESS){ \
variable = 1; \
} \
else if (action == GLFW_RELEASE) { \
	variable = 0; \
} \
}

#define PRESSED_ONLY_MACRO(inKey, variable, val) if (key == GLFW_KEY_##inKey) { \
if (action == GLFW_PRESS){ \
variable = val; \
} \
}

// Keyboard
void keyboardfunc(GLFWwindow* window, int key, int scancode, int action, int mods) {
	PRESSED_RELEASED_MACRO(W, WPressed);
	PRESSED_RELEASED_MACRO(S, SPressed);
	PRESSED_RELEASED_MACRO(D, DPressed);
	PRESSED_RELEASED_MACRO(A, APressed);
	PRESSED_RELEASED_MACRO(Q, QPressed);
	PRESSED_RELEASED_MACRO(E, EPressed);

	PRESSED_ONLY_MACRO(SPACE, isMovingObject, !isMovingObject);
	PRESSED_ONLY_MACRO(P, blurEnabled, !blurEnabled);
	PRESSED_ONLY_MACRO(0, selectedIndex, 0);
	PRESSED_ONLY_MACRO(1, selectedIndex, 1);
	PRESSED_ONLY_MACRO(2, selectedIndex, 2);
	PRESSED_ONLY_MACRO(3, selectedIndex, 3);
	PRESSED_ONLY_MACRO(4, selectedIndex, 4);
	PRESSED_ONLY_MACRO(5, selectedIndex, 5);
	PRESSED_ONLY_MACRO(6, selectedIndex, 6);
	PRESSED_ONLY_MACRO(7, selectedIndex, 7);
}

bool firstMouse = true;
double mouseDeltaX;
double mouseDeltaY;

double lastX;
double lastY;

void mouseFunc(GLFWwindow* window, double xpos, double ypos) {

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

	float sensitivity = 0.05;
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	currYaw += xoffset;
	currPitch += yoffset;

	currPitch = currPitch > 89.0f ? 89.0f : currPitch;
	currPitch = currPitch < -89.0f ? -89.0f : currPitch;


	currFront.x = (float)cos(glm::radians(currYaw)) * cos(glm::radians(currPitch));
	currFront.y = (float)sin(glm::radians(currPitch));
	currFront.z = (float)sin(glm::radians(currYaw)) * cos(glm::radians(currPitch));
	currFront = glm::normalize(currFront);

}

bool initGL() {
	glewExperimental = GL_TRUE; // need this to enforce core profile
	GLenum err = glewInit();
	glGetError(); // parse first error
	if (err != GLEW_OK) {// Problem: glewInit failed, something is seriously wrong.
		printf("glewInit failed: %s /n", glewGetErrorString(err));
		exit(1);
	}
	glViewport(0, 0, WIDTH, HEIGHT); // viewport for x,y to normalized device coordinates transformation
	SDK_CHECK_ERROR_GL();
	return true;
}


std::vector<triangleMesh> importModel(std::string path, float scale, float3 offset, bool switchYZ = false) {

	std::vector<triangleMesh> toReturn;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenSmoothNormals);
	if (!scene) {
		cout << "ya entered an invalid path to mesh fuccboi\n";
		return toReturn;
	}

	for (int i = 0; i < scene->mNumMeshes; i++) {
		auto mesh = scene->mMeshes[i];
		// indices

		triangleMesh current;

		unsigned int totalIndices = 0;

		for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
			auto face = mesh->mFaces[i];
			totalIndices += face.mNumIndices + (face.mNumIndices > 3 ? (face.mNumIndices - 3)*2 : 0);
		}

		current.numIndices = totalIndices;
		current.numVertices = mesh->mNumVertices;
		current.indices = (unsigned int*) malloc(current.numIndices * sizeof(unsigned int));

		unsigned int currIndexPos = 0;
		for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
			auto face = mesh->mFaces[i];
			for (int j = 2; j < face.mNumIndices; j++) { // fan triangulate if not triangles
				current.indices[currIndexPos] = face.mIndices[0];
				current.indices[currIndexPos+1] = face.mIndices[j-1];
				current.indices[currIndexPos+2] = face.mIndices[j];
				currIndexPos += 3;
			}
		}

		// vertices & normals
		current.vertices = (float3*)malloc(current.numVertices * sizeof(float3));
		current.normals = (float3*)malloc(current.numVertices * sizeof(float3));
		cout << "num vertices: " << mesh->mNumVertices << endl;
		cout << "num faces: " << current.numIndices/3 << endl;
		for (unsigned int i = 0; i < current.numVertices; i++) {
			float y = mesh->mVertices[i].y * scale + offset.y;
			float z = mesh->mVertices[i].z * scale + offset.z;
			if (switchYZ) {
				std::swap(y, z);
			}
			current.vertices[i] = make_float3(mesh->mVertices[i].x* scale + offset.x, y, z);
			//cout << "Adding vertex: " << toReturn.vertices[i].x << " " << toReturn.vertices[i].y << " " << toReturn.vertices[i].z << "\n";
			if (mesh->HasNormals())
				current.normals[i] = make_float3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
		}

		toReturn.push_back(current);
	}

	return toReturn;
}

#define MAX(a,b) a > b ? a : b
#define MIN(a,b) a < b ? a : b
#define MOST(type, v1,v2) make_float3( type (v1.x, v2.x), type (v1.y,v2.y), type (v1.z, v2.z));

triangleMesh prepareMeshForCuda(const triangleMesh &myMesh) {
	triangleMesh myMeshOnCuda = myMesh;

	float BIG_VALUE = 1000000;

	float3 max = make_float3(-BIG_VALUE,-BIG_VALUE,-BIG_VALUE);
	float3 min = make_float3(BIG_VALUE, BIG_VALUE, BIG_VALUE);
	for (int i = 0; i < myMesh.numVertices; i++) {
		max = MOST(MAX, max, myMesh.vertices[i]);
		min = MOST(MIN, min, myMesh.vertices[i]);
	}
	

	// acceleration structure
	float3 center = 0.5 * (max + min);
	myMeshOnCuda.bbMax = max;
	myMeshOnCuda.bbMin = min;

	float rad = 0;
	for (int i = 0; i < myMesh.numVertices; i++) {
		rad = MAX(rad, length(myMesh.vertices[i] - center));
	}
	myMeshOnCuda.rad = rad;


	unsigned int gridSize = GRID_SIZE* GRID_SIZE * GRID_SIZE * sizeof(unsigned int*);
	unsigned int gridSizesSize = GRID_SIZE * GRID_SIZE * GRID_SIZE * sizeof(unsigned int);

	unsigned int** grid = (unsigned int**)malloc(gridSize);
	unsigned int* gridSizes = (unsigned int*)malloc(gridSizesSize);


	float3 gridDist = (1.0 / GRID_SIZE) * (max - min);
	myMeshOnCuda.gridBoxDimensions = gridDist;

	for (int x = 0; x < GRID_SIZE; x++) {
		for (int y = 0; y < GRID_SIZE; y++) {
			for (int z = 0; z < GRID_SIZE; z++) {
				std::vector<unsigned int> trianglesToAddToBlock;
				float3 currMin = make_float3(x,y,z)*gridDist + min;
				float3 currMax = make_float3(x+1,y+1,z+1)*gridDist + min;
				float3 currCenter = 0.5 * (currMin + currMax);

				for (int i = 0; i < myMesh.numIndices; i += 3) {
					float3 v0 = myMesh.vertices[myMesh.indices[i]];
					float3 v1 = myMesh.vertices[myMesh.indices[i+1]];
					float3 v2 = myMesh.vertices[myMesh.indices[i+2]];

					float tMin;
					float tMax;
					// we intersect if we're either inside the slab or one edge crosses it
					bool intersecting = (std::fabs(currCenter.x - v0.x) < gridDist.x * 0.5) && (std::fabs(currCenter.y - v0.y) < gridDist.y * 0.5) && (std::fabs(currCenter.z - v0.z) < gridDist.z * 0.5);
					intersecting |= intersectBox(v0, normalize(v1 - v0), currMin, currMax, tMin, tMax) && tMin > 0 && tMin < length(v1 - v0);
					intersecting |= intersectBox(v1, normalize(v2 - v1), currMin, currMax, tMin, tMax) && tMin > 0 && tMin < length(v2 - v1);
					intersecting |= intersectBox(v2, normalize(v0 - v2), currMin, currMax, tMin, tMax) && tMin > 0 && tMin < length(v0 - v2);

					if (intersecting) {
						trianglesToAddToBlock.push_back(i);
					}
				}

				//cout << "x " << x << " y " << y << " z " << z << " collisions: " << trianglesToAddToBlock.size() << endl;
				gridSizes[GRID_POS(x,y,z)] = trianglesToAddToBlock.size();
				grid[GRID_POS(x, y, z)] = (unsigned int*)malloc(trianglesToAddToBlock.size() * sizeof(unsigned int));

				for (int i = 0; i < trianglesToAddToBlock.size(); i++) {
					grid[GRID_POS(x, y, z)][i] = trianglesToAddToBlock[i]; // add collisions to grid
				}
			}
		}
	}

	//checkCudaErrors(cudaMalloc(mesh_pointer, sizeof(triangleMesh)));

	unsigned int indicesSize = myMesh.numIndices * sizeof(unsigned int);
	unsigned int verticesSize = myMesh.numVertices * sizeof(float3);


	if (myMesh.numIndices > 0) {
		// allocate cuda space
		checkCudaErrors(cudaMalloc(&myMeshOnCuda.indices, indicesSize));
		checkCudaErrors(cudaMalloc(&myMeshOnCuda.vertices, verticesSize));
		checkCudaErrors(cudaMalloc(&myMeshOnCuda.normals, verticesSize));
		// this shit is getting convoluted man
		// gotta allocate for each list in grid separately, then feed the correct pointers to the correct positions

		unsigned int** CudaGridPointer = (unsigned int**)malloc(gridSize);

		for (int i = 0; i < GRID_SIZE * GRID_SIZE * GRID_SIZE; i++) {
			checkCudaErrors(cudaMalloc(&(CudaGridPointer[i]), gridSizes[i]*sizeof(unsigned int)));
			checkCudaErrors(cudaMemcpy(CudaGridPointer[i], grid[i], gridSizes[i] * sizeof(unsigned int), cudaMemcpyHostToDevice));

		}
		checkCudaErrors(cudaMalloc(&myMeshOnCuda.grid, gridSize));
		checkCudaErrors(cudaMalloc(&myMeshOnCuda.gridSizes, gridSizesSize));

		// copy data to cuda buffers
		checkCudaErrors(cudaMemcpy(myMeshOnCuda.indices, myMesh.indices, indicesSize, cudaMemcpyHostToDevice));
		checkCudaErrors(cudaMemcpy(myMeshOnCuda.vertices, myMesh.vertices, verticesSize, cudaMemcpyHostToDevice));
		checkCudaErrors(cudaMemcpy(myMeshOnCuda.normals, myMesh.normals, verticesSize, cudaMemcpyHostToDevice));
		checkCudaErrors(cudaMemcpy(myMeshOnCuda.grid, CudaGridPointer, gridSize, cudaMemcpyHostToDevice));
		checkCudaErrors(cudaMemcpy(myMeshOnCuda.gridSizes, gridSizes, gridSizesSize, cudaMemcpyHostToDevice));

		free(CudaGridPointer);

	}

	for (int i = 0; i < GRID_SIZE * GRID_SIZE * GRID_SIZE; i++) {
		free(grid[i]);
	}
	free(grid);
	free(gridSizes);

	return myMeshOnCuda;
}
#define NUM_ELEMENTS 8
objectInfo objects[NUM_ELEMENTS];


//void setupGlobalGrid(objectInfo objects[NUM_ELEMENTS], std::vector<triangleMesh> importedMeshes) {
//
//	unsigned int gridSize = GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * sizeof(unsigned int*);
//	unsigned int gridSizesSize = GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * sizeof(unsigned int);
//
//	float3 gridDist = (1.0 / GLOBAL_GRID_SIZE)* (GLOBAL_GRID_MAX - GLOBAL_GRID_MIN);
//
//
//	unsigned int** objectGrid = (unsigned int**)malloc(gridSize);
//	unsigned int* objectSizes = (unsigned int*)malloc(gridSizesSize);
//	unsigned int** meshesGrid = (unsigned int**)malloc(gridSize);
//	unsigned int* meshesSizes = (unsigned int*)malloc(gridSizesSize);
//
//	std::vector<unsigned int> objectBlocks;
//	for (int x = 0; x < GLOBAL_GRID_SIZE; x++) {
//		for (int y = 0; y < GLOBAL_GRID_SIZE; y++) {
//			for (int z = 0; z < GLOBAL_GRID_SIZE; z++) {
//				std::vector<unsigned int> objectsToAddToBlock;
//				std::vector<unsigned int> meshesToAddToBlock;
//
//				float3 boxMin = GLOBAL_GRID_MIN + GLOBAL_GRID_DIMENSIONS * make_float3(x, y, z);
//				float3 boxMax = GLOBAL_GRID_MIN + GLOBAL_GRID_DIMENSIONS * make_float3(x+1, y+1, z+1);
//				float3 center = (boxMin + boxMax) * 0.5;
//
//				for (int i = 0; i < NUM_ELEMENTS; i++) {
//					objectInfo object = objects[i];
//					switch (object.s) {
//						case water:
//						case plane: {
//							bool foundPositive = false;
//							bool foundNegative = false;
//							for (int x2 = 0; x2 < 2; x2++) {
//								for (int y2 = 0; y2 < 2; y2++) {
//									for (int z2 = 0; z2 < 2; z2++) {
//										float3 vertP = GLOBAL_GRID_MIN + GLOBAL_GRID_DIMENSIONS * make_float3(x2, y2, z2);
//										float3 diffV = vertP - object.shapeData.pos;
//										float dotRes = dot(diffV, object.shapeData.normal);
//										foundPositive |= dotRes > 0;
//										foundNegative |= dotRes < 0;
//
//									}
//								}
//							}
//
//							if (foundNegative && foundPositive)
//								objectsToAddToBlock.push_back(i);
//
//							break;
//						}
//						case sphere: {
//
//						}
//					}
//				}
//				for (int i = 0; i < importedMeshes.size(); i++) {
//					triangleMesh mesh = importedMeshes[i];
//					float3 meshMin = mesh.bbMin;
//					float3 meshMax = mesh.bbMin;
//					#define overlaps1D(var) (meshMax . var >= boxMin .var && boxMax. var >= meshMin. var)
//
//					if (overlaps1D(x) && overlaps1D(y) && overlaps1D(z))
//						objectsToAddToBlock.push_back(i);
//
//				}
//
//				// add objects to grid
//				objectSizes[GLOBAL_GRID_POS(x, y, z)] = objectsToAddToBlock.size();
//				objectGrid[GLOBAL_GRID_POS(x, y, z)] = (unsigned int*)malloc(objectsToAddToBlock.size() * sizeof(unsigned int));
//
//				for (int i = 0; i < objectsToAddToBlock.size(); i++) {
//					objectGrid[GLOBAL_GRID_POS(x, y, z)][i] = objectsToAddToBlock[i]; // add collisions to grid
//				}
//
//				// add meshes to grid
//				meshesSizes[GLOBAL_GRID_POS(x, y, z)] = meshesToAddToBlock.size();
//				meshesGrid[GLOBAL_GRID_POS(x, y, z)] = (unsigned int*)malloc(meshesToAddToBlock.size() * sizeof(unsigned int));
//
//				for (int i = 0; i < meshesToAddToBlock.size(); i++) {
//					meshesGrid[GLOBAL_GRID_POS(x, y, z)][i] = meshesToAddToBlock[i]; // add collisions to grid
//				}
//			}
//		}
//	}
//
//	//unsigned int indicesSize = myMesh.numIndices * sizeof(unsigned int);
//	//unsigned int verticesSize = myMesh.numVertices * sizeof(float3);
//
//	//unsigned int 
//
//	//void** gridMeshes;
//	//void** gridObjects;
//	//void* gridMeshesSizes;
//	//void* gridObjectsSizes;
//
//
//	checkCudaErrors(cudaMalloc(&gridMeshes, gridSize));
//	checkCudaErrors(cudaMalloc(&gridObjects, gridSize));
//
//	checkCudaErrors(cudaMalloc(&gridMeshesSizes, gridSizesSize));
//	checkCudaErrors(cudaMalloc(&gridObjectsSizes, gridSizesSize));
//
//	unsigned int** CudaMeshGridPointer = (unsigned int**)malloc(gridSize);
//	unsigned int** CudaObjectGridPointer = (unsigned int**)malloc(gridSize);
//	for (int i = 0; i < GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE; i++) {
//			checkCudaErrors(cudaMalloc(&(CudaMeshGridPointer[i]), meshesSizes[i] * sizeof(unsigned int)));
//			checkCudaErrors(cudaMemcpy(CudaMeshGridPointer[i], meshesGrid[i], meshesSizes[i] * sizeof(unsigned int), cudaMemcpyHostToDevice));
//
//			checkCudaErrors(cudaMalloc(&(CudaObjectGridPointer[i]), objectSizes[i] * sizeof(unsigned int)));
//			checkCudaErrors(cudaMemcpy(CudaObjectGridPointer[i], objectGrid[i], objectSizes[i] * sizeof(unsigned int), cudaMemcpyHostToDevice));
//	}
//
//	checkCudaErrors(cudaMalloc(&gridMeshes, gridSize));
//	checkCudaErrors(cudaMalloc(&gridObjects, gridSize));
//	checkCudaErrors(cudaMalloc(&gridMeshesSizes, gridSizesSize));
//	checkCudaErrors(cudaMalloc(&gridObjectsSizes, gridSizesSize));
//
//
//	checkCudaErrors(cudaMemcpy(gridMeshes, CudaMeshGridPointer, gridSize, cudaMemcpyHostToDevice));
//	checkCudaErrors(cudaMemcpy(gridMeshesSizes, gridMeshesSizes, gridSizesSize, cudaMemcpyHostToDevice));
//
//	checkCudaErrors(cudaMemcpy(gridObjects, CudaObjectGridPointer, gridSize, cudaMemcpyHostToDevice));
//	checkCudaErrors(cudaMemcpy(gridObjectsSizes, gridObjectsSizes, gridSizesSize, cudaMemcpyHostToDevice));
//
//
//	for (int i = 0; i < GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE * GLOBAL_GRID_SIZE; i++) {
//		free(meshesGrid[i]);
//		free(objectGrid[i]);
//	}
//	free(meshesGrid);
//	free(objectGrid);
//	free(meshesSizes);
//	free(objectSizes);
//
//	//return myMeshOnCuda;
//
//
//}

void createObjects() {
	size_elements_data = sizeof(objectInfo) * NUM_ELEMENTS;
	checkCudaErrors(cudaMalloc(&cuda_custom_objects_buffer, size_elements_data)); // Allocate CUDA memory for objects

	shapeInfo s1 = make_shapeInfo(make_float3(0, -99, 0), make_float3(0, 0, 0), 100);
	shapeInfo s2 = make_shapeInfo(make_float3(0, -15, 50), make_float3(0, 0, 0), 14); // diffuse
	shapeInfo s3 = make_shapeInfo(make_float3(0, 4, -80), make_float3(0, 0, 0), 8); // reflective
	shapeInfo s4 = make_shapeInfo(make_float3(-50, 4, 0), make_float3(0, 0, 0), 12); // refractive
	shapeInfo s5 = make_shapeInfo(make_float3(-50, 10, -50), make_float3(0, 0, 0), 8); // refractive 2
	shapeInfo s6 = make_shapeInfo(make_float3(-30, 10, 10), make_float3(0, 0, 0), 8); // refractive 3
	shapeInfo p1 = make_shapeInfo(make_float3(0, -5, 0), make_float3(0, 1, 0), 0); // water top
	shapeInfo p3 = make_shapeInfo(make_float3(0, -60.0, 0), make_float3(0, 1, 0), 0); // sand bottom

	shapeInfo sun = make_shapeInfo(make_float3(0, 2000, 0), make_float3(1, 0, 0), 200);


	objects[0] = make_objectInfo(sphere, s3, 1.0, make_float3(0., 1, 1), 0, 0, 0, 0.0); // reflective
	objects[1] = make_objectInfo(sphere, s6, 0.0, make_float3(0.0, 0.0, 0.1), 1.0, 1.6, 0.0, 0.0); // refractive 3
	objects[2] = make_objectInfo(water, p1, 0.0, WATER_COLOR, 1.0, 1.33, WATER_DENSITY, 1.0); // water top
	objects[3] = make_objectInfo(plane, p3, 0., make_float3(76.0 / 255.0, 70.0 / 255, 50.0 / 255), 0, 0, 0.0, 0); // sand ocean floor
	objects[4] = make_objectInfo(sphere, s1, 0, make_float3(76.0 / 255.0, 70.0 / 255, 50.0 / 255), 0, 0, 0, 2000); // island
	objects[5] = make_objectInfo(sphere, sun, 0.0, 5000 * make_float3(1, 1, 1), 0.0, 1.33, 0.0, 0.0); // sun
	objects[6] = make_objectInfo(sphere, s2, 0.0, make_float3(0.5, 0.5, 0), 0.0, 1.4, 0, 1.0); // yellow boi
	objects[7] = make_objectInfo(sphere, s5, 0.0, make_float3(0.0, 0.0, 0.1), 1.0, 1.3, 0.0, 0.0); // refractive 2
}

void initCUDABuffers()
{
	// set up vertex data parameters


	// We don't want to use cudaMallocManaged here - since we definitely want
	cudaError_t stat;
	//size_t myStackSize = 8192;
	//stat = cudaDeviceSetLimit(cudaLimitStackSize, myStackSize);
	checkCudaErrors(cudaMalloc(&cuda_dev_render_buffer, size_tex_data)); // Allocate CUDA memory for color output
	checkCudaErrors(cudaMalloc(&cuda_dev_render_buffer_2, size_tex_data)); // Allocate CUDA memory for color output 2
	checkCudaErrors(cudaMalloc(&cuda_ping_buffer, size_tex_data)); // Allocate CUDA memory for ping buffer
	checkCudaErrors(cudaMalloc(&cuda_light_buffer, size_light_data)); // Allocate CUDA memory for pong buffer
	checkCudaErrors(cudaMalloc(&cuda_light_buffer_2, size_light_data)); // Allocate CUDA memory for pong buffer


	// add objects
	createObjects();


	// add meshes
	std::vector<triangleMesh> importedMeshes;
	std::vector<rayHitInfo> infos;

	auto tree = importModel("../../meshes/Palm_Tree.obj", 7.0, make_float3(0, 0, 0), false);
	importedMeshes.insert(std::end(importedMeshes), std::begin(tree), std::end(tree));
	infos.push_back(make_rayHitInfo( 0.0, 0.0, 0.0, 0.0, 0.5*make_float3(133.0/255.0,87.0/255.0,35.0/255.0), 1000)); // bark
	infos.push_back(make_rayHitInfo( 0.0, 0.0, 1.0, 0.0, 0.5*make_float3(111.0/255.0,153.0/255,64.0/255), 500)); // palm leaves
	infos.push_back(make_rayHitInfo( 0.0, 0.0, 1.0, 0.0, 0.7*make_float3(111.0 / 255.0, 153.0 / 255, 64.0 / 255), 0)); // palm leaves 2

	std::vector<triangleMesh> rockMesh = importModel("../../meshes/rock.obj", 0.05, make_float3(80.0, -80, 50.0), false);
	importedMeshes.insert(std::end(importedMeshes), std::begin(rockMesh), std::end(rockMesh));
	infos.push_back(make_rayHitInfo( 0.0, 0.0, 1.5, 0.0, 0.3*make_float3(215./255,198./255,171./255), 2000 )); //rock

	std::vector<triangleMesh> bunnyMesh = importModel("../../meshes/bun2.ply", 500, make_float3(0.0, -70, -250.0), false);
	importedMeshes.insert(std::end(importedMeshes), std::begin(bunnyMesh), std::end(bunnyMesh));
	infos.push_back(make_rayHitInfo(0.0, 0.0, 0.0, 0.0, make_float3(20,0,0.0), 0)); //le bun

	size_meshes_data = sizeof(triangleMesh) * importedMeshes.size();
	num_meshes = importedMeshes.size();

	assert(infos.size() == importedMeshes.size());

	triangleMesh *meshesOnCuda = (triangleMesh*) malloc(size_meshes_data);

	for (int i = 0; i < importedMeshes.size(); i++) {
		triangleMesh curr = importedMeshes[i];
		curr.rayInfo = infos[i];
		meshesOnCuda[i] = prepareMeshForCuda(curr);
	}
	// setup the global grid
	//setupGlobalGrid(objects, importedMeshes);



	checkCudaErrors(cudaMalloc(&cuda_mesh_buffer, size_meshes_data));
	checkCudaErrors(cudaMemcpy(cuda_mesh_buffer, meshesOnCuda, size_meshes_data, cudaMemcpyHostToDevice));


}

bool initGLFW() {
	if (!glfwInit()) exit(EXIT_FAILURE);
	// These hints switch the OpenGL profile to core
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow(WIDTH, WIDTH, "Raytracer", NULL, NULL);
	if (!window) { glfwTerminate(); exit(EXIT_FAILURE); }
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0);
	glfwSetKeyCallback(window, keyboardfunc);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glfwSetCursorPosCallback(window, mouseFunc);
	return true;
}



#define X_ROTATE_SCALE 0.1
#define Y_ROTATE_SCALE 0.1
#define MOVE_SPEED 50

void updateObjects(std::chrono::duration<double> deltaTime) {

	if (isMovingObject) {
		float3 movementVec = make_float3(WPressed - SPressed, QPressed - EPressed, APressed - DPressed);
		objects[selectedIndex].shapeData.pos = objects[selectedIndex].shapeData.pos + movementVec * deltaTime.count() * MOVE_SPEED;
	}


	cudaMemcpy(cuda_custom_objects_buffer, objects, size_elements_data, cudaMemcpyHostToDevice);
}






void generateCUDAImage(std::chrono::duration<double> totalTime, std::chrono::duration<double> deltaTime)
{
	// calculate grid size
	dim3 block(8, 8, 1);
	dim3 grid(WIDTH / block.x, HEIGHT / block.y, 1); // 2D grid, every thread will compute a pixel


	glm::vec3 frontV = currFront;
	glm::vec3 currP(input.currPosX, input.currPosY, input.currPosZ);
	glm::vec3 upV(0, 1, 0);
	glm::vec3 rightV = glm::normalize(glm::cross(frontV, upV));
	glm::vec3 actualUpV = glm::normalize(glm::cross(frontV, rightV));
	glm::vec3 lessUp = actualUpV;

	if (!isMovingObject) {

		frontV *= MOVE_SPEED * (WPressed - SPressed) * deltaTime.count();
		rightV *= MOVE_SPEED * (DPressed - APressed) * deltaTime.count();
		lessUp *= MOVE_SPEED * (EPressed - QPressed) * deltaTime.count();
		currP += frontV;
		currP += rightV;
		currP += lessUp;
	}

	input.currPosX = currP.x;
	input.currPosY = currP.y;
	input.currPosZ = currP.z;

	input.forwardX = currFront.x;
	input.forwardY = currFront.y;
	input.forwardZ = currFront.z;

	input.upX = actualUpV.x;
	input.upY = actualUpV.y;
	input.upZ = actualUpV.z;


	updateObjects(deltaTime);

	rayHitInfo prevMedium;
	prevMedium.color = input.currPosY < -5 ? WATER_COLOR : AIR_COLOR;
	prevMedium.insideColorDensity = input.currPosY < -5 ? WATER_DENSITY : AIR_DENSITY;
	
	input.beginMedium = hitInfo{ prevMedium, true, float3(), float3() };


	sceneInfo info{ (unsigned int**)gridMeshes,(unsigned int**)gridObjects, (unsigned int*)gridMeshesSizes, (unsigned int*)gridObjectsSizes,totalTime.count(), (objectInfo*)cuda_custom_objects_buffer, NUM_ELEMENTS, (triangleMesh*)cuda_mesh_buffer, num_meshes };
	inputPointers pointers{ (unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_light_buffer, info };
	//inputPointers pointers2{ (unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_light_buffer_2, info };

	// draw light
	dim3 lightGridDraw(LIGHT_BUFFER_WIDTH / block.x, LIGHT_BUFFER_WIDTH / block.y, 1); // 2D grid, every thread will compute a pixel
	dim3 lightGrid(LIGHT_BUFFER_WIDTH / block.x, LIGHT_BUFFER_WIDTH / block.y, LIGHT_BUFFER_THICKNESS / block.z); // 2D grid, every thread will compute a pixel
	launch_cudaClear(lightGrid, block, 0, LIGHT_BUFFER_WIDTH, (unsigned int*)cuda_light_buffer);
	launch_cudaLight(lightGridDraw, block, 0, pointers, LIGHT_BUFFER_WIDTH, LIGHT_BUFFER_WIDTH, totalTime.count(), input);


	if (blurEnabled) {
		launch_cudaBlur2SingleChannel(lightGrid, block, 0, LIGHT_BUFFER_WIDTH, LIGHT_BUFFER_WIDTH, true, 1, PostProcessPointers{ (unsigned int*)cuda_light_buffer, (unsigned int*)cuda_light_buffer, (unsigned int*)cuda_light_buffer_2, (unsigned int*)cuda_dev_render_buffer_2, }, 1); // launch with 0 additional shared memory allocated
		launch_cudaBlur2SingleChannel(lightGrid, block, 0, LIGHT_BUFFER_WIDTH, LIGHT_BUFFER_WIDTH, false, 1, PostProcessPointers{ (unsigned int*)cuda_light_buffer_2, (unsigned int*)cuda_light_buffer_2, (unsigned int*)cuda_light_buffer, (unsigned int*)cuda_dev_render_buffer_2, }, 1); // launch with 0 additional shared memory allocated
	}


	// main render
	launch_cudaRender(grid, block, 0, pointers, WIDTH, HEIGHT, totalTime.count(), input); // launch with 0 additional shared memory allocated


	//// bloom passes
	launch_cudaBloomSample(grid, block, 0, WIDTH, HEIGHT, PostProcessPointers{(unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_ping_buffer, (unsigned int*)cuda_ping_buffer, (unsigned int*)cuda_dev_render_buffer_2, }); // launch with 0 additional shared memory allocated
	launch_cudaBlur2(grid, block, 0, WIDTH, HEIGHT, true, 1,PostProcessPointers{(unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_ping_buffer, (unsigned int*)cuda_dev_render_buffer_2, (unsigned int*)cuda_dev_render_buffer_2, }, 4); // launch with 0 additional shared memory allocated
	launch_cudaBlur2(grid, block, 0, WIDTH, HEIGHT, false, 1,PostProcessPointers{(unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_dev_render_buffer_2, (unsigned int*)cuda_ping_buffer, (unsigned int*)cuda_dev_render_buffer_2, }, 4); // launch with 0 additional shared memory allocated
	launch_cudaBloomOutput(grid, block, 0, WIDTH, HEIGHT, PostProcessPointers{(unsigned int*)cuda_dev_render_buffer, (unsigned int*)cuda_ping_buffer, (unsigned int*)cuda_dev_render_buffer_2, (unsigned int*)cuda_dev_render_buffer_2, }); // launch with 0 additional shared memory allocated

	cudaArray* texture_ptr;
	/*checkCudaErrors(*/cudaGraphicsMapResources(1, &cuda_tex_resource, 0);//);
	checkCudaErrors(cudaGraphicsSubResourceGetMappedArray(&texture_ptr, cuda_tex_resource, 0, 0));



	checkCudaErrors(cudaMemcpyToArray(texture_ptr, 0, 0, cuda_dev_render_buffer_2, size_tex_data, cudaMemcpyDeviceToDevice));
	checkCudaErrors(cudaGraphicsUnmapResources(1, &cuda_tex_resource, 0));
	cudaDeviceSynchronize();



}

//void display(std::chrono::duration<double> duration, std::chrono::duration<double> deltaTime) {
//	generateCUDAImage(duration, deltaTime);
//	glfwPollEvents();
//	// Clear the color buffer
//	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
//	glClear(GL_COLOR_BUFFER_BIT);
//
//	glActiveTexture(GL_TEXTURE0);
//	glBindTexture(GL_TEXTURE_2D, opengl_tex_cuda);
//
//	shdrawtex.use(); // we gonna use this compiled GLSL program
//	glUniform1i(glGetUniformLocation(shdrawtex.program, "tex"), 0);
//
//	glBindVertexArray(VAO); // binding VAO automatically binds EBO
//		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
//	glBindVertexArray(0); // unbind VAO
//
//	SDK_CHECK_ERROR_GL();
//	
//	// Swap the screen buffers
//	glfwSwapBuffers(window);
//}


void display(std::chrono::duration<double> duration, std::chrono::duration<double> deltaTime) {
	glClear(GL_COLOR_BUFFER_BIT);
	generateCUDAImage(duration, deltaTime);
	glfwPollEvents();
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	// Swap the screen buffers
	glfwSwapBuffers(window);
}

int main(int argc, char* argv[]) {
	initGLFW();
	initGL();

	printGLFWInfo(window);
	printGlewInfo();
	printGLInfo();

	findCudaGLDevice(argc, (const char**)argv);
	initGLBuffers();
	initCUDABuffers();

	// Generate buffers
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	// Buffer setup
	// Bind the Vertex Array Object first, then bind and set vertex buffer(s) and attribute pointer(s).
	glBindVertexArray(VAO); // all next calls wil use this VAO (descriptor for VBO)

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// Position attribute (3 floats)
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	// Texture attribute (2 floats)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	// Note that this is allowed, the call to glVertexAttribPointer registered VBO as the currently bound 
	// vertex buffer object so afterwards we can safely unbind
	glBindVertexArray(0);

	// Unbind VAO (it's always a good thing to unbind any buffer/array to prevent strange bugs), remember: do NOT unbind the EBO, keep it bound to this VAO
	// A VAO stores the glBindBuffer calls when the target is GL_ELEMENT_ARRAY_BUFFER. 
	// This also means it stores its unbind calls so make sure you don't unbind the element array buffer before unbinding your VAO, otherwise it doesn't have an EBO configured.
	auto firstTime = std::chrono::system_clock::now();
	auto lastTime = firstTime;
	auto lastMeasureTime = firstTime;
	int frameNum = 0;
	// Some computation here


	glBindVertexArray(VAO); // binding VAO automatically binds EBO
	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, opengl_tex_cuda);

	shdrawtex.use(); // we gonna use this compiled GLSL program
	glUniform1i(glGetUniformLocation(shdrawtex.program, "tex"), 0);
	SDK_CHECK_ERROR_GL();


	while (!glfwWindowShouldClose(window))
	{
		auto currTime = std::chrono::system_clock::now();
		auto totalTime = currTime - firstTime;


		display(totalTime, currTime - lastTime);
		std::chrono::duration<double> elapsed_seconds = currTime - lastMeasureTime;
		frameNum++;
		if (elapsed_seconds.count() >= 1.0) {
			// show fps every  second

			std::cout << "fps: " << (frameNum / elapsed_seconds.count()) << "\n";
			frameNum = 0;
			lastMeasureTime = currTime;
		}
		lastTime = currTime;
	}
	glBindVertexArray(0); // unbind VAO


	glfwDestroyWindow(window);
	glfwTerminate();
	exit(EXIT_SUCCESS);
}