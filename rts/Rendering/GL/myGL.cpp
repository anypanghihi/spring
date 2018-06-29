/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <array>
#include <string>
#include <cmath>

#include <SDL.h>
#if (!defined(HEADLESS) && !defined(WIN32) && !defined(__APPLE__))
// need this for glXQueryCurrentRendererIntegerMESA (glxext)
#include <GL/glxew.h>
#endif

#include "myGL.h"
#include "MatrixState.hpp"
#include "VertexArray.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/Matrix44f.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"
#include "System/Config/ConfigHandler.h"
#include "System/FileSystem/FileHandler.h"
#include "System/Platform/MessageBox.h"

#define SDL_BPP(fmt) SDL_BITSPERPIXEL((fmt))

static std::array<CVertexArray, 2> vertexArrays;
static int currentVertexArray = 0;


/******************************************************************************/
/******************************************************************************/

CVertexArray* GetVertexArray()
{
	currentVertexArray += 1;
	currentVertexArray %= vertexArrays.size();
	return &vertexArrays[currentVertexArray];
}

/******************************************************************************/

bool CheckAvailableVideoModes()
{
	// Get available fullscreen/hardware modes
	const int numDisplays = SDL_GetNumVideoDisplays();

	SDL_DisplayMode ddm = {0, 0, 0, 0, nullptr};
	SDL_DisplayMode cdm = {0, 0, 0, 0, nullptr};

	// ddm is virtual, contains all displays in multi-monitor setups
	// for fullscreen windows with non-native resolutions, ddm holds
	// the original screen mode and cdm is the changed mode
	SDL_GetDesktopDisplayMode(0, &ddm);
	SDL_GetCurrentDisplayMode(0, &cdm);

	LOG(
		"[GL::%s] desktop={%ix%ix%ibpp@%iHz} current={%ix%ix%ibpp@%iHz}",
		__func__,
		ddm.w, ddm.h, SDL_BPP(ddm.format), ddm.refresh_rate,
		cdm.w, cdm.h, SDL_BPP(cdm.format), cdm.refresh_rate
	);

	for (int k = 0; k < numDisplays; ++k) {
		const int numModes = SDL_GetNumDisplayModes(k);

		if (numModes <= 0) {
			LOG("\tdisplay=%d bounds=N/A modes=N/A", k + 1);
			continue;
		}

		SDL_DisplayMode cm = {0, 0, 0, 0, nullptr};
		SDL_DisplayMode pm = {0, 0, 0, 0, nullptr};
		SDL_Rect db;
		SDL_GetDisplayBounds(k, &db);

		LOG("\tdisplay=%d modes=%d bounds={x=%d, y=%d, w=%d, h=%d}", k + 1, numModes, db.x, db.y, db.w, db.h);

		for (int i = 0; i < numModes; ++i) {
			SDL_GetDisplayMode(k, i, &cm);

			const float r0 = (cm.w *  9.0f) / cm.h;
			const float r1 = (cm.w * 10.0f) / cm.h;
			const float r2 = (cm.w * 16.0f) / cm.h;

			// skip legacy (3:2, 4:3, 5:4, ...) and weird (10:6, ...) ratios
			if (r0 != 16.0f && r1 != 16.0f && r2 != 25.0f)
				continue;
			// show only the largest refresh-rate and bit-depth per resolution
			if (cm.w == pm.w && cm.h == pm.h && (SDL_BPP(cm.format) < SDL_BPP(pm.format) || cm.refresh_rate < pm.refresh_rate))
				continue;

			LOG("\t\t[%2i] %ix%ix%ibpp@%iHz", int(i + 1), cm.w, cm.h, SDL_BPP(cm.format), cm.refresh_rate);
			pm = cm;
		}
	}

	// we need at least 24bpp or window-creation will fail
	return (SDL_BPP(ddm.format) >= 24);
}



#ifndef HEADLESS
static bool GetVideoMemInfoNV(GLint* memInfo)
{
	#if (defined(GLEW_NVX_gpu_memory_info))
	if (!GLEW_NVX_gpu_memory_info)
		return false;

	glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &memInfo[0]);
	glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &memInfo[1]);
	return true;
	#else
	return false;
	#endif
}

static bool GetVideoMemInfoATI(GLint* memInfo)
{
	#if (defined(GLEW_ATI_meminfo))
	if (!GLEW_ATI_meminfo)
		return false;

	// these are not disjoint, don't sum
	for (uint32_t param: {/*GL_VBO_FREE_MEMORY_ATI,*/ GL_TEXTURE_FREE_MEMORY_ATI/*, GL_RENDERBUFFER_FREE_MEMORY_ATI*/}) {
		glGetIntegerv(param, &memInfo[0]);

		memInfo[4] += (memInfo[0] + memInfo[2]); // total main plus aux. memory free in pool
		memInfo[5] += (memInfo[1] + memInfo[3]); // largest main plus aux. free block in pool
	}

	memInfo[0] = memInfo[4]; // return the VBO/RBO/TEX free sum
	memInfo[1] = memInfo[4]; // sic, just assume total >= free
	return true;
	#else
	return false;
	#endif
}

static bool GetVideoMemInfoMESA(GLint* memInfo)
{
	#if (defined(GLX_MESA_query_renderer))
	if (!GLXEW_MESA_query_renderer)
		return false;

	typedef PFNGLXQUERYCURRENTRENDERERINTEGERMESAPROC QCRIProc;

	static const GLubyte* qcriProcName = (const GLubyte*) "glXQueryCurrentRendererIntegerMESA";
	static const QCRIProc qcriProcAddr = (QCRIProc) glXGetProcAddress(qcriProcName);

	if (qcriProcAddr == nullptr)
		return false;

	// note: unlike the others, this value is returned in megabytes
	qcriProcAddr(GLX_RENDERER_VIDEO_MEMORY_MESA, reinterpret_cast<unsigned int*>(&memInfo[0]));

	memInfo[0] *= 1024;
	memInfo[1] = memInfo[0];
	return true;
	#else
	return false;
	#endif
}
#endif

bool GetAvailableVideoRAM(GLint* memory, const char* glVendor)
{
	#ifdef HEADLESS
	return false;
	#else
	GLint memInfo[4 + 2] = {-1, -1, -1, -1, 0, 0};

	switch (glVendor[0]) {
		case 'N': { if (!GetVideoMemInfoNV  (memInfo)) return false; } break; // "NVIDIA"
		case 'A': { if (!GetVideoMemInfoATI (memInfo)) return false; } break; // "ATI" or "AMD"
		case 'X': { if (!GetVideoMemInfoMESA(memInfo)) return false; } break; // "X.org"
		case 'M': { if (!GetVideoMemInfoMESA(memInfo)) return false; } break; // "Mesa"
		case 'V': { if (!GetVideoMemInfoMESA(memInfo)) return false; } break; // "VMware" (also ships a Mesa variant)
		case 'I': {                                    return false; } break; // "Intel"
		case 'T': {                                    return false; } break; // "Tungsten" (old, acquired by VMware)
		default : {                                    return false; } break;
	}

	// callers assume [0]=total and [1]=free
	memory[0] = std::max(memInfo[0], memInfo[1]);
	memory[1] = std::min(memInfo[0], memInfo[1]);
	return true;
	#endif
}



bool ShowDriverWarning(const char* glVendor, const char* glRenderer)
{
	assert(glVendor != nullptr);
	assert(glRenderer != nullptr);

	const std::string& _glVendor = StringToLower(glVendor);
	// const std::string& _glRenderer = StringToLower(glRenderer);

	// should be unreachable
	// note that checking for Microsoft stubs is no longer required
	// (context-creation will fail if no vendor-specific or pre-GL3
	// drivers are installed)
	if (_glVendor.find("unknown") != std::string::npos)
		return false;

	if (_glVendor.find("vmware") != std::string::npos) {
		const char* msg =
			"Running Spring with virtualized drivers can result in severely degraded "
			"performance and is discouraged. Prefer to use your host operating system.";

		LOG_L(L_WARNING, "%s", msg);
		Platform::MsgBox(msg, "Warning", MBF_EXCL);
		return true;
	}

	return true;
}


/******************************************************************************/

void glSaveTexture(const GLuint textureID, const char* filename)
{
	const GLenum target = GL_TEXTURE_2D;
	GLenum format = GL_RGBA8;
	int sizeX, sizeY;

	int bits = 0;
	{
		glBindTexture(GL_TEXTURE_2D, textureID);

		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sizeX);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sizeY);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, (GLint*)&format);

		GLint _cbits;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH_SIZE, &_cbits); bits += _cbits;
	}
	assert(bits == 32);
	assert(format == GL_RGBA8);

	CBitmap bmp;
	bmp.Alloc(sizeX, sizeY, 4);
	glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp.GetRawMem());
	bmp.Save(filename, false);
}


void glSpringBindTextures(GLuint first, GLsizei count, const GLuint* textures)
{
#ifdef GLEW_ARB_multi_bind
	if (GLEW_ARB_multi_bind) {
		glBindTextures(first, count, textures);
	} else
#endif
	{
		for (int i = 0; i < count; ++i) {
			const GLuint texture = (textures == nullptr) ? 0 : textures[i];
			glActiveTexture(GL_TEXTURE0 + first + i);
			glBindTexture(GL_TEXTURE_2D, texture);
		}
		glActiveTexture(GL_TEXTURE0);

	}
}


void glSpringTexStorage2D(const GLenum target, GLint levels, const GLint internalFormat, const GLsizei width, const GLsizei height)
{
#ifdef GLEW_ARB_texture_storage
	if (levels < 0)
		levels = std::ceil(std::log((float)(std::max(width, height) + 1)));

	if (GLEW_ARB_texture_storage) {
		glTexStorage2D(target, levels, internalFormat, width, height);
	} else
#endif
	{
		GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
		switch (internalFormat) {
			case GL_RGBA8: format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
			case GL_RGB8:  format = GL_RGB;  type = GL_UNSIGNED_BYTE; break;
			default: /*LOG_L(L_ERROR, "[%s] Couldn't detect format type for %i", __FUNCTION__, internalFormat);*/
			break;
		}
		glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, nullptr);
	}
}


void glBuildMipmaps(const GLenum target, GLint internalFormat, const GLsizei width, const GLsizei height, const GLenum format, const GLenum type, const void* data)
{
	if (globalRendering->compressTextures) {
		switch (internalFormat) {
			case 4:
			case GL_RGBA8:
			case GL_RGBA :
				internalFormat = GL_COMPRESSED_RGBA;
			break;

			case 3:
			case GL_RGB8:
			case GL_RGB :
				internalFormat = GL_COMPRESSED_RGB;
			break;

			case GL_LUMINANCE:
				internalFormat = GL_COMPRESSED_LUMINANCE;
			break;
		}
	}

	// create mipmapped texture
	glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
	if (globalRendering->atiHacks) {
		glEnable(target);
		glGenerateMipmap(target);
		glDisable(target);
	} else {
		glGenerateMipmap(target);
	}
}




static void LoadProjMat2D(float l, float r, float b, float t, float n, float f,  bool push) {
	GL::MatrixMode(GL_PROJECTION);

	if (push)
		GL::PushMatrix();

	GL::LoadMatrix(CMatrix44f::ClipControl(globalRendering->supportClipSpaceControl) * CMatrix44f::OrthoProj(l, r, b, t, n, f));
}

static void LoadViewMat2D(bool push) {
	GL::MatrixMode(GL_MODELVIEW);

	if (push)
		GL::PushMatrix();

	GL::LoadIdentity();
}


void glSpringMatrix2dSetupVP(float l, float r, float b, float t, float n, float f,  bool pv, bool pp)
{
	LoadViewMat2D(pv);
	LoadProjMat2D(l, r, b, t, n, f,  pp);
}
void glSpringMatrix2dSetupPV(float l, float r, float b, float t, float n, float f,  bool pv, bool pp)
{
	LoadProjMat2D(l, r, b, t, n, f,  pp);
	LoadViewMat2D(pv);
}

void glSpringMatrix2dResetVP(bool pv, bool pp) {
	GL::MatrixMode(GL_MODELVIEW);
	if (pv)
		GL::PopMatrix();

	GL::MatrixMode(GL_PROJECTION);
	if (pp)
		GL::PopMatrix();
}

void glSpringMatrix2dResetPV(bool pv, bool pp) {
	GL::MatrixMode(GL_PROJECTION);
	if (pp)
		GL::PopMatrix();

	GL::MatrixMode(GL_MODELVIEW);
	if (pv)
		GL::PopMatrix();
}


/******************************************************************************/

void ClearScreen()
{
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


/******************************************************************************/

void glClearErrors(const char* cls, const char* fnc, bool verbose)
{
	if (verbose) {
		for (int count = 0, error = 0; ((error = glGetError()) != GL_NO_ERROR) && (count < 10000); count++) {
			LOG_L(L_ERROR, "[GL::%s][%s::%s][frame=%u] count=%04d error=0x%x", __func__, cls, fnc, globalRendering->drawFrame, count, error);
		}
	} else {
		for (int count = 0; (glGetError() != GL_NO_ERROR) && (count < 10000); count++);
	}
}

