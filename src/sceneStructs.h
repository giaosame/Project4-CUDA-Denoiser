#pragma once
#include <string>
#include <vector>
#include <cuda_runtime.h>
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "utilities.h"

#define BACKGROUND_COLOR (glm::vec3(0.0f))

enum GeomType {
	SPHERE,
	CUBE,
	MESH,
	HEART,
	TORUS,
	TANGLECUBE
};

struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;
};

struct Geom {
	enum GeomType type;
	int materialid;
	int meshid = -1;  // id of the corresponding mesh
	int num_faces ;   // number of faces in this mesh = gltf::Mesh::faces.size() / 3

	glm::vec3 translation;
	glm::vec3 rotation;
	glm::vec3 scale;
	glm::mat4 transform;
	glm::mat4 inverseTransform;
	glm::mat4 invTranspose;
};

struct Material {
	glm::vec3 color;
	struct {
		float exponent;
		glm::vec3 color;
	} specular;
	float hasReflective;
	float hasRefractive;
	float indexOfRefraction;
	float emittance;
};

struct Camera {
	glm::ivec2 resolution;
	glm::vec3 position;
	glm::vec3 lookAt;
	glm::vec3 view;
	glm::vec3 up;
	glm::vec3 right;
	glm::vec2 fov;
	glm::vec2 pixelLength;

	float focalDist = 0;
    float lensRadius = 0;

	__host__ __device__ Ray rayCast(float x, float y) const
	{
		Ray ray;
		ray.origin = position;
		ray.direction = glm::normalize(view - right * pixelLength.x * (x - (float)resolution.x * 0.5f) -
									   up * pixelLength.y * (y - (float)resolution.y * 0.5f));
		return ray;
	}
};

struct RenderState {
	Camera camera;
	unsigned int iterations;
	int traceDepth;
	std::vector<glm::vec3> image;
	std::string imageName;
};

struct PathSegment {
	Ray ray;
	glm::vec3 color;
	int pixelIndex;
	int remainingBounces;
};

// Use with a corresponding PathSegment to do:
// 1) color contribution computation
// 2) BSDF evaluation: generate a new ray
struct ShadeableIntersection {
	float t;
	glm::vec3 surfaceNormal;
	glm::vec3 tangent;
	glm::vec3 bitangent;
	glm::vec3 point;
	glm::vec3 gltfMatColor;
	glm::vec2 gltfUV;
	int materialId;
	Geom* hitGeom;
};

struct material_comp
{
	__host__ __device__ bool operator()(const ShadeableIntersection& isect1,
		const ShadeableIntersection& isect2)
	{
		return isect1.materialId > isect2.materialId;
	}
};

struct raytracing_continuing
{
	__host__ __device__ bool operator()(const PathSegment& segment)
	{
		return segment.remainingBounces > 0;
	}
};

__host__ __device__ inline void setGeomTransform(Geom* geom, const glm::mat4& trans)
{
	geom->transform = trans;
	geom->inverseTransform = glm::inverse(trans);
	geom->invTranspose = glm::inverseTranspose(trans);
}

enum FilterMode
{
	NEAREST = 9728,
	LINEAR = 9729,
	NEAREST_MIPMAP_NEAREST = 9984,
	LINEAR_MIPMAP_NEAREST = 9985,
	NEAREST_MIPMAP_LINEAR = 9986,
	LINEAR_MIPMAP_LINEAR = 9987,
};

enum Wrap
{
	CLAMP_TO_EDGE = 33071,
	MIRRORED_REPEAT = 33648,
	REPEAT = 10497,
};

// A simple struct for storing scene geometry information per-pixel.
// What information might be helpful for guiding a denoising filter?
struct GBufferPixel 
{
	float t;
};
