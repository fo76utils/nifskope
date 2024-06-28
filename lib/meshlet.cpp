//-------------------------------------------------------------------------------------
// DirectXMeshletGenerator.cpp, DirectXMeshAdjacency.cpp
//
// DirectX Mesh Geometry Library - Meshlet and Adjacency Computation
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=324981
//-------------------------------------------------------------------------------------

#include "meshlet.h"
#include "gl/gltools.h"

namespace DirectX
{
	//---------------------------------------------------------------------------------
	// Helper class which manages a fixed-size array like a vector.
	//---------------------------------------------------------------------------------
	template <typename T, size_t N>
	class StaticVector
	{
	public:
		StaticVector() noexcept
			: m_data{}, m_size(0)
		{ }
		~StaticVector() = default;

		StaticVector(StaticVector&&) = default;
		StaticVector& operator= (StaticVector&&) = default;

		StaticVector(StaticVector const&) = default;
		StaticVector& operator= (StaticVector const&) = default;

		void push_back(const T& value) noexcept
		{
			assert(m_size < N);
			m_data[m_size++] = value;
		}

		void push_back(T&& value) noexcept
		{
			assert(m_size < N);
			m_data[m_size++] = std::move(value);
		}

		template <typename... Args>
		void emplace_back(Args&&... args) noexcept
		{
			assert(m_size < N);
			m_data[m_size++] = T(std::forward<Args>(args)...);
		}

		size_t size() const noexcept { return m_size; }
		bool empty() const noexcept { return m_size == 0; }

		T* data() noexcept { return m_data.data(); }
		const T* data() const noexcept { return m_data.data(); }

		T& operator[](size_t index) noexcept { assert(index < m_size); return m_data[index]; }
		const T& operator[](size_t index) const noexcept { assert(index < m_size); return m_data[index]; }

	private:
		std::array<T, N> m_data;
		size_t           m_size;
	};

	struct MeshletTriangle
	{
		uint32_t i0 : 10;
		uint32_t i1 : 10;
		uint32_t i2 : 10;
	};

	//---------------------------------------------------------------------------------
	// Helper struct which maintains the working state of a new meshlet
	//---------------------------------------------------------------------------------
	template <typename T>
	struct InlineMeshlet
	{
		StaticVector<T, MESHLET_MAXIMUM_SIZE>               UniqueVertexIndices;
		StaticVector<MeshletTriangle, MESHLET_MAXIMUM_SIZE> PrimitiveIndices;
	};

	inline FloatVector4 NormalizeVector3(FloatVector4 v)
	{
		float   r = v.dotProduct3(v);
		if (r > 0.0f) [[likely]]
			return v / float(std::sqrt(r));
		return FloatVector4(0.0f, 0.0f, 1.0f, 0.0f);
	}

	//---------------------------------------------------------------------------------
	// Computes normal vector from the points of a triangle
	//---------------------------------------------------------------------------------
	inline FloatVector4 ComputeNormal(const Vector3* tri) noexcept
	{
		FloatVector4  p0(tri[0]);
		FloatVector4  p1(tri[1]);
		FloatVector4  p2(tri[2]);

		FloatVector4  v01 = p0 - p1;
		FloatVector4  v02 = p0 - p2;

		return NormalizeVector3(v01.crossProduct3(v02));
	}

	//---------------------------------------------------------------------------------
	// Utility for walking adjacency
	//---------------------------------------------------------------------------------
	template<class index_t>
	class orbit_iterator
	{
	public:
		enum WalkType
		{
			ALL = 0,
			CW,
			CCW
		};

		orbit_iterator(const uint32_t* adjacency, const index_t* indices, size_t nFaces) noexcept :
			m_face(UNUSED32),
			m_pointIndex(UNUSED32),
			m_currentFace(UNUSED32),
			m_currentEdge(UNUSED32),
			m_nextEdge(UNUSED32),
			m_adjacency(adjacency),
			m_indices(indices),
			m_nFaces(nFaces),
			m_clockWise(false),
			m_stopOnBoundary(false)
		{
		}

		void initialize(uint32_t face, uint32_t point, WalkType wtype) noexcept
		{
			m_face = m_currentFace = face;
			m_pointIndex = point;
			m_clockWise = (wtype != CCW);
			m_stopOnBoundary = (wtype != ALL);

			m_nextEdge = find(face, point);
			assert(m_nextEdge < 3);

			if (!m_clockWise)
			{
				m_nextEdge = (m_nextEdge + 2) % 3;
			}

			m_currentEdge = m_nextEdge;
		}

		uint32_t find(uint32_t face, uint32_t point) noexcept
		{
			assert(face < m_nFaces);

			if (m_indices[face * 3] == point)
				return 0;
			else if (m_indices[face * 3 + 1] == point)
				return 1;
			else
			{
				assert(m_indices[face * 3 + 2] == point);
				return 2;
			}
		}

		uint32_t nextFace() noexcept
		{
			assert(!done());

			const uint32_t ret = m_currentFace;
			m_currentEdge = m_nextEdge;

			for (;;)
			{
				const uint32_t prevFace = m_currentFace;

				assert((size_t(m_currentFace) * 3 + m_nextEdge) < (m_nFaces * 3));

				m_currentFace = m_adjacency[m_currentFace * 3 + m_nextEdge];

				if (m_currentFace == m_face)
				{
					// wrapped around after a full orbit, so finished
					m_currentFace = UNUSED32;
					break;
				}
				else if (m_currentFace != UNUSED32)
				{
					assert((size_t(m_currentFace) * 3 + 2) < (m_nFaces * 3));

					if (m_adjacency[m_currentFace * 3] == prevFace)
						m_nextEdge = 0;
					else if (m_adjacency[m_currentFace * 3 + 1] == prevFace)
						m_nextEdge = 1;
					else
					{
						assert(m_adjacency[m_currentFace * 3 + 2] == prevFace);
						m_nextEdge = 2;
					}

					if (m_clockWise)
					{
						m_nextEdge = (m_nextEdge + 1) % 3;
					}
					else
					{
						m_nextEdge = (m_nextEdge + 2) % 3;
					}

					break;
				}
				else if (m_clockWise && !m_stopOnBoundary)
				{
					// hit boundary and need to restart to go counter-clockwise
					m_clockWise = false;
					m_currentFace = m_face;

					m_nextEdge = find(m_face, m_pointIndex);
					assert(m_nextEdge < 3);

					m_nextEdge = (m_nextEdge + 2) % 3;
					m_currentEdge = (m_currentEdge + 2) % 3;

					// Don't break out of loop so we can go the other way
				}
				else
				{
					// hit boundary and should stop
					break;
				}
			}

			return ret;
		}

		bool moveToCCW() noexcept
		{
			m_currentFace = m_face;

			m_nextEdge = find(m_currentFace, m_pointIndex);
			const uint32_t initialNextEdge = m_nextEdge;
			assert(m_nextEdge < 3);

			m_nextEdge = (m_nextEdge + 2) % 3;

			bool ret = false;

			uint32_t prevFace;
			do
			{
				prevFace = m_currentFace;
				m_currentFace = m_adjacency[m_currentFace * 3 + m_nextEdge];

				if (m_currentFace != UNUSED32)
				{
					if (m_adjacency[m_currentFace * 3] == prevFace)
						m_nextEdge = 0;
					else if (m_adjacency[m_currentFace * 3 + 1] == prevFace)
						m_nextEdge = 1;
					else
					{
						assert(m_adjacency[m_currentFace * 3 + 2] == prevFace);
						m_nextEdge = 2;
					}

					m_nextEdge = (m_nextEdge + 2) % 3;
				}
			}
			while ((m_currentFace != m_face) && (m_currentFace != UNUSED32));

			if (m_currentFace == UNUSED32)
			{
				m_currentFace = prevFace;
				m_nextEdge = (m_nextEdge + 1) % 3;

				m_pointIndex = m_indices[m_currentFace * 3 + m_nextEdge];

				ret = true;
			}
			else
			{
				m_nextEdge = initialNextEdge;
			}

			m_clockWise = true;
			m_currentEdge = m_nextEdge;
			m_face = m_currentFace;
			return ret;
		}

		bool done() const noexcept { return (m_currentFace == UNUSED32); }
		uint32_t getpoint() const noexcept { return m_clockWise ? m_currentEdge : ((m_currentEdge + 1) % 3); }

	private:
		uint32_t        m_face;
		uint32_t        m_pointIndex;
		uint32_t        m_currentFace;
		uint32_t        m_currentEdge;
		uint32_t        m_nextEdge;

		const uint32_t* m_adjacency;
		const index_t*  m_indices;
		size_t          m_nFaces;

		bool            m_clockWise;
		bool            m_stopOnBoundary;
	};


	//-------------------------------------------------------------------------------------
	template<class index_t>
	inline uint32_t find_edge(const index_t* indices, index_t search) noexcept
	{
		assert(indices != nullptr);

		uint32_t edge = 0;

		for (; edge < 3; ++edge)
		{
			if (indices[edge] == search)
				break;
		}

		return edge;
	}

	//---------------------------------------------------------------------------------
	// Computes number of triangle vertices already exist in the meshlet
	//---------------------------------------------------------------------------------
	template <typename T>
	uint8_t ComputeReuse(const InlineMeshlet<T>& meshlet, const T* triIndices) noexcept
	{
		uint8_t count = 0;
		for (size_t i = 0; i < meshlet.UniqueVertexIndices.size(); ++i)
		{
			for (size_t j = 0; j < 3u; ++j)
			{
				if (meshlet.UniqueVertexIndices[i] == triIndices[j])
				{
					assert(count < 255);
					++count;
				}
			}
		}

		return count;
	}

	//---------------------------------------------------------------------------------
	// Computes a candidacy score based on spatial locality, orientational coherence,
	// and vertex re-use within a meshlet.
	//---------------------------------------------------------------------------------
	template <typename T>
	float ComputeScore(
		const InlineMeshlet<T>& meshlet, FloatVector4 sphere, FloatVector4 normal,
		const T* triIndices, const Vector3* triVerts) noexcept
	{
		// Configurable weighted sum parameters
		constexpr float c_wtReuse = 0.334f;
		constexpr float c_wtLocation = 0.333f;
		constexpr float c_wtOrientation = 1.0f - (c_wtReuse + c_wtLocation);

		// Vertex reuse -
		const uint8_t reuse = ComputeReuse(meshlet, triIndices);
		const float scrReuse = 1.0f - (float(reuse) / 3.0f);

		// Distance from center point - log falloff to preserve normalization where it needs it
		float maxSq = 0;
		for (size_t i = 0; i < 3u; ++i)
		{
			FloatVector4  pos(triVerts[i]);
			FloatVector4  v = sphere - pos;

			const float distSq = v.dotProduct3(v);
			maxSq = std::max(maxSq, distSq);
		}

		const float r = sphere[3];
		const float r2 = r * r;
		const float scrLocation = std::max(0.0f, log2f(maxSq / (r2 + FLT_EPSILON) + FLT_EPSILON));

		// Angle between normal and meshlet cone axis - cosine falloff
		FloatVector4  n = ComputeNormal(triVerts);
		const float   d = n.dotProduct3(normal);
		const float   scrOrientation = (1.0f - d) * 0.5f;

		// Weighted sum of scores
		return c_wtReuse * scrReuse + c_wtLocation * scrLocation + c_wtOrientation * scrOrientation;
	}

	//---------------------------------------------------------------------------------
	// Attempts to add a candidate triangle to a meshlet
	//---------------------------------------------------------------------------------
	template <typename T>
	bool TryAddToMeshlet(size_t maxVerts, size_t maxPrims, const T* tri, InlineMeshlet<T>& meshlet)
	{
		// Cull degenerate triangle and return success
		// newCount calculation will break if such triangle is passed
		if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
			return true;

		// Are we already full of vertices?
		if (meshlet.UniqueVertexIndices.size() >= maxVerts)
			return false;

		// Are we full, or can we store an additional primitive?
		if (meshlet.PrimitiveIndices.size() >= maxPrims)
			return false;

		uint32_t indices[3] = { uint32_t(-1), uint32_t(-1), uint32_t(-1) };
		uint8_t newCount = 3;

		for (size_t i = 0; i < meshlet.UniqueVertexIndices.size(); ++i)
		{
			for (size_t j = 0; j < 3; ++j)
			{
				if (meshlet.UniqueVertexIndices[i] == tri[j])
				{
					indices[j] = static_cast<uint32_t>(i);
					--newCount;
				}
			}
		}

		// Will this triangle fit?
		if (meshlet.UniqueVertexIndices.size() + newCount > maxVerts)
			return false;

		// Add unique vertex indices to unique vertex index list
		for (size_t j = 0; j < 3; ++j)
		{
			if (indices[j] == uint32_t(-1))
			{
				indices[j] = static_cast<uint32_t>(meshlet.UniqueVertexIndices.size());
				meshlet.UniqueVertexIndices.push_back(tri[j]);
			}
		}

		// Add the new primitive
		MeshletTriangle mtri = { indices[0], indices[1], indices[2] };
		meshlet.PrimitiveIndices.emplace_back(mtri);

		return true;
	}

	//---------------------------------------------------------------------------------
	// Determines whether a meshlet contains the maximum number of vertices/primitives
	//---------------------------------------------------------------------------------
	template <typename T>
	inline bool IsMeshletFull(size_t maxVerts, size_t maxPrims, const InlineMeshlet<T>& meshlet) noexcept
	{
		assert(meshlet.UniqueVertexIndices.size() <= maxVerts);
		assert(meshlet.PrimitiveIndices.size() <= maxPrims);

		return meshlet.UniqueVertexIndices.size() >= maxVerts || meshlet.PrimitiveIndices.size() >= maxPrims;
	}

	//---------------------------------------------------------------------------------
	// Meshletize a contiguous list of primitives
	//---------------------------------------------------------------------------------
	template <typename T>
	int Meshletize(
		size_t maxVerts, size_t maxPrims, const T* indices, size_t nFaces, const Vector3* positions, size_t nVerts,
		const std::pair<size_t, size_t>& subset, const uint32_t* adjacency, std::vector<InlineMeshlet<T>>& meshlets)
	{
		if (!indices || !positions || !adjacency)
			return EINVAL;

		if (subset.first + subset.second > nFaces)
			return ERANGE;

		meshlets.clear();

		// Bitmask of all triangles in mesh to determine whether a specific one has been added
		std::vector<bool> checklist;
		checklist.resize(subset.second);

		// Cache to maintain scores for each candidate triangle
		std::vector<std::pair<uint32_t, float>> candidates;
		std::unordered_set<uint32_t> candidateCheck;

		// Positions and normals of the current primitive
		std::vector<Vector3>  vertices;
		std::vector<Vector3>  normals;

		// Seed the candidate list with the first triangle of the subset
		const uint32_t startIndex = static_cast<uint32_t>(subset.first);
		const uint32_t endIndex = static_cast<uint32_t>(subset.first + subset.second);

		uint32_t triIndex = static_cast<uint32_t>(subset.first);

		candidates.push_back(std::make_pair(triIndex, 0.0f));
		candidateCheck.insert(triIndex);

		// Continue adding triangles until triangle list is exhausted.
		InlineMeshlet<T>* curr = nullptr;

		while (!candidates.empty())
		{
			uint32_t index = candidates.back().first;
			candidates.pop_back();

			T tri[3] =
			{
				indices[index * 3],
				indices[index * 3 + 1],
				indices[index * 3 + 2],
			};

			if (tri[0] >= nVerts ||
				tri[1] >= nVerts ||
				tri[2] >= nVerts)
			{
				return ERANGE;
			}

			// Create a new meshlet if necessary
			if (curr == nullptr)
			{
				vertices.clear();
				normals.clear();

				meshlets.emplace_back();
				curr = &meshlets.back();
			}

			// Try to add triangle to meshlet
			if (TryAddToMeshlet(maxVerts, maxPrims, tri, *curr))
			{
				// Success! Mark as added.
				checklist[index - startIndex] = true;

				// Add positions & normal to list
				const Vector3 points[3] =
				{
					positions[tri[0]],
					positions[tri[1]],
					positions[tri[2]],
				};

				vertices.push_back(points[0]);
				vertices.push_back(points[1]);
				vertices.push_back(points[2]);

				normals.emplace_back();
				normals.back().fromFloatVector4(ComputeNormal(points));

				// Compute new bounding sphere & normal axis
				BoundSphere positionBounds(vertices.data(), qsizetype(vertices.size()), false);
				BoundSphere normalBounds(normals.data(), qsizetype(normals.size()), false);

				FloatVector4  psphere(positionBounds.center[0], positionBounds.center[1], positionBounds.center[2], positionBounds.radius);
				FloatVector4  normal(normalBounds.center[0], normalBounds.center[1], normalBounds.center[2], normalBounds.radius);

				// Find and add all applicable adjacent triangles to candidate list
				const uint32_t adjIndex = index * 3;

				uint32_t adj[3] =
				{
					adjacency[adjIndex],
					adjacency[adjIndex + 1],
					adjacency[adjIndex + 2],
				};

				for (size_t i = 0; i < 3u; ++i)
				{
					// Invalid triangle in adjacency slot
					if (adj[i] == uint32_t(-1))
						continue;

					// Primitive is outside the subset
					if (adj[i] < subset.first || adj[i] > endIndex)
						continue;

					// Already processed triangle
					if (checklist[adj[i] - startIndex])
						continue;

					// Triangle already in the candidate list
					if (candidateCheck.count(adj[i]))
						continue;

					candidates.push_back(std::make_pair(adj[i], FLT_MAX));
					candidateCheck.insert(adj[i]);
				}

				// Re-score remaining candidate triangles
				for (size_t i = 0; i < candidates.size(); ++i)
				{
					uint32_t candidate = candidates[i].first;

					T triIndices[3] =
					{
						indices[candidate * 3],
						indices[candidate * 3 + 1],
						indices[candidate * 3 + 2],
					};

					if (triIndices[0] >= nVerts ||
						triIndices[1] >= nVerts ||
						triIndices[2] >= nVerts)
					{
						return ERANGE;
					}

					const Vector3 triVerts[3] =
					{
						positions[triIndices[0]],
						positions[triIndices[1]],
						positions[triIndices[2]],
					};

					candidates[i].second = ComputeScore(*curr, psphere, normal, triIndices, triVerts);
				}

				// Determine whether we need to move to the next meshlet.
				if (IsMeshletFull(maxVerts, maxPrims, *curr))
				{
					candidateCheck.clear();
					curr = nullptr;

					// Discard candidates -  one of our existing candidates as the next meshlet seed.
					if (!candidates.empty())
					{
						candidates[0] = candidates.back();
						candidates.resize(1);
						candidateCheck.insert(candidates[0].first);
					}
				}
				else
				{
					// Sort in reverse order to use vector as a queue with pop_back
					std::stable_sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.second > b.second; });
				}
			}
			else
			{
				// Ran out of candidates while attempting to fill the last bits of a meshlet.
				if (candidates.empty())
				{
					candidateCheck.clear();
					curr = nullptr;

				}
			}

			// Ran out of candidates; add a new seed candidate to start the next meshlet.
			if (candidates.empty())
			{
				while (triIndex < endIndex && checklist[triIndex - startIndex])
					++triIndex;

				if (triIndex == endIndex)
					break;

				candidates.push_back(std::make_pair(triIndex, 0.0f));
				candidateCheck.insert(triIndex);
			}
		}

		return 0;
	}


	//---------------------------------------------------------------------------------
	// Utilities
	//---------------------------------------------------------------------------------
	struct vertexHashEntry
	{
		Vector3             v;
		uint32_t            index;
		vertexHashEntry *   next;
	};

	struct edgeHashEntry
	{
		uint32_t        v1;
		uint32_t        v2;
		uint32_t        vOther;
		uint32_t        face;
		edgeHashEntry * next;
	};

	// <algorithm> std::make_heap doesn't match D3DX10 so we use the same algorithm here
	void MakeXHeap(uint32_t *index, const Vector3* positions, size_t nVerts) noexcept
	{
		for (size_t vert = 0; vert < nVerts; ++vert)
		{
			index[vert] = static_cast<uint32_t>(vert);
		}

		if (nVerts > 1)
		{
			// Create the heap
			uint32_t iulLim = uint32_t(nVerts);

			for (uint32_t vert = uint32_t(nVerts >> 1); --vert != uint32_t(-1); )
			{
				// Percolate down
				uint32_t iulI = vert;
				uint32_t iulJ = vert + vert + 1;
				const uint32_t ulT = index[iulI];

				while (iulJ < iulLim)
				{
					uint32_t ulJ = index[iulJ];

					if (iulJ + 1 < iulLim)
					{
						const uint32_t ulJ1 = index[iulJ + 1];
						if (positions[ulJ1][0] <= positions[ulJ][0])
						{
							iulJ++;
							ulJ = ulJ1;
						}
					}

					if (positions[ulJ][0] > positions[ulT][0])
						break;

					index[iulI] = index[iulJ];
					iulI = iulJ;
					iulJ += iulJ + 1;
				}

				index[iulI] = ulT;
			}

			// Sort the heap
			while (--iulLim != uint32_t(-1))
			{
				const uint32_t ulT = index[iulLim];
				index[iulLim] = index[0];

				// Percolate down
				uint32_t iulI = 0;
				uint32_t iulJ = 1;

				while (iulJ < iulLim)
				{
					uint32_t ulJ = index[iulJ];

					if (iulJ + 1 < iulLim)
					{
						const uint32_t ulJ1 = index[iulJ + 1];
						if (positions[ulJ1][0] <= positions[ulJ][0])
						{
							iulJ++;
							ulJ = ulJ1;
						}
					}

					if (positions[ulJ][0] > positions[ulT][0])
						break;

					index[iulI] = index[iulJ];
					iulI = iulJ;
					iulJ += iulJ + 1;
				}

				assert(iulI < nVerts);
				index[iulI] = ulT;
			}
		}
	}

	//---------------------------------------------------------------------------------
	// PointRep computation
	//---------------------------------------------------------------------------------
	template<class index_t>
	int GeneratePointReps(
		const index_t* indices, size_t nFaces, const Vector3* positions, size_t nVerts, float epsilon,
		uint32_t* pointRep) noexcept
	{
		std::unique_ptr<uint32_t[]> temp(new (std::nothrow) uint32_t[nVerts + nFaces * 3]);
		if (!temp)
			return ENOMEM;

		uint32_t* vertexToCorner = temp.get();
		uint32_t* vertexCornerList = temp.get() + nVerts;

		memset(vertexToCorner, 0xff, sizeof(uint32_t) * nVerts);
		memset(vertexCornerList, 0xff, sizeof(uint32_t) * nFaces * 3);

		// build initial lists and validate indices
		for (size_t j = 0; j < (nFaces * 3); ++j)
		{
			index_t k = indices[j];
			if (k == index_t(-1))
				continue;

			if (k >= nVerts)
				return ERANGE;

			vertexCornerList[j] = vertexToCorner[k];
			vertexToCorner[k] = uint32_t(j);
		}

		if (epsilon == 0.f)
		{
			auto hashSize = std::max<size_t>(nVerts / 3, 1);

			std::unique_ptr<vertexHashEntry*[]> hashTable(new (std::nothrow) vertexHashEntry*[hashSize]);
			if (!hashTable)
				return ENOMEM;

			memset(hashTable.get(), 0, sizeof(vertexHashEntry*) * hashSize);

			std::unique_ptr<vertexHashEntry[]> hashEntries(new (std::nothrow) vertexHashEntry[nVerts]);
			if (!hashEntries)
				return ENOMEM;

			uint32_t freeEntry = 0;

			for (size_t vert = 0; vert < nVerts; ++vert)
			{
				auto px = std::bit_cast<std::uint32_t>(positions[vert][0]);
				auto py = std::bit_cast<std::uint32_t>(positions[vert][1]);
				auto pz = std::bit_cast<std::uint32_t>(positions[vert][2]);
				const uint32_t hashKey = (px + py + pz) % uint32_t(hashSize);

				uint32_t found = UNUSED32;

				for (auto current = hashTable[hashKey]; current != nullptr; current = current->next)
				{
					if (current->v[0] == positions[vert][0]
						&& current->v[1] == positions[vert][1]
						&& current->v[2] == positions[vert][2])
					{
						uint32_t head = vertexToCorner[vert];

						bool ispresent = false;

						while (head != UNUSED32)
						{
							const uint32_t face = head / 3;
							assert(face < nFaces);

							assert((indices[face * 3] == vert) || (indices[face * 3 + 1] == vert) || (indices[face * 3 + 2] == vert));

							if ((indices[face * 3] == current->index) || (indices[face * 3 + 1] == current->index) || (indices[face * 3 + 2] == current->index))
							{
								ispresent = true;
								break;
							}

							head = vertexCornerList[head];
						}

						if (!ispresent)
						{
							found = current->index;
							break;
						}
					}
				}

				if (found != UNUSED32)
				{
					pointRep[vert] = found;
				}
				else
				{
					assert(freeEntry < nVerts);

					auto newEntry = &hashEntries[freeEntry];
					++freeEntry;

					newEntry->v = positions[vert];
					newEntry->index = uint32_t(vert);
					newEntry->next = hashTable[hashKey];
					hashTable[hashKey] = newEntry;

					pointRep[vert] = uint32_t(vert);
				}
			}

			assert(freeEntry <= nVerts);

			return 0;
		}
		else
		{
			std::unique_ptr<uint32_t[]> xorder(new (std::nothrow) uint32_t[nVerts]);
			if (!xorder)
				return ENOMEM;

			// order in descending order
			MakeXHeap(xorder.get(), positions, nVerts);

			memset(pointRep, 0xff, sizeof(uint32_t) * nVerts);

			FloatVector4  vepsilon(epsilon * epsilon);

			uint32_t head = 0;
			uint32_t tail = 0;

			while (tail < nVerts)
			{
				// move head until just out of epsilon
				while ((head < nVerts) && ((positions[tail][0] - positions[head][0]) <= epsilon))
				{
					++head;
				}

				// check new tail against all points up to the head
				uint32_t tailIndex = xorder[tail];
				assert(tailIndex < nVerts);
				if (pointRep[tailIndex] == UNUSED32)
				{
					pointRep[tailIndex] = tailIndex;

					FloatVector4  outer(positions[tailIndex]);

					for (uint32_t current = tail + 1; current < head; ++current)
					{
						uint32_t curIndex = xorder[current];
						assert(curIndex < nVerts);

						// if the point is already assigned, ignore it
						if (pointRep[curIndex] == UNUSED32)
						{
							FloatVector4  inner(positions[curIndex]);

							float   diff = (inner - outer).dotProduct3(inner - outer);

							if (diff < vepsilon[0])
							{
								uint32_t headvc = vertexToCorner[tailIndex];

								bool ispresent = false;

								while (headvc != UNUSED32)
								{
									const uint32_t face = headvc / 3;
									assert(face < nFaces);

									assert((indices[face * 3] == tailIndex) || (indices[face * 3 + 1] == tailIndex) || (indices[face * 3 + 2] == tailIndex));

									if ((indices[face * 3] == curIndex) || (indices[face * 3 + 1] == curIndex) || (indices[face * 3 + 2] == curIndex))
									{
										ispresent = true;
										break;
									}

									headvc = vertexCornerList[headvc];
								}

								if (!ispresent)
								{
									pointRep[curIndex] = tailIndex;
								}
							}
						}
					}
				}

				++tail;
			}

			return 0;
		}
	}


	//---------------------------------------------------------------------------------
	// Convert PointRep to Adjacency
	//---------------------------------------------------------------------------------
	template<class index_t>
	int ConvertPointRepsToAdjacencyImpl(
		const index_t* indices, size_t nFaces, const Vector3* positions, size_t nVerts, const uint32_t* pointRep,
		uint32_t* adjacency) noexcept
	{
		auto hashSize = std::max<size_t>(nVerts / 3, 1);

		std::unique_ptr<edgeHashEntry*[]> hashTable(new (std::nothrow) edgeHashEntry*[hashSize]);
		if (!hashTable)
			return ENOMEM;

		memset(hashTable.get(), 0, sizeof(edgeHashEntry*) * hashSize);

		std::unique_ptr<edgeHashEntry[]> hashEntries(new (std::nothrow) edgeHashEntry[3 * nFaces]);
		if (!hashEntries)
			return ENOMEM;

		uint32_t freeEntry = 0;

		// add face edges to hash table and validate indices
		for (size_t face = 0; face < nFaces; ++face)
		{
			index_t i0 = indices[face * 3];
			index_t i1 = indices[face * 3 + 1];
			index_t i2 = indices[face * 3 + 2];

			if (i0 == index_t(-1) || i1 == index_t(-1) || i2 == index_t(-1))
				continue;

			if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts)
				return ERANGE;

			const uint32_t v1 = pointRep[i0];
			const uint32_t v2 = pointRep[i1];
			const uint32_t v3 = pointRep[i2];

			// filter out degenerate triangles
			if (v1 == v2 || v1 == v3 || v2 == v3)
				continue;

			for (uint32_t point = 0; point < 3; ++point)
			{
				const uint32_t va = pointRep[indices[face * 3 + point]];
				const uint32_t vb = pointRep[indices[face * 3 + ((point + 1) % 3)]];
				const uint32_t vOther = pointRep[indices[face * 3 + ((point + 2) % 3)]];

				const uint32_t hashKey = va % hashSize;

				assert(freeEntry < (3 * nFaces));

				auto newEntry = &hashEntries[freeEntry];
				++freeEntry;

				newEntry->v1 = va;
				newEntry->v2 = vb;
				newEntry->vOther = vOther;
				newEntry->face = uint32_t(face);
				newEntry->next = hashTable[hashKey];
				hashTable[hashKey] = newEntry;
			}
		}

		assert(freeEntry <= (3 * nFaces));

		memset(adjacency, 0xff, sizeof(uint32_t) * nFaces * 3);

		for (size_t face = 0; face < nFaces; ++face)
		{
			index_t i0 = indices[face * 3];
			index_t i1 = indices[face * 3 + 1];
			index_t i2 = indices[face * 3 + 2];

			// filter out unused triangles
			if (i0 == index_t(-1)
				|| i1 == index_t(-1)
				|| i2 == index_t(-1))
				continue;

			assert(i0 < nVerts);
			assert(i1 < nVerts);
			assert(i2 < nVerts);

			const uint32_t v1 = pointRep[i0];
			const uint32_t v2 = pointRep[i1];
			const uint32_t v3 = pointRep[i2];

			// filter out degenerate triangles
			if (v1 == v2 || v1 == v3 || v2 == v3)
				continue;

			for (uint32_t point = 0; point < 3; ++point)
			{
				if (adjacency[face * 3 + point] != UNUSED32)
					continue;

				// see if edge already entered, if not then enter it
				const uint32_t va = pointRep[indices[face * 3 + ((point + 1) % 3)]];
				const uint32_t vb = pointRep[indices[face * 3 + point]];
				const uint32_t vOther = pointRep[indices[face * 3 + ((point + 2) % 3)]];

				const uint32_t hashKey = va % hashSize;

				edgeHashEntry* current = hashTable[hashKey];
				edgeHashEntry* prev = nullptr;

				uint32_t foundFace = UNUSED32;

				while (current != nullptr)
				{
					if ((current->v2 == vb) && (current->v1 == va))
					{
						foundFace = current->face;
						break;
					}

					prev = current;
					current = current->next;
				}

				edgeHashEntry* found = current;
				edgeHashEntry* foundPrev = prev;

				float bestDiff = -2.f;

				// Scan for additional matches
				if (current)
				{
					prev = current;
					current = current->next;

					// find 'better' match
					while (current != nullptr)
					{
						if ((current->v2 == vb) && (current->v1 == va))
						{
							FloatVector4  pB1(positions[vb]);
							FloatVector4  pB2(positions[va]);
							FloatVector4  pB3(positions[vOther]);

							FloatVector4  v12 = pB1 - pB2;
							FloatVector4  v13 = pB1 - pB3;

							FloatVector4  bnormal(NormalizeVector3(v12.crossProduct3(v13)));

							if (bestDiff == -2.f)
							{
								FloatVector4  pA1(positions[found->v1]);
								FloatVector4  pA2(positions[found->v2]);
								FloatVector4  pA3(positions[found->vOther]);

								v12 = pA1 - pA2;
								v13 = pA1 - pA3;

								FloatVector4  anormal(NormalizeVector3(v12.crossProduct3(v13)));

								bestDiff = anormal.dotProduct3(bnormal);
							}

							FloatVector4  pA1(positions[current->v1]);
							FloatVector4  pA2(positions[current->v2]);
							FloatVector4  pA3(positions[current->vOther]);

							v12 = pA1 - pA2;
							v13 = pA1 - pA3;

							FloatVector4  anormal(NormalizeVector3(v12.crossProduct3(v13)));

							const float diff = anormal.dotProduct3(bnormal);

							// if face normals are closer, use new match
							if (diff > bestDiff)
							{
								found = current;
								foundPrev = prev;
								foundFace = current->face;
								bestDiff = diff;
							}
						}

						prev = current;
						current = current->next;
					}
				}

				if (foundFace != UNUSED32)
				{
					assert(found != nullptr);

					// remove found face from hash table
					if (foundPrev != nullptr)
					{
						foundPrev->next = found->next;
					}
					else
					{
						hashTable[hashKey] = found->next;
					}

					assert(adjacency[face * 3 + point] == UNUSED32);
					adjacency[face * 3 + point] = foundFace;

					// Check for other edge
					const uint32_t hashKey2 = vb % hashSize;

					current = hashTable[hashKey2];
					prev = nullptr;

					while (current != nullptr)
					{
						if ((current->face == uint32_t(face)) && (current->v2 == va) && (current->v1 == vb))
						{
							// trim edge from hash table
							if (prev != nullptr)
							{
								prev->next = current->next;
							}
							else
							{
								hashTable[hashKey2] = current->next;
							}
							break;
						}

						prev = current;
						current = current->next;
					}

					// mark neighbor to point back
					bool linked = false;

					for (uint32_t point2 = 0; point2 < point; ++point2)
					{
						if (foundFace == adjacency[face * 3 + point2])
						{
							linked = true;
							adjacency[face * 3 + point] = UNUSED32;
							break;
						}
					}

					if (!linked)
					{
						uint32_t point2 = 0;
						for (; point2 < 3; ++point2)
						{
							index_t k = indices[foundFace * 3 + point2];
							if (k == index_t(-1))
								continue;

							assert(k < nVerts);

							if (pointRep[k] == va)
								break;
						}

						if (point2 < 3)
						{
						#ifndef NDEBUG
							uint32_t testPoint = indices[foundFace * 3 + ((point2 + 1) % 3)];
							testPoint = pointRep[testPoint];
							assert(testPoint == vb);
						#endif
							assert(adjacency[foundFace * 3 + point2] == UNUSED32);

							// update neighbor to point back to this face match edge
							adjacency[foundFace * 3 + point2] = uint32_t(face);
						}
					}
				}
			}
		}

		return 0;
	}

	//-------------------------------------------------------------------------------------
	template<typename T>
	int GenerateAdjacencyAndPointReps(
		const T* indices, size_t nFaces, const Vector3* positions, size_t nVerts, float epsilon,
		uint32_t* pointRep, uint32_t* adjacency)
	{
		if (!indices || !nFaces || !positions || !nVerts)
			return EINVAL;

		if (!pointRep && !adjacency)
			return EINVAL;

		if ((nVerts - 1) != size_t(T(nVerts - 1)))
			return EINVAL;

		if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
			return ERANGE;

		std::unique_ptr<uint32_t[]> temp;
		if (!pointRep)
		{
			temp.reset(new (std::nothrow) uint32_t[nVerts]);
			if (!temp)
				return ENOMEM;

			pointRep = temp.get();
		}

		int hr = GeneratePointReps<T>(indices, nFaces, positions, nVerts, epsilon, pointRep);
		if (hr != 0)
			return hr;

		if (!adjacency)
			return 0;

		return ConvertPointRepsToAdjacencyImpl<T>(indices, nFaces, positions, nVerts, pointRep, adjacency);
	}


	//---------------------------------------------------------------------------------
	// Builds meshlets for a list of index subsets and organizes their data into
	// corresponding output buffers.
	//---------------------------------------------------------------------------------
	template <typename T>
	int ComputeMeshletsInternal(
		const T* indices, size_t nFaces, const Vector3* positions, size_t nVerts,
		const std::pair<size_t, size_t>* subsets, size_t nSubsets, const uint32_t* adjacency,
		std::vector<Meshlet>& meshlets, std::vector<T>& primitiveIndices,
		std::pair<size_t, size_t>* meshletSubsets,
		size_t maxVerts, size_t maxPrims)
	{
		if (!indices || !positions || !subsets || !meshletSubsets)
			return EINVAL;

		// Validate the meshlet vertex & primitive sizes
		if (maxVerts < MESHLET_MINIMUM_SIZE || maxVerts > MESHLET_MAXIMUM_SIZE)
			return EINVAL;

		if (maxPrims < MESHLET_MINIMUM_SIZE || maxPrims > MESHLET_MAXIMUM_SIZE)
			return EINVAL;

		if (nFaces == 0 || nVerts == 0 || nSubsets == 0)
			return EINVAL;

		// Auto-generate adjacency data if not provided.
		std::unique_ptr<uint32_t[]> generatedAdj;
		if (!adjacency)
		{
			generatedAdj.reset(new (std::nothrow) uint32_t[nFaces * 3]);
			if (!generatedAdj)
				return ENOMEM;

			int hr = GenerateAdjacencyAndPointReps(indices, nFaces, positions, nVerts, 0.0f, nullptr, generatedAdj.get());
			if (hr != 0)
			{
				return hr;
			}

			adjacency = generatedAdj.get();
		}

		// Now start generating meshlets
		size_t  uniqueVertexIndexCount = 0;
		size_t  primitiveIndexCount = 0;
		for (size_t i = 0; i < nSubsets; ++i)
		{
			auto& s = subsets[i];

			if ((s.first + s.second) > nFaces)
			{
				return ERANGE;
			}

			std::vector<InlineMeshlet<T>> newMeshlets;
			int hr = Meshletize(maxVerts, maxPrims, indices, nFaces, positions, nVerts, s, adjacency, newMeshlets);
			if (hr != 0)
			{
				return hr;
			}

			meshletSubsets[i] = std::make_pair(meshlets.size(), newMeshlets.size());

			// Resize the meshlet output array to hold the newly formed meshlets.
			const size_t meshletCount = meshlets.size();
			meshlets.resize(meshletCount + newMeshlets.size());

			Meshlet* dest = &meshlets[meshletCount];
			for (auto& m : newMeshlets)
			{
				dest->VertOffset = static_cast<uint32_t>(uniqueVertexIndexCount);
				dest->VertCount = static_cast<uint32_t>(m.UniqueVertexIndices.size());

				dest->PrimOffset = static_cast<uint32_t>(primitiveIndexCount);
				dest->PrimCount = static_cast<uint32_t>(m.PrimitiveIndices.size());

				uniqueVertexIndexCount += m.UniqueVertexIndices.size();
				primitiveIndexCount += m.PrimitiveIndices.size();

				++dest;
			}

			// Copy data from the freshly built meshlets into the output buffers.
			for (auto& m : newMeshlets)
			{
				for (size_t j = 0; j < m.PrimitiveIndices.size(); j++)
				{
					primitiveIndices.push_back(T(m.UniqueVertexIndices[m.PrimitiveIndices[j].i0]));
					primitiveIndices.push_back(T(m.UniqueVertexIndices[m.PrimitiveIndices[j].i1]));
					primitiveIndices.push_back(T(m.UniqueVertexIndices[m.PrimitiveIndices[j].i2]));
				}
			}
		}

		return 0;
	}


	//=====================================================================================
	// Entry-points
	//=====================================================================================

	//-------------------------------------------------------------------------------------
	int ComputeMeshlets(
		const Triangle* triangles, size_t nFaces, const Vector3* positions, size_t nVerts,
		std::vector<Meshlet>& meshlets, std::vector<uint16_t>& primitiveIndices,
		size_t maxVerts, size_t maxPrims)
	{
		const std::pair<size_t, size_t> s = { 0, nFaces };
		std::pair<size_t, size_t> subset;
		std::vector<std::uint16_t>  indices(nFaces * 3);
		for (size_t i = 0; i < nFaces; i++)
		{
			indices[i * 3] = triangles[i][0];
			indices[i * 3 + 1] = triangles[i][1];
			indices[i * 3 + 2] = triangles[i][2];
		}

		return ComputeMeshletsInternal<uint16_t>(
			indices.data(), nFaces,
			positions, nVerts,
			&s, 1u,
			nullptr,
			meshlets,
			primitiveIndices,
			&subset,
			maxVerts, maxPrims);
	}

}	// namespace DirectX
