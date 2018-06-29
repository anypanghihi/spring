/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>
#include <cstring> // memcpy

#include "myGL.h"
#include "RenderDataBuffer.hpp"

// global general-purpose buffers
static GL::RenderDataBuffer gRenderBuffer0 [2];
static GL::RenderDataBuffer gRenderBufferN [2];
static GL::RenderDataBuffer gRenderBufferC [2];
static GL::RenderDataBuffer gRenderBufferFC[2];
static GL::RenderDataBuffer gRenderBufferT [2];

static GL::RenderDataBuffer gRenderBufferT4[2];
static GL::RenderDataBuffer gRenderBufferTN[2];
static GL::RenderDataBuffer gRenderBufferTC[2];

static GL::RenderDataBuffer gRenderBuffer2D0[2];
static GL::RenderDataBuffer gRenderBuffer2DT[2];


static GL::RenderDataBuffer0 tRenderBuffer0 [2];
static GL::RenderDataBufferN tRenderBufferN [2];
static GL::RenderDataBufferC tRenderBufferC [2];
static GL::RenderDataBufferC tRenderBufferFC[2];
static GL::RenderDataBufferT tRenderBufferT [2];

static GL::RenderDataBufferT4 tRenderBufferT4[2];
static GL::RenderDataBufferTN tRenderBufferTN[2];
static GL::RenderDataBufferTC tRenderBufferTC[2];

static GL::RenderDataBuffer2D0 tRenderBuffer2D0[2];
static GL::RenderDataBuffer2DT tRenderBuffer2DT[2];


GL::RenderDataBuffer0* GL::GetRenderBuffer0 () { return &tRenderBuffer0 [0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferN* GL::GetRenderBufferN () { return &tRenderBufferN [0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferC* GL::GetRenderBufferC () { return &tRenderBufferC [0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferC* GL::GetRenderBufferFC() { return &tRenderBufferFC[0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferT* GL::GetRenderBufferT () { return &tRenderBufferT [0 /*globalRendering->drawFrame & 1*/ ]; }

GL::RenderDataBufferT4* GL::GetRenderBufferT4() { return &tRenderBufferT4[0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferTN* GL::GetRenderBufferTN() { return &tRenderBufferTN[0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBufferTC* GL::GetRenderBufferTC() { return &tRenderBufferTC[0 /*globalRendering->drawFrame & 1*/ ]; }

GL::RenderDataBuffer2D0* GL::GetRenderBuffer2D0() { return &tRenderBuffer2D0[0 /*globalRendering->drawFrame & 1*/ ]; }
GL::RenderDataBuffer2DT* GL::GetRenderBuffer2DT() { return &tRenderBuffer2DT[0 /*globalRendering->drawFrame & 1*/ ]; }


void GL::InitRenderBuffers() {
	char vsBuffer[65536];
	char fsBuffer[65536];

	Shader::GLSLShaderObject shaderObjs[2] = {{GL_VERTEX_SHADER, "", ""}, {GL_FRAGMENT_SHADER, "", ""}};


	#define SETUP_RBUFFER(T, i, ne, ni) tRenderBuffer ## T[i].Setup(&gRenderBuffer ## T[i], &GL::VA_TYPE_ ## T ## _ATTRS, ne, ni)
	#define CREATE_SHADER(T, i, VS_CODE, FS_CODE)                                                                          \
		do {                                                                                                               \
			GL::RenderDataBuffer::FormatShader ## T(vsBuffer, vsBuffer + sizeof(vsBuffer), "", "", VS_CODE, "VS");         \
			GL::RenderDataBuffer::FormatShader ## T(fsBuffer, fsBuffer + sizeof(fsBuffer), "", "", FS_CODE, "FS");         \
			shaderObjs[0] = {GL_VERTEX_SHADER  , &vsBuffer[0], ""};                                                        \
			shaderObjs[1] = {GL_FRAGMENT_SHADER, &fsBuffer[0], ""};                                                        \
			gRenderBuffer ## T[i].CreateShader((sizeof(shaderObjs) / sizeof(shaderObjs[0])), 0,  &shaderObjs[0], nullptr); \
		} while (false)

	for (int i = 0; i < 2; i++) {
		SETUP_RBUFFER( 0, i, 1 << 18, 1 << 16);
		SETUP_RBUFFER( N, i, 1 << 18, 1 << 16);
		SETUP_RBUFFER( C, i, 1 << 20, 1 << 16); // more heavily used
		SETUP_RBUFFER(FC, i, 1 << 10, 1 <<  8); // less heavily used
		SETUP_RBUFFER( T, i, 1 << 18, 1 << 16);

		SETUP_RBUFFER(T4, i, 1 << 18, 1 << 16);
		SETUP_RBUFFER(TN, i, 1 << 18, 1 << 16);
		SETUP_RBUFFER(TC, i, 1 << 18, 1 << 16);

		SETUP_RBUFFER(2D0, i, 1 << 18, 1 << 16);
		SETUP_RBUFFER(2DT, i, 1 << 18, 1 << 16);
	}

	for (int i = 0; i < 2; i++) {
		CREATE_SHADER( 0, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
		CREATE_SHADER( N, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
		CREATE_SHADER( C, i, "", "\tf_color_rgba = v_color_rgba      * (1.0 / 255.0);\n");
		CREATE_SHADER(FC, i, "", "\tf_color_rgba = v_color_rgba_flat * (1.0 / 255.0);\n");
		CREATE_SHADER( T, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");

		CREATE_SHADER(T4, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_stuv.st);\n");
		CREATE_SHADER(TN, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");
		CREATE_SHADER(TC, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st) * v_color_rgba * (1.0 / 255.0);\n");

		CREATE_SHADER(2D0, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
		CREATE_SHADER(2DT, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");
	}

	#undef CREATE_SHADER
	#undef SETUP_RBUFFER
}

void GL::KillRenderBuffers() {
	for (int i = 0; i < 2; i++) {
		gRenderBuffer0 [i].Kill();
		gRenderBufferN [i].Kill();
		gRenderBufferC [i].Kill();
		gRenderBufferFC[i].Kill();
		gRenderBufferT [i].Kill();

		gRenderBufferT4[i].Kill();
		gRenderBufferTN[i].Kill();
		gRenderBufferTC[i].Kill();

		gRenderBuffer2D0[i].Kill();
		gRenderBuffer2DT[i].Kill();
	}
}

void GL::SwapRenderBuffers() {
	#if 0
	// NB: called before frame is incremented
	tRenderBuffer0[1 - (globalRendering->drawFrame & 1)].Reset();
	tRenderBufferN[1 - (globalRendering->drawFrame & 1)].Reset();
	tRenderBufferC[1 - (globalRendering->drawFrame & 1)].Reset();
	tRenderBufferT[1 - (globalRendering->drawFrame & 1)].Reset();
	#else
	std::swap(tRenderBuffer0 [0], tRenderBuffer0 [1]);
	std::swap(tRenderBufferN [0], tRenderBufferN [1]);
	std::swap(tRenderBufferC [0], tRenderBufferC [1]);
	std::swap(tRenderBufferFC[0], tRenderBufferFC[1]);
	std::swap(tRenderBufferT [0], tRenderBufferT [1]);

	std::swap(tRenderBufferT4[0], tRenderBufferT4[1]);
	std::swap(tRenderBufferTN[0], tRenderBufferTN[1]);
	std::swap(tRenderBufferTC[0], tRenderBufferTC[1]);

	std::swap(tRenderBuffer2D0[0], tRenderBuffer2D0[1]);
	std::swap(tRenderBuffer2DT[0], tRenderBuffer2DT[1]);

	tRenderBuffer0 [0].Reset();
	tRenderBufferN [0].Reset();
	tRenderBufferC [0].Reset();
	tRenderBufferFC[0].Reset();
	tRenderBufferT [0].Reset();

	tRenderBufferT4[0].Reset();
	tRenderBufferTN[0].Reset();
	tRenderBufferTC[0].Reset();

	tRenderBuffer2D0[0].Reset();
	tRenderBuffer2DT[0].Reset();
	#endif
}




void GL::RenderDataBuffer::EnableAttribs(size_t numAttrs, const Shader::ShaderInput* rawAttrs) const {
	for (size_t n = 0; n < numAttrs; n++) {
		const Shader::ShaderInput& a = rawAttrs[n];

		glEnableVertexAttribArray(a.index);
		glVertexAttribPointer(a.index, a.count, a.type, false, a.stride, a.data);
	}
}

void GL::RenderDataBuffer::DisableAttribs(size_t numAttrs, const Shader::ShaderInput* rawAttrs) const {
	for (size_t n = 0; n < numAttrs; n++) {
		glDisableVertexAttribArray(rawAttrs[n].index);
	}
}


char* GL::RenderDataBuffer::FormatShaderBase(
	char* buf,
	const char* end,
	const char* defines,
	const char* globals,
	const char* type,
	const char* name
) {
	std::memset(buf, 0, (end - buf));

	char* ptr = &buf[0];
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "#version 410 core\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "#extension GL_ARB_explicit_attrib_location : enable\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "// defines\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "#define VA_TYPE %s\n", name);
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", defines);
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "// globals\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", globals);
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "// uniforms\n");

	switch (type[0]) {
		case 'V': {
			ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "uniform mat4 u_movi_mat;\n");
			ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "uniform mat4 u_proj_mat;\n");
		} break;
		case 'F': {
			ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "uniform sampler2D u_tex0;\n"); // T*,2DT* (v_texcoor_st*)
			ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "uniform sampler3D u_tex1;\n"); // TNT (v_texcoor_uv1)
			ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "uniform sampler3D u_tex2;\n"); // TNT (v_texcoor_uv2)
		} break;
		default: {} break;
	}

	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\n");
	return ptr;
}

char* GL::RenderDataBuffer::FormatShaderType(
	char* buf,
	char* ptr,
	const char* end,
	size_t numAttrs,
	const Shader::ShaderInput* rawAttrs,
	const char* code,
	const char* type,
	const char* name
) {
	constexpr const char*  vecTypes[] = {"vec2", "vec3", "vec4"};
	constexpr const char* typeQuals[] = {"", "flat"};

	constexpr const char* vsInpFmt = "layout(location = %d) in %s %s;\n";
	constexpr const char* vsOutFmt = "%s out %s v_%s;\n"; // prefix VS outs by "v_"
	constexpr const char* fsInpFmt = "%s in %s v_%s;\n";
	constexpr const char* fsOutFmt = "layout(location = 0) out vec4 f_%s;\n"; // prefix (single, fixed) FS out by "f_"

	{
		ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "// %s input attributes\n", type);

		for (size_t n = 0; n < numAttrs; n++) {
			const Shader::ShaderInput& a = rawAttrs[n];

			assert(a.count >= 2);
			assert(a.count <= 4);

			const char*  vecType = vecTypes[a.count - 2];
			const char* typeQual = typeQuals[strstr(a.name + 2, typeQuals[1]) != nullptr];

			switch (type[0]) {
				case 'V': { ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), vsInpFmt, a.index, vecType, a.name); } break;
				case 'F': { ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), fsInpFmt, typeQual, vecType, a.name + 2); } break;
				default: {} break;
			}
		}
	}

	{
		ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "// %s output attributes\n", type);

		switch (type[0]) {
			case 'V': {
				for (size_t n = 0; n < numAttrs; n++) {
					const Shader::ShaderInput& a = rawAttrs[n];

					assert(a.name[0] == 'a');
					assert(a.name[1] == '_');

					const char*  vecType = vecTypes[a.count - 2];
					const char* typeQual = typeQuals[strstr(a.name + 2, typeQuals[1]) != nullptr];

					ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), vsOutFmt, typeQual, vecType, a.name + 2);
				}
			} break;
			case 'F': {
				ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), fsOutFmt, "color_rgba");
			} break;
			default: {} break;
		}
	}

	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\n");
	ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "void main() {\n");

	if (code[0] != '\0') {
		ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s\n", code);
	} else {
		switch (type[0]) {
			case 'V': {
				// position (2D or 3D) is always the first attribute
				switch (rawAttrs[0].count) {
					case 2: { ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\tgl_Position = u_proj_mat * u_movi_mat * vec4(a_vertex_xy , 0.0, 1.0);\n"); } break;
					case 3: { ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\tgl_Position = u_proj_mat * u_movi_mat * vec4(a_vertex_xyz,      1.0);\n"); } break;
					default: {} break;
				}

				for (size_t n = 1; n < numAttrs; n++) {
					const Shader::ShaderInput& a = rawAttrs[n];

					// assume standard tc-gen
					if (std::strcmp(a.name, "a_texcoor_stuv") == 0) {
						ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\tv_texcoor_stuv = a_texcoor_stuv;\n");
						continue;
					}
					if (std::strcmp(a.name, "a_texcoor_st") == 0) {
						ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "\tv_texcoor_st = a_texcoor_st;\n");
						continue;
					}
					if (std::strcmp(a.name, "a_texcoor_uv") == 0) {
						ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "\tv_texcoor_uv%d = a_texcoor_uv%d;\n", a.name[12] - '0', a.name[12] - '0');
						continue;
					}

					ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "\tv_%s = a_%s;\n", a.name + 2, a.name + 2);
				}
			} break;
			case 'F': {} break;
			default: {} break;
		}
	}

	return (ptr += std::snprintf(ptr, (end - buf) - (ptr - buf), "%s", "}\n"));
}


Shader::GLSLProgramObject* GL::RenderDataBuffer::CreateShader(
	size_t numObjects,
	size_t numUniforms,
	Shader::GLSLShaderObject* objects,
	const Shader::ShaderInput* uniforms
) {
	for (size_t n = 0; n < numObjects; n++) {
		shader.AttachShaderObject(&objects[n]);
	}

	shader.ReloadShaderObjects();
	shader.CreateAndLink();
	shader.RecalculateShaderHash();
	// RDB shaders are never reloaded, get rid of attachments early
	shader.ClearAttachedShaderObjects();

	for (size_t n = 0; n < numUniforms; n++) {
		shader.SetUniform(uniforms[n]);
	}

	shader.Validate();
	return &shader;
}


void GL::RenderDataBuffer::Upload(
	size_t numElems,
	size_t numIndcs,
	size_t numAttrs,
	const uint8_t* rawElems,
	const uint8_t* rawIndcs,
	const Shader::ShaderInput* rawAttrs
) {
	array.Bind();
	elems.Bind();
	elems.New(numElems * sizeof(uint8_t), elems.usage, rawElems);

	if (numIndcs > 0) {
		indcs.Bind();
		indcs.New(numIndcs * sizeof(uint8_t), indcs.usage, rawIndcs);
	}

	EnableAttribs(numAttrs, rawAttrs);

	array.Unbind();
	elems.Unbind();

	if (numIndcs > 0)
		indcs.Unbind();

	DisableAttribs(numAttrs, rawAttrs);
}


void GL::RenderDataBuffer::Submit(uint32_t primType, uint32_t dataIndx, uint32_t dataSize) const {
	assert(elems.GetSize() != 0);
	// buffers populated with (dummy or actual) indices
	// can still be Submit()'ed for non-indexed drawing
	assert(indcs.GetSize() >= 0);

	array.Bind();

	// dataIndx := first elem, dataSize := numElems (unique verts)
	glDrawArrays(primType, dataIndx, dataSize);

	array.Unbind();
}

void GL::RenderDataBuffer::SubmitInstanced(uint32_t primType, uint32_t dataIndx, uint32_t dataSize, uint32_t numInsts) const {
	array.Bind();
	glDrawArraysInstanced(primType, dataIndx, dataSize, numInsts);
	array.Unbind();
}


void GL::RenderDataBuffer::SubmitIndexed(uint32_t primType, uint32_t dataIndx, uint32_t dataSize) const {
	assert(elems.GetSize() != 0);
	assert(indcs.GetSize() != 0);

	array.Bind();

	// dataIndx := index offset, dataSize := numIndcs
	glDrawElements(primType, dataSize, GL_UNSIGNED_INT, VA_TYPE_OFFSET(uint32_t, dataIndx));

	array.Unbind();
}

void GL::RenderDataBuffer::SubmitIndexedInstanced(uint32_t primType, uint32_t dataIndx, uint32_t dataSize, uint32_t numInsts) const {
	array.Bind();
	glDrawElementsInstanced(primType, dataSize, GL_UNSIGNED_INT, VA_TYPE_OFFSET(uint32_t, dataIndx), numInsts);
	array.Unbind();
}




#if 0
void GL::RenderDataBuffer::UploadC(
	size_t numElems,
	size_t numIndcs,
	const VA_TYPE_C* rawElems,
	const uint32_t* rawIndcs
) {
	uploadBuffer.clear();
	uploadBuffer.reserve(numElems * (VA_SIZE_C + 3));

	for (size_t n = 0; n < numElems; n++) {
		const VA_TYPE_C& e = rawElems[n];

		uploadBuffer.push_back(e.p.x);
		uploadBuffer.push_back(e.p.y);
		uploadBuffer.push_back(e.p.z);
		uploadBuffer.push_back(e.c.r); // turn SColor uint32 into 4 floats
		uploadBuffer.push_back(e.c.g);
		uploadBuffer.push_back(e.c.b);
		uploadBuffer.push_back(e.c.a);
	}

	Upload(uploadBuffer.size(), numIndcs, NUM_VA_TYPE_C_ATTRS, uploadBuffer.data(), rawIndcs, VA_TYPE_C_ATTRS);
}
#endif

