#pragma once
#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>
#include "sceneStructs.h"
#include "utilities.h"
#include "gltf-loader.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "warpfunctions.h"
#include "implicitsurface.h"

#define MOTIONBLUR true
#define MOTIONBLUR_VELOCITY glm::vec3(0.618, 0, 0)
#define EPSILON 0.00618f

__host__ __device__
glm::mat4 getTansformation(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) 
{
	glm::mat4 translationMat = glm::translate(glm::mat4(), translation);
	glm::mat4 rotationMat = glm::rotate(glm::mat4(), rotation.x * (float)PI / 180, glm::vec3(1, 0, 0));
	rotationMat = rotationMat * glm::rotate(glm::mat4(), rotation.y * (float)PI / 180, glm::vec3(0, 1, 0));
	rotationMat = rotationMat * glm::rotate(glm::mat4(), rotation.z * (float)PI / 180, glm::vec3(0, 0, 1));
	glm::mat4 scaleMat = glm::scale(glm::mat4(), scale);
	return translationMat * rotationMat * scaleMat;
}

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) 
{
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t)
{
	return r.origin + (t - 0.0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v)
{
	return glm::vec3(m * v);
}

/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(const Geom& box,
											  const Ray& r,
											  glm::vec3& intersectionPoint, 
											  glm::vec3& normal, 
											  bool& outside)
{
	Ray q;
	q.origin = multiplyMV(box.inverseTransform, glm::vec4(r.origin, 1.0f));
	q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

	float tmin = -1e38f;
	float tmax = 1e38f;
	glm::vec3 tmin_n;
	glm::vec3 tmax_n;
	for (int xyz = 0; xyz < 3; ++xyz)
	{
		float qdxyz = q.direction[xyz];
		/*if (glm::abs(qdxyz) > 0.00001f)*/ {
			float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
			float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
			float ta = glm::min(t1, t2);
			float tb = glm::max(t1, t2);
			glm::vec3 n;
			n[xyz] = t2 < t1 ? +1 : -1;
			if (ta > 0 && ta > tmin) 
			{
				tmin = ta;
				tmin_n = n;
			}
			if (tb < tmax) 
			{
				tmax = tb;
				tmax_n = n;
			}
		}
	}

	if (tmax >= tmin && tmax > 0) 
	{
		outside = true;
		if (tmin <= 0) {
			tmin = tmax;
			tmin_n = tmax_n;
			outside = false;
		}

		intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin), 1.0f));
		normal = glm::normalize(multiplyMV(box.invTranspose, glm::vec4(tmin_n, 0.0f)));
		return glm::length(r.origin - intersectionPoint);
	}
	return -1;
}

/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(const Geom& sphere,
												 const Ray& r,
												 glm::vec3& intersectionPoint, 
												 glm::vec3& normal, 
												 bool& outside)
{
	float radius = 0.5f;

	glm::vec3 ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
	glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

	Ray rt;
	rt.origin = ro;
	rt.direction = rd;

	float vDotDirection = glm::dot(rt.origin, rt.direction);
	float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
	if (radicand < 0)
	{
		return -1;
	}

	float squareRoot = sqrt(radicand);
	float firstTerm = -vDotDirection;
	float t1 = firstTerm + squareRoot;
	float t2 = firstTerm - squareRoot;

	float t = 0;
	if (t1 < 0 && t2 < 0)
	{
		return -1;
	}
	else if (t1 > 0 && t2 > 0)
	{
		t = min(t1, t2);
		outside = true;
	}
	else 
	{
		t = max(t1, t2);
		outside = false;
	}

	glm::vec3 objspaceIntersection = getPointOnRay(rt, t);

	intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
	normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));
	if (!outside) 
	{
		normal = -normal;
	}

	return glm::length(r.origin - intersectionPoint);
}

__host__ __device__ float getTriangleArea(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2)
{
	return 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
}

/**
 * Test intersection between a ray and a mesh loaded from glTF file. 
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float meshIntersectionTest(const Geom& mesh,
											   const Ray& r,
											   glm::vec3& intersectionPoint,
											   glm::vec3& normal,
											   glm::vec3& tangent,
											   glm::vec3& bitangent,
											   glm::vec2& intersectionUV,
											   int& intersectionMatId,
											   bool& outside,
											   unsigned int* faces,
											   float* vertices,
											   float* uvs,
									           unsigned int* faces_offset,
											   unsigned int* verts_offset,
											   unsigned int* gltf_mat_ids)
{
	// Initialize to default material
	// intersectionMatId = mesh.materialid;

	float t = FLT_MAX;
	bool intersected = false;
	unsigned int hit_face_idx = 0;
	unsigned int hit_mat_id = -1;
	glm::vec3 hit_p0, hit_p1, hit_p2;
	glm::vec3 objspaceIntersection, objspaceNormal;

	glm::vec3 ro = multiplyMV(mesh.inverseTransform, glm::vec4(r.origin, 1.0f));
	glm::vec3 rd = glm::normalize(multiplyMV(mesh.inverseTransform, glm::vec4(r.direction, 0.0f)));

	Ray rt;
	rt.origin = ro;
	rt.direction = rd;

	int mesh_idx = mesh.meshid;
	int f_offset = faces_offset[mesh_idx];
	int v_offset = verts_offset[mesh_idx];

	for (int face_idx = 0; face_idx < mesh.num_faces; face_idx++)
	{
		unsigned int f0, f1, f2;
		float v0[3], v1[3], v2[3];

		f0 = faces[3 * face_idx + 0 + f_offset];
		f1 = faces[3 * face_idx + 1 + f_offset];
		f2 = faces[3 * face_idx + 2 + f_offset];

		v0[0] = vertices[3 * f0 + 0 + v_offset];
		v0[1] = vertices[3 * f0 + 1 + v_offset];
		v0[2] = vertices[3 * f0 + 2 + v_offset];

		v1[0] = vertices[3 * f1 + 0 + v_offset];
		v1[1] = vertices[3 * f1 + 1 + v_offset];
		v1[2] = vertices[3 * f1 + 2 + v_offset];

		v2[0] = vertices[3 * f2 + 0 + v_offset];
		v2[1] = vertices[3 * f2 + 1 + v_offset];
		v2[2] = vertices[3 * f2 + 2 + v_offset];

		glm::vec3 p0(v0[0], v0[1], v0[2]);
		glm::vec3 p1(v1[0], v1[1], v1[2]);
		glm::vec3 p2(v2[0], v2[1], v2[2]);

		glm::vec3 res;
		bool intersected_this_time = glm::intersectRayTriangle(ro, rd, p0, p1, p2, res);
		if (intersected_this_time)
		{
			intersected = true;
			outside = false;
			if (res.z >= t)
				continue;

			t = res.z;
			objspaceIntersection = getPointOnRay(rt, t);
			objspaceNormal = glm::cross(p1 - p0, p2 - p0);
			hit_face_idx = face_idx;
			hit_mat_id = gltf_mat_ids[3 * face_idx + f_offset];
			hit_p0 = p0; hit_p1 = p1; hit_p2 = p2;
		}
	}

	if (intersected)
	{
		const glm::vec3 hit_p = getPointOnRay(rt, t + 0.0001f);
		const float s = getTriangleArea(hit_p0, hit_p1, hit_p2);
		const float s0 = getTriangleArea(hit_p, hit_p1, hit_p2);
		const float s1 = getTriangleArea(hit_p, hit_p0, hit_p2);
		const float s2 = getTriangleArea(hit_p, hit_p0, hit_p1);

		// Get the uv on the hit face
		const unsigned int uv_idx = 3 * hit_face_idx;
		const glm::vec2 hit_uv0 = glm::vec2(uvs[2 * (uv_idx + 0) + 2 * f_offset], uvs[2 * (uv_idx + 0) + 1 + 2 * f_offset]);
		const glm::vec2 hit_uv1 = glm::vec2(uvs[2 * (uv_idx + 1) + 2 * f_offset], uvs[2 * (uv_idx + 1) + 1 + 2 * f_offset]);
		const glm::vec2 hit_uv2 = glm::vec2(uvs[2 * (uv_idx + 2) + 2 * f_offset], uvs[2 * (uv_idx + 2) + 1 + 2 * f_offset]);

		// Compute tangent and bitangent based on uv
		const glm::vec3 delta_p1 = hit_p1 - hit_p0;
		const glm::vec3 delta_p2 = hit_p2 - hit_p0;
		const glm::vec2 delta_uv1 = hit_uv1 - hit_uv0;
		const glm::vec2 delta_uv2 = hit_uv2 - hit_uv0;

		tangent = (delta_uv2.y * delta_p1 - delta_uv1.y * delta_p2) / (delta_uv2.y * delta_uv1.x - delta_uv1.y * delta_uv2.x + 0.0001f);
		bitangent = (delta_p2 - delta_uv2.x * tangent) / (delta_uv2.y + 0.0001f);
		tangent = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(tangent, 0.f)));
		bitangent = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(bitangent, 0.f)));
		
		// Interpolate triangle uvs
		intersectionUV = hit_uv0 * s0 / s + hit_uv1 * s1 / s + hit_uv2 * s2 / s;
		intersectionMatId = -(int(hit_mat_id) + 1);
		intersectionPoint = multiplyMV(mesh.transform, glm::vec4(objspaceIntersection, 1.f));
		normal = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(objspaceNormal, 0.f)));
		return glm::length(r.origin - intersectionPoint);
	}
	return -1;
}

__host__ __device__ ShadeableIntersection getSampleOnSphere(const glm::vec2& xi, float* pdf, const Geom& geom)
{
	glm::vec3 pObj = WarpFunctions::squareToSphereUniform(xi);

	ShadeableIntersection it;
	it.surfaceNormal = glm::normalize(glm::vec3(geom.invTranspose * glm::vec4(pObj, 1)));
	it.point = glm::vec3(geom.transform * glm::vec4(pObj, 1));

	float area = 4.f * PI * geom.scale.x * geom.scale.x;
	*pdf = 1.0f / area;

	return it;
}

__host__ __device__ ShadeableIntersection getSampleOnSquare(const glm::vec2& xi, float* pdf, const Geom& geom)
{
	ShadeableIntersection it;
	glm::vec4 localNormal(0);
	glm::vec4 localPoint(0);
	localPoint.w = 1.f;

	float minScale = min(geom.scale.x, min(geom.scale.y, geom.scale.z));
	if (minScale == geom.scale.x)
	{
		localNormal.x = -1.f;
		localPoint.x = -0.5f;
		localPoint.y = xi.x - 0.5f;
		localPoint.z = xi.y - 0.5f;
	}
	else if (minScale == geom.scale.y)
	{
		localNormal.y = -1.f;
		localPoint.y = -0.5f;
		localPoint.x = xi.x - 0.5f;
		localPoint.z = xi.y - 0.5f;
	}
	else
	{
		localNormal.z = -1.f;
		localPoint.z = -0.5f;
		localPoint.x = xi.x - 0.5f;
		localPoint.y = xi.y - 0.5f;
	}

	glm::vec3 worldPoint = multiplyMV(geom.transform, localPoint);
	glm::vec3 worldNormal = glm::normalize(multiplyMV(geom.invTranspose, localNormal));

	it.point = worldPoint;
	it.surfaceNormal = worldNormal;
	float area = max(geom.scale.x * geom.scale.y, max(geom.scale.y * geom.scale.z, geom.scale.x * geom.scale.z));
	*pdf = 1.f / area;

	return it;
}


__host__ __device__ float implicitSurfaceIntersectionTest(const Geom& implicitSurface,
														  const Ray& r,
														  glm::vec3& intersectionPoint,
														  glm::vec3& normal,
														  bool& outside)
{
	const float MAX_DIST = 30.0f;
	const float STEP = 1e-4f;
	const float SCALE = implicitSurface.scale.x;

	glm::vec3 ro = multiplyMV(implicitSurface.inverseTransform, glm::vec4(r.origin, 1.0f));
	glm::vec3 rd = glm::normalize(multiplyMV(implicitSurface.inverseTransform, glm::vec4(r.direction, 0.0f)));

	Ray rt;
	rt.origin = ro;
	rt.direction = rd;

	const float x = ro.x;
	const float y = ro.y;
	const float z = ro.z;
	//printf("x = %f, y = %f, z = %f\n", x, y, z);

	float t = 0;
	bool intersected = false;
	if (implicitSurface.type == GeomType::HEART)
	{
		for (; t < MAX_DIST; t += STEP, ro += STEP * rd)
		{
			float curDist = ImplicitSurface::heartSDF(ro);
			if (curDist < EPSILON)
			{
				intersected = true;
				break;
			}
		}
	}
	else if (implicitSurface.type == GeomType::TANGLECUBE)
	{
		for (; t < MAX_DIST; t += STEP, ro += STEP * rd)
		{
			float curDist = ImplicitSurface::tanglecubeSDF(ro);
			if (curDist < EPSILON)
			{
				intersected = true;
				break;
			}
		}
	}
	else
	{
		for (; t < MAX_DIST; t += STEP, ro += STEP * rd)
		{
			float curDist = ImplicitSurface::torusSDF(ro);
			if (curDist < EPSILON)
			{
				intersected = true;
				break;
			}
		}
	}
	
	if (intersected)
	{
		glm::vec3 objspaceIntersection = getPointOnRay(rt, t);
		glm::vec3 objspaceNormal = ImplicitSurface::computeSurfaceNormal(implicitSurface, objspaceIntersection);
		intersectionPoint = multiplyMV(implicitSurface.transform, glm::vec4(objspaceIntersection, 1.f));
		normal = glm::normalize(multiplyMV(implicitSurface.invTranspose, glm::vec4(objspaceNormal, 0.f)));
		return t;
	}

	return -1;;
}