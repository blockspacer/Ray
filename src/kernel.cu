#pragma once
#include "rayHelpers.cu"



cudaError_t cuda();
__global__ void kernel(){
  
}



__device__ bool intersectsSphere(const float3 &origin, const float3& dir,  const sphereInfo& info, float &t) {

		float t0, t1; // solutions for t if the ray intersects 

		float3 L = info.pos - origin;
		float tca = dot(dir, L);
		 //if (tca < 0) return false;
		float d2 = dot(L,L) - tca * tca;
		if (d2 > info.rad2) return false;
		float thc = sqrt(info.rad2 - d2);
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
__device__ bool intersectPlane(const planeInfo& p, const float3& l0, const float3& l, float& t)
{
	// assuming vectors are all normalized
	float denom = dot(p.normal, l);
	if (denom > 1e-6) {
		float3 p0l0 = p.point - l0;
		t = dot(p0l0, p.normal) / denom;
		return (t >= 0);
	}
	return false;
}


__device__ void fresnel(const float3& I, const float3& N, const float& ior, float& kr)
{
	float cosi = clamp(-1, 1, dot(I, N));
	float etai = 1, etat = ior;
	if (cosi > 0) { float temp = etai; etai = etat; etat = temp;}
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
	int objectIndex = -1;
	float3 pos;
	float3 normal;

};

#define LIGHT_POS make_float3(0,5,20)


__device__ hitInfo getHit(float3 currRayPos, float3 currRayDir, const float& currTime, const objectInfo* objects, int numObjects) {
	float closestDist = 1000000;
	float3 normal;
	hitInfo toReturn;
	int closestObjectIndex = -1;


	for (int i = 0; i < numObjects; i++) {
		const objectInfo& curr = objects[i];
		float currDist;

		switch (curr.s) {
		case plane: {
			planeInfo* p1 = (planeInfo*)curr.shapeData;
			if (intersectPlane(*p1, currRayPos, currRayDir, currDist) && currDist < closestDist) {
				closestDist = currDist;
				closestObjectIndex = i;

				normal = p1->normal;
			}

			break;
		}
		case sphere: {
			sphereInfo* s1 = (sphereInfo*)curr.shapeData;
			if (intersectsSphere(currRayPos, currRayDir, *s1, currDist) && currDist < closestDist) {
				closestDist = currDist;
				closestObjectIndex = i;

				float3 nextPos = currRayPos + currDist * currRayDir;
				normal = normalize(nextPos - s1->pos);

			}
			break;
		}
		}
	}

	toReturn.objectIndex = closestObjectIndex;
	toReturn.normal = normal;
	toReturn.pos = currRayPos + closestDist * currRayDir;
	return toReturn;
}


__device__ float getShadowTerm(const float3 originalPos, const float currTime, const objectInfo* objects, int numObjects) {
	float3 toLightVec = normalize(LIGHT_POS - originalPos);
	hitInfo hit = getHit(originalPos, toLightVec, currTime, objects, numObjects);

	if (hit.objectIndex == -1 || length(hit.pos - originalPos) > length(originalPos - LIGHT_POS)) {
		return 1.;
	}
	return objects[hit.objectIndex].refractivity * 0.8 + 0.2;
	//while (length(currPos - LIGHT_POS) > 0.1) {
	//
	//}

}

__device__ float3 trace(const float3 currRayPos, const float3 currRayDir, int remainingDepth, const float currTime, objectInfo *objects, int numObjects) {
	if (remainingDepth <= 0) {
		return make_float3(0,0,0);
	}

	hitInfo hit = getHit(currRayPos, currRayDir, currTime, objects, numObjects);

	if (hit.objectIndex == -1) {
		return make_float3(0,0,0);
	}
	else {
		objectInfo currObject = objects[hit.objectIndex];
		float3 reflected = make_float3(0, 0, 0);
		float3 refracted = make_float3(0, 0, 0);
		float3 nextPos = hit.pos;
		float3 normal = hit.normal;

		float extraReflection = 0;
		float3 bias = 0.001 * normal;
		if (currObject.refractivity > 0.) {
			float kr;
			bool outside = dot(currRayDir, normal) < 0;
			fresnel(currRayDir, normal, outside? currObject.refractiveIndex : 1 / currObject.refractiveIndex, kr);


			if (kr < 1) {
				float3 refractionDirection = normalize(refract(currRayDir, normal, currObject.refractiveIndex));
				float3 refractionRayOrig = outside ? nextPos - bias : nextPos + bias;
				refracted = currObject.refractivity *(1-kr)* trace(refractionRayOrig, refractionDirection, remainingDepth - 1, currTime, objects, numObjects);
			}
			extraReflection = min(1.,kr) * currObject.refractivity;

		}
		if (currObject.reflectivity + extraReflection > 0.) {
			float3 reflectDir = reflect(currRayDir, normal);
			reflected = (currObject.reflectivity + extraReflection )* trace(nextPos + bias, reflectDir, remainingDepth - 1, currTime, objects, numObjects);
		}
		float3 color = (1 - currObject.reflectivity - extraReflection - currObject.refractivity) * currObject.color;
		return 1000 * (1 / powf(length(nextPos - LIGHT_POS), 2)) * getShadowTerm(nextPos + bias, currTime, objects, numObjects) * color + reflected + refracted;
	}

}

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

__global__ void
cudaRender(unsigned int *g_odata, int imgw, int imgh, float currTime, inputStruct input)
{
	extern __shared__ uchar4 sdata[];

	int tx = threadIdx.x;
	int ty = threadIdx.y;
	int bw = blockDim.x;
	int bh = blockDim.y;
	int x = blockIdx.x*bw + tx;
	int y = blockIdx.y*bh + ty;

	float3 forwardV = make_float3(input.forwardX, input.forwardY, input.forwardZ);
	float3 upV = make_float3(input.upX, input.upY, input.upZ);
	float3 rightV = normalize(cross(upV,forwardV));

	float sizeFarPlane = 10;
	float sizeNearPlane = sizeFarPlane*0.5;
	float3 origin = make_float3(input.currPosX, input.currPosY, input.currPosZ);
	float distFarPlane = 4;
	float distFirstPlane = distFarPlane *0.5;

	float3 center = make_float3(imgw / 2.0, imgh / 2.0, 0.);
	float3 distFromCenter = ((x - center.x) / imgw) * rightV + ((center.y - y) / imgh) * upV;
	float3 firstPlanePos = (sizeNearPlane*distFromCenter) + origin + (distFirstPlane * forwardV);
	float3 secondPlanePos = (sizeFarPlane * distFromCenter) + (distFarPlane * forwardV) + origin;




	float3 dirVector = normalize(secondPlanePos - firstPlanePos);

	sphereInfo s1 = make_sphereInfo(make_float3(sin(currTime) * 2.0, -3, cos(currTime) * 2 - 15), 1);
	sphereInfo s2 = make_sphereInfo(make_float3(-15, -4, -15), 4);
	sphereInfo s3 = make_sphereInfo(make_float3(2, 4, -40), 8);
	sphereInfo s4 = make_sphereInfo(make_float3(sin(currTime * 0.2)*6 + 4, 1,  cos(currTime*0.2) * 5 - 10), 3);
	planeInfo p1 = make_planeInfo(make_float3(0, -4.0, 0), make_float3(0, -1, 0));
	planeInfo p2 = make_planeInfo(make_float3(0, 50.0, 0), make_float3(0, 1, 0));
	planeInfo p3 = make_planeInfo(make_float3(0, 0.0, -70), make_float3(0, 0, -1));
	planeInfo p4 = make_planeInfo(make_float3(70, 0, 0), make_float3(1, 0, 0));

	objectInfo objects[10];
	objects[0] = make_objectInfo(sphere, &s1, 0.0, make_float3(1, 0, 0),0,0);
	objects[1] = make_objectInfo(sphere, &s2, 0.5, make_float3(0, 1, 0),0.0,1.5);
	objects[2] = make_objectInfo(plane, &p1, 0.2, make_float3(0, 1, 1),0,0);
	objects[3] = make_objectInfo(sphere, &s3, 0.7, make_float3(1, 1, 1), 0,0);
	objects[4] = make_objectInfo(plane, &p2, 0.0, make_float3(1, 1, 1), 0,0);
	objects[5] = make_objectInfo(sphere, &s4, 0.0, make_float3(1, 1, 1), 0.9,1.5);
	objects[6] = make_objectInfo(plane, &p3, 0.5, make_float3(0, 1, 0), 0,0);
	objects[7] = make_objectInfo(plane, &p4, 0.5, make_float3(0, 1, 0), 0,0);

	float3 out = 255*trace(firstPlanePos, dirVector, 1, currTime, objects, 8);


	g_odata[y * imgw + x] = rgbToInt(out.x, out.y, out.z);
}
extern "C" void
launch_cudaRender(dim3 grid, dim3 block, int sbytes, unsigned int *g_odata, int imgw, int imgh, float currTime,inputStruct input)
{

	cudaRender << < grid, block, sbytes >> >(g_odata, imgw, imgh, currTime, input);
}

