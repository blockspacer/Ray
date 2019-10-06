#pragma once
#include "rayHelpers.cu"
#include "perlin.h"


#define USING_SHADOWS
//#define USING_POINT_LIGHT
//#define SOFT_SHADOWS
#define STATIC_LIGHT_DIR make_float3(0.5,1,0.5)
#define LIGHT_POS make_float3(1000,2000,1000)

//#define AMBIENT_OCCLUSION

//#define VISUALIZE_BOUNDS


//#define USING_SHADOW_FALLOW
#define SHADOW_FALLOW_FACTOR 0.95
#define AIR_DENSITY 0.001
#define AIR_COLOR 1.0*make_float3(53.0/255, 81.0/255, 98.0/255);
cudaError_t cuda();
__global__ void kernel() {

}


__device__ float currentTime;
__device__ sceneInfo *scene;
//sceneInfo info;


__device__ bool intersectsSphere(const float3& origin, const float3& dir, const float3 pos, const float rad, float& t) {

	float t0, t1; // solutions for t if the ray intersects 

	float rad2 = powf(rad, 2);

	float3 L = pos - origin;
	float tca = dot(dir, L);
	//if (tca < 0) return false;
	float d2 = dot(L, L) - tca * tca;
	if (d2 > rad2) return false;
	float thc = sqrt(rad2 - d2);
	t0 = tca - thc;
	t1 = tca + thc;

	if (t0 > t1) {
		float temp = t0;
		t0 = t1;
		t1 = temp;
	}

	if (t0 < 0) {
		t0 = t1; // if t0 is negative, let's use t1 instead 
		if (t0 < 0) return false; // both t0 and t1 are negative 
	}
	t = t0;
	return true;
}

// plane normal, plane point, ray start, ray dir, point along line
__device__ bool intersectPlane(const shapeInfo& p, const float3& l0, const float3& l, float& t)
{
	// assuming vectors are all normalized
	float denom = dot(p.normal, l);
	if (denom < -1e-8) {
		float3 p0l0 = p.pos - l0;
		t = dot(p0l0, p.normal) / denom;
		return (t >= 0);
	}
	return false;
}

__device__ bool intersectWater(const shapeInfo& p, const float3& l0, const float3& l, float& t)
{
	// assuming vectors are all normalized
	float denom = dot(p.normal, l);
	if (denom < -1e-8) {
		float3 p0l0 = p.pos - l0;
		t = dot(p0l0, p.normal) / denom;
		return (t >= 0);
	}
	return false;
}

__device__ bool rayTriangleIntersect(
	float3 orig, float3 dir, float3 v0, const float3& v1, const float3& v2,
	float& t, float& u, float& v)
{
	// compute plane's normal
	float3 v0v1 = v1 - v0;
	float3 v0v2 = v2 - v0;

	//orig = orig - 2*
	//// no need to normalize
	float3 N = cross(v0v1, v0v2); // N 
	float denom = dot(N, N);


	//// Step 1: finding P

	// check if ray and plane are parallel ?
	float NdotRayDirection = dot(N, dir);
	if (fabs(NdotRayDirection) < 0.0001) // almost 0 
		return false; // they are parallel so they don't intersect ! 

	// compute d parameter using equation 2
	float d = dot(N, v0);

	// compute t (equation 3)
	t = (dot(N, orig) + d) / NdotRayDirection;
	// check if the triangle is in behind the ray
	if (t < 0) return false; // the triangle is behind 

	// compute the intersection point using equation 1
	float3 P = orig + t * dir;

	// Step 2: inside-outside test
	float3 C; // vector perpendicular to triangle's plane 

	// edge 0
	float3 edge0 = v1 - v0;
	float3 vp0 = P - v0;
	C = cross(edge0, vp0);
	if (dot(N, C) < 0) return false; // P is on the right side 

	// edge 1
	float3 edge1 = v2 - v1;
	float3 vp1 = P - v1;
	C = cross(edge1, vp1);
	if ((u = dot(N, C)) < 0)  return false; // P is on the right side 

	// edge 2
	float3 edge2 = v0 - v2;
	float3 vp2 = P - v2;
	C = cross(edge2, vp2);
	if ((v = dot(N, C)) < 0) return false; // P is on the right side; 

	u /= denom;
	v /= denom;

	return true; // this ray hits the triangle 
}




__device__ bool RayIntersectsTriangle(float3 rayOrigin,
	float3 rayVector,
	float3 vertex0, float3 vertex1, float3 vertex2,
	float& t, float& u, float& v)
{

	const float EPSILON = 0.001;
	float3 edge1, edge2, h, s, q;
	float a, f;
	edge1 = vertex1 - vertex0;
	edge2 = vertex2 - vertex0;
	h = cross(rayVector, edge2);
	a = dot(edge1, h);
	if (a > -EPSILON && a < EPSILON)
		return false;    // This ray is parallel to this triangle.
	f = 1.0 / a;
	s = rayOrigin - vertex0;
	u = f * dot(s, h);
	//if (u < 0.0 || u > 1.0)
	//	return false;
	q = cross(s, edge1);
	v = f * dot(rayVector, q);
	//if (v < 0.0 || u + v > 1.0)
	//	return false;
	// At this stage we can compute t to find out where the intersection point is on the line.
	t = f * dot(edge2, q);
	if (t > EPSILON && !((u < 0.0 || u > 1.0) || (v < 0.0 || u + v > 1.0))) // ray intersection
	{
		return true;
	}
	else // This means that there is a line intersection but not a ray intersection.
		return false;
}


__device__ void fresnel(const float3& I, const float3& N, const float& ior, float& kr)
{
	float cosi = clamp(-1, 1, dot(I, N));
	float etai = 1, etat = ior;
	if (cosi > 0) { float temp = etai; etai = etat; etat = temp; }
	// Compute sini using Snell's law
	float sint = etai / etat * sqrtf(max(0.f, 1 - cosi * cosi));
	// Total internal reflection
	if (sint >= 1) {
		kr = 1;
	}
	else {
		float cost = sqrtf(max(0.f, 1 - sint * sint));
		cosi = abs(cosi);
		float Rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
		float Rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
		kr = (Rs * Rs + Rp * Rp) / 2;
	}

}


__device__ float3 refract(const float3& I, const float3& N, const float& ior)
{
	float cosi = clamp(-1, 1, dot(I, N));
	float etai = 1, etat = ior;
	float3 n = N;
	if (cosi < 0) { cosi = -cosi; }
	else { float temp = etai; etai = etat; etat = temp; n = inverse(N); }
	float eta = etai / etat;
	float k = 1 - eta * eta * (1 - cosi * cosi);
	return eta * I + (eta * cosi - sqrtf(k)) * n;
}

__device__ float3 reflect(const float3& I, const float3& N)
{
	return I - 2 * dot(I, N) * N;
}

struct hitInfo {
	rayHitInfo info;
	bool hit = false;
	float3 pos;
	float3 normal;

};




__device__ float3 getWave(float3 pos, float xSpeed, float ySpeed, float xSize, float ySize) {

	return make_float3(1, 1, 1);

	//normal = normalize(info.normal + strength * make_float3(sinf(waveInput.x), 0, sinf(waveInput.z * 0.1)));

}


//float rayMarchUntilIntersection

//#define rayMarchUntilIntersection(FUN, orig, dir, startDist, numIterations) \
//float t = startDist; \
//for (int i = 0; i < numIterations; i++) { \
//	AA
//
//}

__device__ hitInfo getHit(const float3 currRayPos,const float3 currRayDir) {
	float closestDist = 1000000;
	float3 normal;
	hitInfo toReturn;
	toReturn.hit = false;


	// mathematical objects
	for (int i = 0; i < scene->numObjects; i++) {
		const objectInfo& curr = scene->objects[i];
		float currDist;


		shapeInfo info = curr.shapeData;
		switch (curr.s) {
		case water: {
			if (intersectPlane(info, currRayPos, currRayDir, currDist) && currDist < closestDist) {

				closestDist = currDist;
				toReturn.info = curr.rayInfo;
				float3 waveInput = (currRayPos + currDist * currRayDir)*0.5 + make_float3(3*currentTime + 10000, 10000,10000);
				float strength = 2000.;

				float d = 0.01;
				float h1 = perlin2d(waveInput.x - d, waveInput.z, 1, 3);
				float h2 = perlin2d(waveInput.x + d, waveInput.z, 1, 3);
				float h3 = perlin2d(waveInput.x, waveInput.z - d, 1, 3);
				float h4 = perlin2d(waveInput.x, waveInput.z + d, 1, 3);

				// derivatives
				float d1 = (h2 - h1) / 2*d; 
				float d2 = (h4 - h3) / 2*d;

				//float3 distortion = make_float3(perlin2d(waveInput.x+10000, 0.0, 1, 1)*2* - 1., 0, perlin2d(waveInput.z + 20000, waveInput.x + 30000, 1, 1)*2.-1.);
				float3 rightDir = make_float3(0, 0, 1);
				float3 otherDir1 = cross(rightDir, info.normal);
				float3 otherDir2 = cross(otherDir1, info.normal);

				//otherDir1 = dot(currRayDir, otherDir1) > 0.1 ? inverse(otherDir1) : otherDir1;
				//otherDir2 = dot(currRayDir, otherDir2) > 0.1 ? inverse(otherDir2) : otherDir2;

				float3 distortion = (otherDir1 * d1 +otherDir2 * d2);

				//distortion = dot(distortion, currRayDir) <= -0.0000001 ? distortion : inverse(distortion);

				//distortion = distortion * make_float3(currRayDir.x, currRayDir.y, currRayDir.z);

				normal = normalize(info.normal + strength*distortion);
				//normal = normalize(info.normal + strength*make_float3(sinf(waveInput.x+currentTime), 0, sinf(waveInput.z + currentTime)));
				//normal = normalize(info.normal + strength * make_float3(sinf(waveInput.x + currentTime), 0, 0));
				//normal = make_float3(currRayDir.x > 0 ? -abs(normal.x) : abs(normal.x), currRayDir.y > 0 ? -abs(normal.y) : abs(normal.y), currRayDir.z > 0 ? -abs(normal.z) : abs(normal.z));
				//normal = dot(currRayDir, normal) < 0. ? inverse(normal) + 2 * normal : normal;

				toReturn.hit = true;
			}

			break;
		}
		case plane: {
			if (intersectPlane(info, currRayPos, currRayDir, currDist) && currDist < closestDist) {
				closestDist = currDist;
				toReturn.info = curr.rayInfo;
				normal = info.normal;
				toReturn.hit = true;
			}

			break;
		}
		case sphere: {
			if (intersectsSphere(currRayPos, currRayDir, info.pos, info.rad, currDist) && currDist < closestDist) {
				closestDist = currDist;
				float3 nextPos = currRayPos + currDist * currRayDir;
				normal = normalize(nextPos - info.pos);
				toReturn.info = curr.rayInfo;
				toReturn.hit = true;

			}
			break;
		}
		}
	}


	// meshes
	for (int i = 0; i < scene->numMeshes; i++) {
		triangleMesh currMesh = scene->meshes[i];

		float tMin = 0;
		float tMax;

		float3 gridPos = (currRayPos - currMesh.bbMin) / currMesh.gridBoxDimensions;
		gridPos = make_float3(floor(gridPos.x), floor(gridPos.y), floor(gridPos.z));

		bool isAlreadyInside = max(gridPos.x, max(gridPos.y, gridPos.z)) < GRID_SIZE && min(gridPos.x, min(gridPos.y, gridPos.z)) >= 0;
		/*intersectsSphere(currRayPos, currRayDir, 0.5 * (currMesh.bbMin + currMesh.bbMax), currMesh.rad, tMin);*/
		if (isAlreadyInside || intersectsSphere(currRayPos, currRayDir, 0.5 * (currMesh.bbMin + currMesh.bbMax), currMesh.rad, tMin) && (intersectBox(currRayPos, currRayDir, currMesh.bbMin, currMesh.bbMax, tMin, tMax) && tMin < closestDist && tMin > 0)) {

			// engage the GRID
			float3 currPos = currRayPos + (tMin + 0.001)*currRayDir;
			gridPos = (currPos - currMesh.bbMin) / currMesh.gridBoxDimensions;

			int stepsBeforeQuit = 1000;
			while (--stepsBeforeQuit >= 0 && max(gridPos.x, max(gridPos.y, gridPos.z)) < GRID_SIZE && min(gridPos.x, min(gridPos.y, gridPos.z)) >= 0) {

				gridPos = make_float3(floor(gridPos.x), floor(gridPos.y), floor(gridPos.z));
				unsigned int gridPosLoc = GRID_POS(gridPos.x, gridPos.y, gridPos.z);

				float t;
				float u;
				float v;
				for (unsigned int j = 0; j < currMesh.gridSizes[gridPosLoc]; j++) {
					unsigned int iPos = currMesh.grid[gridPosLoc][j];
					bool hitTriangle = RayIntersectsTriangle(currRayPos, currRayDir, currMesh.vertices[currMesh.indices[iPos]], currMesh.vertices[currMesh.indices[iPos + 1]], currMesh.vertices[currMesh.indices[iPos + 2]], t, u, v);
					if (hitTriangle && t < closestDist) {
						closestDist = t;
						toReturn.info = currMesh.rayInfo;

						normal = (1 - v - u)* currMesh.normals[currMesh.indices[iPos]] + u * currMesh.normals[currMesh.indices[iPos + 1]] + v * currMesh.normals[currMesh.indices[iPos + 2]];
						toReturn.hit = true;
						stepsBeforeQuit = 1;
					}
				}

				float3 distFromCorner = currPos - gridPos * currMesh.gridBoxDimensions - currMesh.bbMin;
				float3 distFromOtherCorner = currMesh.gridBoxDimensions - distFromCorner;
				float remainingToHitX = max(-distFromCorner.x / currRayDir.x, distFromOtherCorner.x / currRayDir.x);
				float remainingToHitY = max(-distFromCorner.y / currRayDir.y, distFromOtherCorner.y / currRayDir.y);
				float remainingToHitZ = max(-distFromCorner.z / currRayDir.z, distFromOtherCorner.z / currRayDir.z);
				float minDist = min(remainingToHitX, min(remainingToHitY, remainingToHitZ)) + 0.01;

				currPos = currPos + minDist * currRayDir;
				gridPos = (currPos - currMesh.bbMin) / currMesh.gridBoxDimensions;
			}
		}

	}


	toReturn.normal = normal;
	toReturn.pos = currRayPos + closestDist * currRayDir;
	return toReturn;
}



__device__ float getShadowTerm(const float3 originalPos, const float3 normal) {

#ifndef USING_SHADOWS
	return 1.0;
#endif
	float toReturn;
#ifndef SOFT_SHADOWS
#ifdef USING_POINT_LIGHT
	float3 toLightVec = normalize(LIGHT_POS - originalPos);
#else 
	float3 toLightVec = STATIC_LIGHT_DIR;
#endif // USING_POINT_LIGHT
	hitInfo hit = getHit(originalPos + 0.001 *toLightVec, toLightVec);
#ifdef USING_POINT_LIGHT
	if (!hit.hit || length(hit.pos - originalPos) > length(originalPos - LIGHT_POS)) {
		toReturn = 1.;
	}
	else {
		toReturn = 0.0;
	}
#else 
	if (!hit.hit || length(hit.pos - LIGHT_POS) < 2001.0f) {
		toReturn = 1.;
	}
	else {
		#ifdef USING_SHADOW_FALLOW
			toReturn = min(1.f, 1.-powf(SHADOW_FALLOW_FACTOR,length(hit.pos - originalPos)));
		#else
		toReturn = 0.0;
		#endif
	}
#endif // USING_POINT_LIGHT

#else // if soft shadows

	int totalHits = 0;
	for (int x = 0; x < 2; x++) {
		for (int y = 0; y < 2; y++) {
			float3 toLightVec = normalize(STATIC_LIGHT_DIR + 0.05*make_float3(x,0,y));
			hitInfo hit = getHit(originalPos + 0.001 * toLightVec, toLightVec);
			if (!hit.hit || length(hit.pos - originalPos) > length(originalPos - LIGHT_POS))
				totalHits++;
		}
	}

	toReturn = ((float)totalHits / 4);
#endif  // soft shadows

#ifdef AMBIENT_OCCLUSION
	float3 rightDir = make_float3(0, 0, 1);
	float3 otherDir1 = cross(rightDir, normal);
	float3 otherDir2 = cross(otherDir1, normal);

	float maxLen = 0.2;
	float minDist = maxLen;
	for (int x = -5; x < 5; x++) {
		for (int y = -5; y < 5; y++) {
			float3 currDir = normal + otherDir1 * x + otherDir2 * y;
			hitInfo hit = getHit(originalPos + 0.001 * currDir, currDir);
			minDist = min(minDist, length(hit.pos - originalPos));
			//if (!hit.hit || length(hit.pos - originalPos) > maxLen)
			//	totalHits+;
		}
	}

	toReturn *= (minDist / maxLen);

#endif

	return toReturn;

}


__device__ float3 trace(const float3 currRayPos, const float3 currRayDir, int remainingDepth, const hitInfo prevHitToAddDepthFrom) {
	if (remainingDepth <= 0) {
		return AIR_COLOR;
	}


	hitInfo hit = getHit(currRayPos, currRayDir);

	if (!hit.hit) {
		return AIR_COLOR;
	}
	else {

		rayHitInfo info = hit.info;
		//objectInfo currObject = scene.objects[hit.objectIndex];
		float3 reflected = make_float3(0, 0, 0);
		float3 refracted = make_float3(0, 0, 0);
		float3 nextPos = hit.pos;
		float3 normal = hit.normal;

		float extraReflection = 0;
		float3 extraColor;
		float3 refractBias = 0.0005 * normal;
		float3 reflectBias = 0.001 * normal;
		float prevColorMP = 0;
		float3 extraPrevColor = make_float3(0,0,0);
		bool outside = dot(currRayDir, hit.normal) < 0;

		if (prevHitToAddDepthFrom.hit && prevHitToAddDepthFrom.info.insideColorDensity > 0.001) {
			prevColorMP = 1 - powf(1. - prevHitToAddDepthFrom.info.insideColorDensity, length(nextPos - currRayPos));
			extraPrevColor = prevColorMP * prevHitToAddDepthFrom.info.color;
		}
		else if (!prevHitToAddDepthFrom.hit){
			prevColorMP = 1 - powf(1. - AIR_DENSITY, length(nextPos - currRayPos));
			extraPrevColor = prevColorMP * AIR_COLOR;
		}

		if (info.refractivity > 0.0001) {
			float kr;
			fresnel(currRayDir, normal, outside ? info.refractiveIndex : 1 / info.refractiveIndex, kr);


			if (kr <= 1) {
				float3 refractionDirection = normalize(refract(currRayDir, normal, info.refractiveIndex));
				float3 refractionRayOrig = outside ? nextPos - refractBias : nextPos + refractBias;

				refracted = info.refractivity * max(0.,(1 - kr)) * trace(refractionRayOrig, refractionDirection, remainingDepth - 1,  hit);
			}
			extraReflection = max(0.,min(1., kr) * info.refractivity);

		}
		if (info.reflectivity + extraReflection > 0.00001) {
			float3 reflectDir = reflect(currRayDir, normal);
			float3 reflectionOrig = outside ? nextPos + reflectBias : nextPos - reflectBias;// nextPos + reflectNormal * 0.001;// : nextPos - reflectNormal * 0.01;//0.01;


			reflected = (info.reflectivity + extraReflection) * trace(reflectionOrig, reflectDir, remainingDepth - 1, hitInfo());
		}
		float3 color = ((1 - info.reflectivity - extraReflection - info.refractivity) * info.color);
#ifdef USING_POINT_LIGHT
		float3 light_dir = normalize(LIGHT_POS - nextPos);
		return 10000 * (1 / powf(length(nextPos - LIGHT_POS), 2)) *(0.2 + 0.8*getShadowTerm(nextPos + bias, scene)) * color + reflected + refracted;
#else
		float3 light_dir = STATIC_LIGHT_DIR;
		float angleFactor = (0.0 + 1.0 * max(0.0, dot(light_dir, normal)));
		return (1. - prevColorMP) * ((0.8*getShadowTerm(nextPos + 0.01*inverse(currRayDir),normal)* angleFactor + 0.2)* 1.0 *color + reflected + refracted) + extraPrevColor;
#endif
	}

}

__global__ void
cudaRender(inputPointers pointers, int imgw, int imgh, float currTime, inputStruct input)
{
	extern __shared__ uchar4 sdata[];

	int tx = threadIdx.x;
	int ty = threadIdx.y;
	int bw = blockDim.x;
	int bh = blockDim.y;
	int x = blockIdx.x * bw + tx;
	int y = blockIdx.y * bh + ty;

	float3 forwardV = make_float3(input.forwardX, input.forwardY, input.forwardZ);
	float3 upV = make_float3(input.upX, input.upY, input.upZ);
	float3 rightV = normalize(cross(upV, forwardV));

	float sizeFarPlane = 10;
	float sizeNearPlane = sizeFarPlane * 0.5;
	float3 origin = make_float3(input.currPosX, input.currPosY, input.currPosZ);
	float distFarPlane = 4;
	float distFirstPlane = distFarPlane * 0.5;

	float3 center = make_float3(imgw / 2.0, imgh / 2.0, 0.);
	float3 distFromCenter = ((x - center.x) / imgw) * rightV + ((center.y - y) / imgh) * upV;
	float3 firstPlanePos = (sizeNearPlane * distFromCenter) + origin + (distFirstPlane * forwardV);
	float3 secondPlanePos = (sizeFarPlane * distFromCenter) + (distFarPlane * forwardV) + origin;

	float3 dirVector = normalize(secondPlanePos - firstPlanePos);


	currentTime = currTime;
	scene = &pointers.scene;
	float3 out = 255 * trace(firstPlanePos, dirVector, 10, hitInfo())*3;

	pointers.g_odata[y * imgw + x] = rgbToInt(out.x, out.y, out.z);
}
extern "C" void
launch_cudaRender(dim3 grid, dim3 block, int sbytes, inputPointers pointers, int imgw, int imgh, float currTime, inputStruct input)
{

	cudaRender << < grid, block, sbytes >> > (pointers, imgw, imgh, currTime, input);
}

