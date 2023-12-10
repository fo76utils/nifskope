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

#ifndef SFCUBE_HPP_INCLUDED
#define SFCUBE_HPP_INCLUDED

#include "common.hpp"
#include "fp32vec4.hpp"

class SFCubeMapFilter {
 protected:
	enum {
		width = 256,
		height = 256,
		dxgiFormat = 0x0A		// DXGI_FORMAT_R16G16B16A16_FLOAT
	};
	std::vector< FloatVector4 >	inBuf;
	std::vector< FloatVector4 >	cubeCoordTable;
	size_t	faceDataSize;
	static FloatVector4 convertCoord(int x, int y, int w, int n);
	void processImage_Copy(unsigned char * outBufP,
						   int w, int h, int y0, int y1);
	void processImage_Diffuse(unsigned char * outBufP,
							  int w, int h, int y0, int y1);
	void processImage_Specular(unsigned char * outBufP,
							   int w, int h, int y0, int y1, float roughness);
	static void threadFunction(SFCubeMapFilter * p, unsigned char * outBufP,
							   int w, int h, int m, int maxMip, int y0, int y1);
 public:
	SFCubeMapFilter();
	~SFCubeMapFilter();
	// returns the new buffer size
	size_t convertImage(unsigned char * buf, size_t bufSize);
};

class SFCubeMapCache {
 protected:
	std::map< std::uint64_t, std::vector< unsigned char > >	cachedTextures;
 public:
	SFCubeMapCache();
	~SFCubeMapCache();
	size_t convertImage(unsigned char * buf, size_t bufSize);
};

#endif

