#pragma once

#include "cuda_runtime.h"


struct sphereInfo {
	float3 pos;
	float rad;
	float rad2;
};

struct shapeInfo {
	float3 pos;
	float3 normal;
	float rad;
};

inline __device__ shapeInfo make_shapeInfo(float3 pos, float3 normal, float rad) {
	shapeInfo info;
	info.pos = pos;
	info.normal = normal;
	info.rad = rad;
	return info;
}

inline __device__ sphereInfo make_sphereInfo(float3 pos, float rad) {
	sphereInfo s;
	s.pos = pos;
	s.rad = rad;
	s.rad2 = rad * rad;
	return s;
}


struct planeInfo {
	float3 point;
	float3 normal;
};

inline __device__ planeInfo make_planeInfo(float3 point, float3 normal) {
	planeInfo p;
	p.point = point;
	p.normal = normal;
	return p;
}

enum shape { sphere, plane };

struct rayHitInfo {
	float reflectivity;
	float refractivity;
	float refractiveIndex;
	float insideColorDensity;
	float3 color;
};

struct objectInfo {
	shape s;
	shapeInfo shapeData;
	rayHitInfo rayInfo;

};

inline __device__ objectInfo make_objectInfo(shape s, shapeInfo shapeData, float reflectivity, float3 color, float refractivity, float refractiveIndex, float insideColorDensity) {
	objectInfo o;
	o.s = s;
	o.shapeData = shapeData;
	o.rayInfo.reflectivity = reflectivity;
	o.rayInfo.color = color;
	o.rayInfo.refractivity = refractivity;
	o.rayInfo.refractiveIndex = refractiveIndex;
	o.rayInfo.insideColorDensity = insideColorDensity;
	return o;
}

struct triangleMesh {
	float3* vertices; //float3
	float3* normals; //float3
	unsigned int* indices; // unsigned int
	int numIndices = 0;
	int numVertices = 0;

	rayHitInfo rayInfo;
};

struct sceneInfo {

	// objects are pure mathematical objects, while meshes are triangle meshes
	const float currTime;
	const objectInfo* objects;
	int numObjects;

	const triangleMesh* meshes;
	int numMeshes;
};


struct inputPointers {
	unsigned int* g_odata; // texture position

	sceneInfo scene;
	//objectInfo* objects;
	//int numObjects;

	//triangleMesh* meshes;
	//int numMeshes;

};


struct inputStruct {
	float currPosX;
	float currPosY;
	float currPosZ;

	float forwardX;
	float forwardY;
	float forwardZ;

	float upX;
	float upY;
	float upZ;
};
