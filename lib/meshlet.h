//-------------------------------------------------------------------------------------
// DirectXMeshP.h, DirectXMesh.h
//
// DirectX Mesh Geometry Library
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=324981
//-------------------------------------------------------------------------------------

#ifndef MESHLET_H_INCLUDED
#define MESHLET_H_INCLUDED

#include "data/niftypes.h"
#include "fp32vec4.hpp"

#include <unordered_set>
#include <array>
#include <cassert>
#include <cerrno>


namespace DirectX
{
	//---------------------------------------------------------------------------------
	constexpr uint32_t UNUSED32 = uint32_t(-1);

	//---------------------------------------------------------------------------------
	// Meshlet Generation

	constexpr size_t MESHLET_DEFAULT_MAX_VERTS = 128u;
	constexpr size_t MESHLET_DEFAULT_MAX_PRIMS = 128u;

	constexpr size_t MESHLET_MINIMUM_SIZE = 32u;
	constexpr size_t MESHLET_MAXIMUM_SIZE = 256u;

	enum MESHLET_FLAGS : unsigned long
	{
		MESHLET_DEFAULT = 0x0,

		MESHLET_WIND_CW = 0x1,
		// Vertices are clock-wise (defaults to CCW)
	};

	struct Meshlet
	{
		uint32_t VertCount;
		uint32_t VertOffset;
		uint32_t PrimCount;
		uint32_t PrimOffset;
	};

	int ComputeMeshlets(
		const Triangle* triangles, size_t nFaces, const Vector3* positions, size_t nVerts,
		std::vector<Meshlet>& meshlets, std::vector<uint16_t>& primitiveIndices,
		size_t maxVerts = MESHLET_DEFAULT_MAX_VERTS, size_t maxPrims = MESHLET_DEFAULT_MAX_PRIMS);
		// Generates meshlets for a single subset mesh

} // namespace DirectX

#endif
