/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "sfcube.hpp"
#include "filebuf.hpp"

#include <thread>

// face 0: E,      -X = up,   +X = down, -Y = N,    +Y = S
// face 1: W,      -X = down, +X = up,   -Y = N,    +Y = S
// face 2: N,      -X = W,    +X = E,    -Y = down, +Y = up
// face 3: S,      -X = W,    +X = E,    -Y = up,   +Y = down
// face 4: top,    -X = W,    +X = E,    -Y = N,    +Y = S
// face 5: bottom, -X = E,    +X = W,    -Y = N,    +Y = S

FloatVector4 SFCubeMapFilter::convertCoord(int x, int y, int w, int n)
{
	FloatVector4	v(0.0f);
	switch ( n ) {
		case 0:
			v[0] = float(w);
			v[1] = float(w - (y << 1));
			v[2] = float(w - (x << 1));
			v += FloatVector4(-1.0f, -1.0f, -1.0f, 0.0f);
			break;
		case 1:
			v[0] = float(-w);
			v[1] = float(w - (y << 1));
			v[2] = float((x << 1) - w);
			v += FloatVector4(1.0f, -1.0f, 1.0f, 0.0f);
			break;
		case 2:
			v[0] = float((x << 1) - w);
			v[1] = float(w);
			v[2] = float((y << 1) - w);
			v += FloatVector4(1.0f, -1.0f, 1.0f, 0.0f);
			break;
		case 3:
			v[0] = float((x << 1) - w);
			v[1] = float(-w);
			v[2] = float(w - (y << 1));
			v += FloatVector4(1.0f, 1.0f, -1.0f, 0.0f);
			break;
		case 4:
			v[0] = float((x << 1) - w);
			v[1] = float(w - (y << 1));
			v[2] = float(w);
			v += FloatVector4(1.0f, -1.0f, -1.0f, 0.0f);
			break;
		case 5:
			v[0] = float(w - (x << 1));
			v[1] = float(w - (y << 1));
			v[2] = float(-w);
			v += FloatVector4(-1.0f, -1.0f, 1.0f, 0.0f);
			break;
	}
	// normalize vector
	float	scale = 1.0f / float(std::sqrt(v.dotProduct3(v)));
	// v[3] = scale factor to account for variable angular resolution
	v[3] = float(w);
	v *= scale;
	return v;
}

void SFCubeMapFilter::processImage_Copy(
	unsigned char * outBufP, int w, int h, int y0, int y1)
{
	for (int n = 0; n < 6; n++) {
		for (int y = y0; y < y1; y++) {
			unsigned char *	p =
				outBufP + (size_t(y * w) * sizeof(std::uint32_t));
			for (int x = 0; x < w; x++, p = p + 4) {
				FloatVector4	c(inBuf[(n * h + y) * w + x]);
				std::uint32_t	tmp = std::uint32_t(c.srgbCompress());
				p[0] = (unsigned char) (tmp & 0xFF);
				p[1] = (unsigned char) ((tmp >> 8) & 0xFF);
				p[2] = (unsigned char) ((tmp >> 16) & 0xFF);
				p[3] = 0xFF;
			}
		}
		outBufP = outBufP + faceDataSize;
	}
}

void SFCubeMapFilter::processImage_Diffuse(
	unsigned char * outBufP, int w, int h, int y0, int y1)
{
	for (int n = 0; n < 6; n++) {
		for (int y = y0; y < y1; y++) {
			unsigned char *	p =
				outBufP + (size_t(y * w) * sizeof(std::uint32_t));
			for (int x = 0; x < w; x++, p = p + 4) {
				// v1 = normal vector
				FloatVector4	v1(cubeCoordTable[(n * h + y) * w + x]);
				FloatVector4	c(0.0f);
				float	totalWeight = 0.0f;
				for (std::vector< FloatVector4 >::const_iterator
						i = cubeCoordTable.begin(); i != cubeCoordTable.end();
						i++) {
					// v2 = light vector
					FloatVector4	v2(*i);
					float	weight = v2.dotProduct3(v1);
					if (weight > 0.0f) {
						weight *= v2[3];
						c += (inBuf[i - cubeCoordTable.begin()] * weight);
						totalWeight += weight;
					}
				}
				c /= totalWeight;
				std::uint32_t	tmp = std::uint32_t(c.srgbCompress());
				p[0] = (unsigned char) (tmp & 0xFF);
				p[1] = (unsigned char) ((tmp >> 8) & 0xFF);
				p[2] = (unsigned char) ((tmp >> 16) & 0xFF);
				p[3] = 0xFF;
			}
		}
		outBufP = outBufP + faceDataSize;
	}
}

void SFCubeMapFilter::processImage_Specular(
	unsigned char * outBufP, int w, int h, int y0, int y1, float roughness)
{
	float	a = roughness * roughness;
	float	a2 = a * a;
	for (int n = 0; n < 6; n++) {
		for (int y = y0; y < y1; y++) {
			unsigned char *	p =
				outBufP + (size_t(y * w) * sizeof(std::uint32_t));
			// w must be a multiple of 4
			for (int x = 0; x < w; x++, p = p + 4) {
				std::vector< FloatVector4 >::const_iterator	i =
					cubeCoordTable.begin() + ((n * h + y) * w + (x & ~3));
				// v1 = reflected view vector (R), assume V = N = R
				FloatVector4	v1x(i[0][x & 3]);
				FloatVector4	v1y(i[1][x & 3]);
				FloatVector4	v1z(i[2][x & 3]);
				FloatVector4	c(0.0f);
				FloatVector4	totalWeight(0.0f);
				for (i = cubeCoordTable.begin();
					 (i + 4) <= cubeCoordTable.end(); i = i + 4) {
					// v2 = light vector
					FloatVector4	v2x(i[0]);
					FloatVector4	v2y(i[1]);
					FloatVector4	v2z(i[2]);
					FloatVector4	v2w(i[3]);
					// d = N·L = R·L = 2.0 * N·H * N·H - 1.0
					FloatVector4	d = (v1x * v2x) + (v1y * v2y) + (v1z * v2z);
					d.maxValues(FloatVector4(0.0f));
					FloatVector4	g1 = d;
					// g2 = geometry function denominator * 2.0 (a = k * 2.0)
					FloatVector4	g2 = d * (2.0f - a) + a;
					// D denominator = (N·H * N·H * (a2 - 1.0) + 1.0)² * 4.0
					d = (d + 1.0f) * (a2 - 1.0f) + 2.0f;
					FloatVector4	weight = g1 * v2w / (g2 * d * d);
					c += (inBuf[i - cubeCoordTable.begin()] * weight[0]);
					c += (inBuf[(i - cubeCoordTable.begin()) + 1] * weight[1]);
					c += (inBuf[(i - cubeCoordTable.begin()) + 2] * weight[2]);
					c += (inBuf[(i - cubeCoordTable.begin()) + 3] * weight[3]);
					totalWeight += weight;
				}
				c /= totalWeight.dotProduct(FloatVector4(1.0f));
				std::uint32_t	tmp = std::uint32_t(c.srgbCompress());
				p[0] = (unsigned char) (tmp & 0xFF);
				p[1] = (unsigned char) ((tmp >> 8) & 0xFF);
				p[2] = (unsigned char) ((tmp >> 16) & 0xFF);
				p[3] = 0xFF;
			}
		}
		outBufP = outBufP + faceDataSize;
	}
}

void SFCubeMapFilter::threadFunction(
	SFCubeMapFilter * p, unsigned char * outBufP,
	int w, int h, int m, int maxMip, int y0, int y1)
{
	if (m == 0) {
		p->processImage_Copy(outBufP, w, h, y0, y1);
	} else if (m < (maxMip - 2)) {
		float	smoothness = float((maxMip - 3) - m) / float(maxMip - 3);
		float	roughness = 1.0f - float(std::sqrt(smoothness));
		p->processImage_Specular(outBufP, w, h, y0, y1, roughness);
	} else {
		p->processImage_Diffuse(outBufP, w, h, y0, y1);
	}
}

SFCubeMapFilter::SFCubeMapFilter()
{
}

SFCubeMapFilter::~SFCubeMapFilter()
{
}

size_t SFCubeMapFilter::convertImage(unsigned char * buf, size_t bufSize)
{
	if (bufSize < 148)
		return bufSize;
	size_t	w0 = FileBuffer::readUInt32Fast(buf + 16);
	size_t	h0 = FileBuffer::readUInt32Fast(buf + 12);
	if (FileBuffer::readUInt32Fast(buf) != 0x20534444 ||	// "DDS "
		FileBuffer::readUInt32Fast(buf + 84) != 0x30315844 ||	// "DX10"
		w0 != h0 || w0 < width || (w0 & (w0 - 1)) ||
		FileBuffer::readUInt32Fast(buf + 128) != dxgiFormat) {
		return bufSize;
	}

	size_t	sizeRequired1 = 0;
	size_t	sizeRequired2 = 0;
	int	mipCnt = 0;
	int	maxMip = -1;
	faceDataSize = 0;
	{
		size_t	w = w0;
		size_t	h = h0;
		do {
			w = w + size_t(!w);
			h = h + size_t(!h);
			if (!mipCnt)
				sizeRequired1 += (w * h);
			sizeRequired2 += (w * h);
			if (w <= width && h <= height) {
				faceDataSize += (w * h);
				maxMip++;
			}
			mipCnt++;
			w = w >> 1;
			h = h >> 1;
		} while (w | h);
	}
	sizeRequired1 = sizeRequired1 * sizeof(std::uint64_t) * 6 + 148;
	sizeRequired2 = sizeRequired2 * sizeof(std::uint64_t) * 6 + 148;
	if (bufSize != sizeRequired1 && bufSize != sizeRequired2)
		return bufSize;
	faceDataSize = faceDataSize * sizeof(std::uint32_t);

	inBuf.resize(w0 * h0 * 6, FloatVector4(0.0f));
	FloatVector4	scale(0.0f);
	for (int n = 0; n < 6; n++) {
		for (int y = 0; y < int(h0); y++) {
			for (int x = 0; x < int(w0); x++) {
				size_t	i = size_t(y) * w0 + size_t(x);
				size_t	j = size_t(n) * w0 * h0 + i;
				i = i * sizeof(std::uint64_t);
				i = i + (size_t(n) * ((bufSize - 148) / 6)) + 148;
				std::uint64_t	tmp = FileBuffer::readUInt64Fast(buf + i);
				FloatVector4	c(FloatVector4::convertFloat16(tmp));
				c.maxValues(FloatVector4(0.0f));
				c.minValues(FloatVector4(65536.0f));
				inBuf[j] = c;
				scale += c;
			}
		}
	}
	// normalize
	scale[0] = scale[0] + scale[1] + scale[2];
	scale[0] = (15.0f / 3.0f) * scale[0] / float(int(inBuf.size()));
	scale = FloatVector4(1.0f / std::min(std::max(scale[0], 1.0f), 65536.0f));
	for (size_t i = 0; i < inBuf.size(); i++)
		inBuf[i] = inBuf[i] * scale;

	unsigned char *	outBufP = buf + 148;
	int	threadCnt = int(std::thread::hardware_concurrency());
	threadCnt = std::min< int >(std::max< int >(threadCnt, 1), 16);
	std::thread *	threads[16];
	for (int i = 0; i < 16; i++)
		threads[i] = nullptr;
	for (int m = 0; m < mipCnt; m++) {
		int	w = int(w0 >> m);
		int	h = int(h0 >> m);
		w = w + int(w < 1);
		h = h + int(h < 1);
		if (w <= width && h <= height) {
			cubeCoordTable.resize(size_t(w * h) * 6, FloatVector4(0.0f));
			for (int n = 0; n < 6; n++) {
				for (int y = 0; y < h; y++) {
					for (int x = 0; x < w; x++) {
						cubeCoordTable[(n * h + y) * w + x] =
							convertCoord(x, y, w, n);
					}
				}
			}
			if (m > (mipCnt - 9) && m < (mipCnt - 3)) {
				// specular: reorder data for more efficient use of SIMD
				for (size_t i = 0; (i + 4) <= cubeCoordTable.size(); i += 4) {
					FloatVector4	v0 = cubeCoordTable[i];
					FloatVector4	v1 = cubeCoordTable[i + 1];
					FloatVector4	v2 = cubeCoordTable[i + 2];
					FloatVector4	v3 = cubeCoordTable[i + 3];
					cubeCoordTable[i] =
						FloatVector4(v0[0], v1[0], v2[0], v3[0]);
					cubeCoordTable[i + 1] =
						FloatVector4(v0[1], v1[1], v2[1], v3[1]);
					cubeCoordTable[i + 2] =
						FloatVector4(v0[2], v1[2], v2[2], v3[2]);
					cubeCoordTable[i + 3] =
						FloatVector4(v0[3], v1[3], v2[3], v3[3]);
				}
			}
			try {
				if (h < 16)
					threadCnt = 1;
				else if ((h >> 3) < threadCnt)
					threadCnt = h >> 3;
				for (int i = 0; i < threadCnt; i++) {
					threads[i] =
						new std::thread(threadFunction, this, outBufP,
										w, h, m + maxMip - (mipCnt - 1), maxMip,
										i * h / threadCnt,
										(i + 1) * h / threadCnt);
				}
				for (int i = 0; i < threadCnt; i++) {
					if (threads[i]) {
						threads[i]->join();
						delete threads[i];
						threads[i] = nullptr;
					}
				}
			} catch (...) {
				for (int i = 0; i < 16; i++) {
					if (threads[i]) {
						threads[i]->join();
						delete threads[i];
					}
				}
				throw;
			}
		}
		// calculate mipmaps
		int	w2 = (w + 1) >> 1;
		int	h2 = (h + 1) >> 1;
		for (int n = 0; n < 6; n++) {
			for (int y = 0; y < h2; y++) {
				for (int x = 0; x < w2; x++) {
					int	x0 = x << 1;
					int	x1 = x0 + (int(x0 + 1) < w);
					int	y0 = y << 1;
					int	y1 = y0 + (int(y0 + 1) < h);
					FloatVector4	c0 = inBuf[(n * w * h) + (y0 * w + x0)];
					FloatVector4	c1 = inBuf[(n * w * h) + (y0 * w + x1)];
					FloatVector4	c2 = inBuf[(n * w * h) + (y1 * w + x0)];
					FloatVector4	c3 = inBuf[(n * w * h) + (y1 * w + x1)];
					c0 = (c0 + c1 + c2 + c3) * 0.25f;
					inBuf[(n * w2 * h2) + (y * w2 + x)] = c0;
				}
			}
		}
		inBuf.resize(size_t(w2 * h2) * 6);
		outBufP = outBufP + (size_t(w * h) * sizeof(std::uint32_t));
	}

	buf[10] = buf[10] | 0x02;	// DDSD_MIPMAPCOUNT
	buf[12] = (unsigned char) (height & 0xFF);
	buf[13] = (unsigned char) (height >> 8);
	buf[16] = (unsigned char) (width & 0xFF);
	buf[17] = (unsigned char) (width >> 8);
	buf[20] = (unsigned char) ((sizeof(std::uint32_t) * width) & 0xFF);
	buf[21] = (unsigned char) ((sizeof(std::uint32_t) * width) >> 8);
	buf[28] = (unsigned char) (maxMip + 1);
	buf[108] = buf[108] | 0x08;	// DDSCAPS_COMPLEX
	buf[113] = buf[113] | 0xFE;	// DDSCAPS2_CUBEMAP*
	buf[128] = 0x1D;	// DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
	size_t	newSize = faceDataSize * 6 + 148;
	return newSize;
}

SFCubeMapCache::SFCubeMapCache()
{
}

SFCubeMapCache::~SFCubeMapCache()
{
}

size_t SFCubeMapCache::convertImage(unsigned char * buf, size_t bufSize)
{
	std::uint32_t	h = 0xFFFFFFFFU;
	size_t	i = 0;
	for ( ; (i + 8) <= bufSize; i = i + 8) {
		std::uint64_t	tmp = FileBuffer::readUInt64Fast(buf + i);
		hashFunctionCRC32C< std::uint64_t >(h, tmp);
	}
	for ( ; i < bufSize; i++)
		hashFunctionCRC32C< unsigned char >(h, buf[i]);
	std::uint64_t	k = (std::uint64_t(bufSize) << 32) | h;
	std::vector< unsigned char >&	v = cachedTextures[k];
	if (v.size() > 0) {
		std::memcpy(buf, v.data(), v.size());
		return v.size();
	}
	SFCubeMapFilter	cubeMapFilter;
	size_t	newSize = cubeMapFilter.convertImage(buf, bufSize);
	if (newSize && newSize < bufSize) {
		v.resize(newSize);
		std::memcpy(v.data(), buf, newSize);
	}
	return newSize;
}

