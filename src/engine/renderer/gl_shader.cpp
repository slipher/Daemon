/*
===========================================================================
Copyright (C) 2010-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// gl_shader.cpp -- GLSL shader handling

#include <common/FileSystem.h>
#include "gl_shader.h"

// We currently write GLBinaryHeader to a file and memcpy all over it.
// Make sure it's a pod, so we don't put a std::string in it or something
// and try to memcpy over that or binary write an std::string to a file.
static_assert(std::is_pod<GLBinaryHeader>::value, "Value must be a pod while code in this cpp file reads and writes this object to file as binary.");

extern std::unordered_map<std::string, std::string> shadermap;
// shaderKind's value will be determined later based on command line setting or absence of.
ShaderKind shaderKind = ShaderKind::Unknown;

// *INDENT-OFF*

GLShader_generic2D                       *gl_generic2DShader = nullptr;
GLShader_generic                         *gl_genericShader = nullptr;
GLShader_lightMapping                    *gl_lightMappingShader = nullptr;
GLShader_shadowFill                      *gl_shadowFillShader = nullptr;
GLShader_reflection                      *gl_reflectionShader = nullptr;
GLShader_skybox                          *gl_skyboxShader = nullptr;
GLShader_fogQuake3                       *gl_fogQuake3Shader = nullptr;
GLShader_fogGlobal                       *gl_fogGlobalShader = nullptr;
GLShader_heatHaze                        *gl_heatHazeShader = nullptr;
GLShader_screen                          *gl_screenShader = nullptr;
GLShader_portal                          *gl_portalShader = nullptr;
GLShader_contrast                        *gl_contrastShader = nullptr;
GLShader_cameraEffects                   *gl_cameraEffectsShader = nullptr;
GLShader_blurX                           *gl_blurXShader = nullptr;
GLShader_blurY                           *gl_blurYShader = nullptr;
GLShader_debugShadowMap                  *gl_debugShadowMapShader = nullptr;
GLShader_liquid                          *gl_liquidShader = nullptr;
GLShader_motionblur                      *gl_motionblurShader = nullptr;
GLShader_ssao                            *gl_ssaoShader = nullptr;
GLShader_depthtile1                      *gl_depthtile1Shader = nullptr;
GLShader_depthtile2                      *gl_depthtile2Shader = nullptr;
GLShader_lighttile                       *gl_lighttileShader = nullptr;
GLShader_fxaa                            *gl_fxaaShader = nullptr;
GLShaderManager                           gl_shaderManager;

namespace // Implementation details
{
	NORETURN inline void ThrowShaderError(Str::StringRef msg)
	{
		throw ShaderException(msg.c_str());
	}

	const char* GetInternalShader(Str::StringRef filename)
	{
		auto it = shadermap.find(filename);
		if (it != shadermap.end())
			return it->second.c_str();
		return nullptr;
	}

	void CRLFToLF(std::string& source)
	{
		size_t sourcePos = 0;
		size_t keepPos = 0;

		auto keep = [&](size_t keepLength)
		{
			if (sourcePos > 0)
				std::copy(source.begin() + sourcePos, source.begin() + sourcePos + keepLength,
					source.begin() + keepPos);
			keepPos += keepLength;
		};

		for (;;)
		{
			size_t targetPos = source.find("\r\n", sourcePos);
			// If we don't find a line break, shuffle what's left
			// into place and we're done.
			if (targetPos == std::string::npos)
			{
				size_t remainingLength = source.length() - sourcePos;
				keep(remainingLength);
				break;
			}
			// If we do find a line break, shuffle what's before it into place
			// except for the '\r\n'. But then tack on a '\n',
			// resulting in effectively just losing the '\r' in the sequence.
			size_t keepLength = (targetPos - sourcePos);
			keep(keepLength);
			source[keepPos] = '\n';
			++keepPos;
			sourcePos = targetPos + 2;
		}
		source.resize(keepPos);
	}

	// CR/LF's can wind up in the raw files because of how
	// version control system works and how Windows works.
	// Remove them so we are always comparing apples with apples.
	void NormalizeShaderText( std::string& text )
	{
		// A windows user changing the shader file can put
		// Windows can put CRLF's in the file. Make them LF's.
		CRLFToLF(text);
	}

	std::string GetShaderFilename(Str::StringRef filename)
	{
		std::string shaderBase = GetShaderPath();
		if (shaderBase.empty())
			return shaderBase;
		std::string shaderFileName = FS::Path::Build(shaderBase, filename);
		return shaderFileName;
	}

	std::string GetShaderText(Str::StringRef filename)
	{
		// Shader type should be set during initialisation.
		if (shaderKind == ShaderKind::BuiltIn)
		{
			// Look for the shader internally. If not found, look for it externally.
			// If found neither internally or externally of if empty, then Error.
			auto text_ptr = GetInternalShader(filename);
			if (text_ptr == nullptr)
				ThrowShaderError(Str::Format("No shader found for shader: %s", filename));
			return text_ptr;
		}
		else if (shaderKind == ShaderKind::External)
		{
			std::string shaderText;
			std::string shaderFilename = GetShaderFilename(filename);

			Log::Notice("Loading shader '%s'", shaderFilename);

			std::error_code err;

			FS::File shaderFile = FS::RawPath::OpenRead(shaderFilename, err);
			if (err)
				ThrowShaderError(Str::Format("Cannot load shader from file %s: %s", shaderFilename, err.message()));

			shaderText = shaderFile.ReadAll(err);
			if (err)
				ThrowShaderError(Str::Format("Failed to read shader from file %s: %s", shaderFilename, err.message()));

			// Alert the user when a file does not match it's built-in version.
			// There should be no differences in normal conditions.
			// When testing shader file changes this is an expected message
			// and helps the tester track which files have changed and need
			// to be recommitted to git.
			// If one is not making shader files changes this message
			// indicates there is a mismatch between disk changes and builtins
			// which the application is out of sync with it's files
			// and he translation script needs to be run.
			auto textPtr = GetInternalShader(filename);
			std::string internalShaderText;
			if (textPtr != nullptr)
				internalShaderText = textPtr;

			// Note to the user any differences that might exist between
			// what's on disk and what's compiled into the program in shaders.cpp.
			// The developer should be aware of any differences why they exist but
			// they might be expected or unexpected.
			// If the developer made changes they might want to be reminded of what
			// they have changed while they are working.
			// But it also might be that the developer hasn't made any changes but
			// the compiled code is shaders.cpp is just out of sync with the shader
			// files and that buildshaders.sh might need to be run to re-sync.
			// This message alerts user to either situation and they can decide
			// what's going on from seeing that.
			// We normalize the text by removing CL/LF's so they aren't considered
			// a difference as Windows or the Version Control System can put them in
			// and another OS might read them back and consider that a difference
			// to what's in shader.cpp or vice vesa.
			NormalizeShaderText(internalShaderText);
			NormalizeShaderText(shaderText);
			if (internalShaderText != shaderText)
				Log::Warn("Note shader file differs from built-in shader: %s", shaderFilename);

			if (shaderText.empty())
				ThrowShaderError(Str::Format("Shader from file is empty: %s", shaderFilename));

			return shaderText;
		}
		ThrowShaderError("Internal error. ShaderKind not set.");
	}
}

std::string GetShaderPath()
{
	std::string shaderPath;
	auto shaderPathVar = Cvar_Get("shaderpath", "", CVAR_INIT);
	if (shaderPathVar->string != nullptr)
		shaderPath = shaderPathVar->string;
	return shaderPath;
}

GLShaderManager::~GLShaderManager()
= default;

void GLShaderManager::freeAll()
{
	_shaders.clear();

	for ( GLint sh : _deformShaders )
		glDeleteShader( sh );

	_deformShaders.clear();
	_deformShaderLookup.clear();

	while ( !_shaderBuildQueue.empty() )
	{
		_shaderBuildQueue.pop();
	}

	_totalBuildTime = 0;
}

void GLShaderManager::UpdateShaderProgramUniformLocations( GLShader *shader, shaderProgram_t *shaderProgram ) const
{
	size_t uniformSize = shader->_uniformStorageSize;
	size_t numUniforms = shader->_uniforms.size();
	size_t numUniformBlocks = shader->_uniformBlocks.size();

	// create buffer for storing uniform locations
	shaderProgram->uniformLocations = ( GLint * ) ri.Z_Malloc( sizeof( GLint ) * numUniforms );

	// create buffer for uniform firewall
	shaderProgram->uniformFirewall = ( byte * ) ri.Z_Malloc( uniformSize );

	// update uniforms
	for (GLUniform *uniform : shader->_uniforms)
	{
		uniform->UpdateShaderProgramUniformLocation( shaderProgram );
	}

	if( glConfig2.uniformBufferObjectAvailable ) {
		// create buffer for storing uniform block indexes
		shaderProgram->uniformBlockIndexes = ( GLuint * ) ri.Z_Malloc( sizeof( GLuint ) * numUniformBlocks );

		// update uniform blocks
		for (GLUniformBlock *uniformBlock : shader->_uniformBlocks)
		{
			uniformBlock->UpdateShaderProgramUniformBlockIndex( shaderProgram );
		}
	}
}

static inline void AddDefine( std::string& defines, const std::string& define, int value )
{
	defines += Str::Format("#ifndef %s\n#define %s %d\n#endif\n", define, define, value);
}

// Epsilon for float is 5.96e-08, so exponential notation with 8 decimal places should give exact values.

static inline void AddDefine( std::string& defines, const std::string& define, float value )
{
	defines += Str::Format("#ifndef %s\n#define %s %.8e\n#endif\n", define, define, value);
}

static inline void AddDefine( std::string& defines, const std::string& define, float v1, float v2 )
{
	defines += Str::Format("#ifndef %s\n#define %s vec2(%.8e, %.8e)\n#endif\n", define, define, v1, v2);
}

static inline void AddDefine( std::string& defines, const std::string& define )
{
	defines += Str::Format("#ifndef %s\n#define %s\n#endif\n", define, define);
}

// Has to match enum genFunc_t in tr_local.h
static const char *const genFuncNames[] = {
	  "DSTEP_NONE",
	  "DSTEP_SIN",
	  "DSTEP_SQUARE",
	  "DSTEP_TRIANGLE",
	  "DSTEP_SAWTOOTH",
	  "DSTEP_INV_SAWTOOTH",
	  "DSTEP_NOISE"
};

static std::string BuildDeformSteps( deformStage_t *deforms, int numDeforms )
{
	std::string steps;

	steps.reserve(256); // Might help a little.

	steps += "#define DEFORM_STEPS ";
	for( int step = 0; step < numDeforms; step++ )
	{
		const deformStage_t &ds = deforms[ step ];

		switch ( ds.deformation )
		{
		case deform_t::DEFORM_WAVE:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("%s(%f, %f, %f) ",
				    genFuncNames[ Util::ordinal(ds.deformationWave.func) ],
				    ds.deformationWave.phase,
				    ds.deformationSpread,
				    ds.deformationWave.frequency );
			steps += "DSTEP_LOAD_NORM(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_MODIFY_POS(%f, %f, 1.0) ",
				    ds.deformationWave.base,
				    ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_BULGE:
			steps += "DSTEP_LOAD_TC(1.0, 0.0, 0.0) ";
			steps += Str::Format("DSTEP_SIN(0.0, %f, %f) ",
				    ds.bulgeWidth,
				    ds.bulgeSpeed * 0.001f );
			steps += "DSTEP_LOAD_NORM(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_MODIFY_POS(0.0, %f, 1.0) ",
				    ds.bulgeHeight );
			break;

		case deform_t::DEFORM_MOVE:
			steps += Str::Format("%s(%f, 0.0, %f) ",
				    genFuncNames[ Util::ordinal(ds.deformationWave.func) ],
				    ds.deformationWave.phase,
				    ds.deformationWave.frequency );
			steps += Str::Format("DSTEP_LOAD_VEC(%f, %f, %f) ",
				    ds.moveVector[ 0 ],
				    ds.moveVector[ 1 ],
				    ds.moveVector[ 2 ] );
			steps += Str::Format("DSTEP_MODIFY_POS(%f, %f, 1.0) ",
				    ds.deformationWave.base,
				    ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_NORMALS:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_NOISE(0.0, 0.0, %f) ",
				    ds.deformationWave.frequency );
			steps += Str::Format("DSTEP_MODIFY_NORM(0.0, %f, 1.0) ",
				    0.98f * ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_ROTGROW:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_ROTGROW(%f, %f, %f) ",
				    ds.moveVector[0],
				    ds.moveVector[1],
				    ds.moveVector[2] );
			steps += "DSTEP_LOAD_COLOR(1.0, 1.0, 1.0) ";
			steps += "DSTEP_MODIFY_COLOR(-1.0, 1.0, 0.0) ";
			break;

		default:
			break;
		}
	}

	return steps;
}


static void addExtension( std::string &str, int enabled, int minGlslVersion,
			  int supported, const char *name ) {
	if( !enabled ) {
		// extension disabled by user
	} else if( glConfig2.shadingLanguageVersion >= minGlslVersion ) {
		// the extension is available in the core language
		str += Str::Format( "#define HAVE_%s 1\n", name );
	} else if( supported ) {
		// extension has to be explicitly enabled
		str += Str::Format( "#extension GL_%s : require\n", name );
		str += Str::Format( "#define HAVE_%s 1\n", name );
	} else {
		// extension is not supported
	}
}

static void AddConst( std::string& str, const std::string& name, int value )
{
	str += Str::Format("const int %s = %d;\n", name, value);
}

static void AddConst( std::string& str, const std::string& name, float value )
{
	str += Str::Format("const float %s = %.8e;\n", name, value);
}

static void AddConst( std::string& str, const std::string& name, float v1, float v2 )
{
	str += Str::Format("const vec2 %s = vec2(%.8e, %.8e);\n", name, v1, v2);
}

static std::string GenVersionDeclaration() {
	// Basic version declaration
	std::string str = Str::Format( "#version %d %s\n",
				       glConfig2.shadingLanguageVersion,
				       glConfig2.shadingLanguageVersion >= 150 ? (glConfig2.glCoreProfile ? "core" : "compatibility") : "");

	// add supported GLSL extensions
	addExtension( str, glConfig2.textureGatherAvailable, 400,
		      GLEW_ARB_texture_gather, "ARB_texture_gather" );
	addExtension( str, r_ext_gpu_shader4->integer, 130,
		      GLEW_EXT_gpu_shader4, "EXT_gpu_shader4" );
	addExtension( str, r_arb_gpu_shader5->integer, 400,
		      GLEW_ARB_gpu_shader5, "ARB_gpu_shader5" );
	addExtension( str, r_arb_uniform_buffer_object->integer, 140,
		      GLEW_ARB_uniform_buffer_object, "ARB_uniform_buffer_object" );

	return str;
}

static std::string GenCompatHeader() {
	std::string str;

	// definition of functions missing in early GLSL
	if( glConfig2.shadingLanguageVersion <= 120 ) {
		str += "float smoothstep(float edge0, float edge1, float x) { float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }\n";
	}

	return str;
}

static std::string GenVertexHeader() {
	std::string str;

	// Vertex shader compatibility defines
	if( glConfig2.shadingLanguageVersion > 120 ) {
		str =   "#define IN in\n"
			"#define OUT(mode) mode out\n"
			"#define textureCube texture\n"
			"#define texture2D texture\n"
			"#define texture2DProj textureProj\n"
			"#define texture3D texture\n";
	} else {
		str =   "#define IN attribute\n"
			"#define OUT(mode) varying\n";
	}

	return str;
}

static std::string GenFragmentHeader() {
	std::string str;

	// Fragment shader compatibility defines
	if( glConfig2.shadingLanguageVersion > 120 ) {
		str =   "#define IN(mode) mode in\n"
			"#define DECLARE_OUTPUT(type) out type outputColor;\n"
			"#define textureCube texture\n"
			"#define texture2D texture\n"
			"#define texture2DProj textureProj\n"
			"#define texture3D texture\n";
	} else if( glConfig2.gpuShader4Available) {
		str =   "#define IN(mode) varying\n"
			"#define DECLARE_OUTPUT(type) varying out type outputColor;\n";
	} else {
		str =   "#define IN(mode) varying\n"
			"#define outputColor gl_FragColor\n"
			"#define DECLARE_OUTPUT(type) /* empty*/\n";
	}

	return str;
}

static std::string GenEngineConstants() {
	// Engine constants
	std::string str;

	if ( r_shadows->integer >= Util::ordinal(shadowingMode_t::SHADOWING_ESM16) && glConfig2.textureFloatAvailable )
	{
		if ( r_shadows->integer == Util::ordinal(shadowingMode_t::SHADOWING_ESM16) || r_shadows->integer == Util::ordinal(shadowingMode_t::SHADOWING_ESM32) )
		{
			AddDefine( str, "ESM", 1 );
		}
		else if ( r_shadows->integer == Util::ordinal(shadowingMode_t::SHADOWING_EVSM32) )
		{
			AddDefine( str, "EVSM", 1 );
			// The exponents for the EVSM techniques should be less than ln(FLT_MAX/FILTER_SIZE)/2 {ln(FLT_MAX/1)/2 ~44.3}
			//         42.9 is the maximum possible value for FILTER_SIZE=15
			//         42.0 is the truncated value that we pass into the sample
			AddConst( str, "r_EVSMExponents", 42.0f, 42.0f );
			if ( r_evsmPostProcess->integer )
				AddDefine( str,"r_EVSMPostProcess", 1 );
		}
		else
		{
			AddDefine( str, "VSM", 1 );

			if ( glConfig.hardwareType == glHardwareType_t::GLHW_R300 )
			{
				AddDefine( str, "VSM_CLAMP", 1 );
			}
		}

		if ( ( glConfig.driverType == glDriverType_t::GLDRV_OPENGL3 ) && r_shadows->integer == Util::ordinal(shadowingMode_t::SHADOWING_VSM32) )
		{
			AddConst( str, "VSM_EPSILON", 0.000001f );
		}
		else // also required by GLHW_R300 which is not GLDRV_OPENGL3 anyway
		{
			AddConst( str, "VSM_EPSILON", 0.0001f );
		}

		if ( r_lightBleedReduction->value )
			AddConst( str, "r_lightBleedReduction", r_lightBleedReduction->value );

		if ( r_overDarkeningFactor->value )
			AddConst( str, "r_overDarkeningFactor", r_overDarkeningFactor->value );

		if ( r_shadowMapDepthScale->value )
			AddConst( str, "r_shadowMapDepthScale", r_shadowMapDepthScale->value );

		if ( r_debugShadowMaps->integer )
			AddDefine( str, "r_debugShadowMaps", r_debugShadowMaps->integer );

		if ( r_softShadows->integer == 6 )
			AddDefine( str, "PCSS", 1 );
		else if ( r_softShadows->integer )
			AddConst( str, "r_PCFSamples", r_softShadows->value + 1.0f );

		if ( r_parallelShadowSplits->integer )
			AddDefine( str, Str::Format( "r_parallelShadowSplits_%d", r_parallelShadowSplits->integer ) );

		if ( r_showParallelShadowSplits->integer )
			AddDefine( str, "r_showParallelShadowSplits", 1 );
	}

	if ( glConfig2.dynamicLight )
	{
		AddDefine( str, "r_dynamicLight", glConfig2.dynamicLight );
	}

	if ( r_precomputedLighting->integer )
		AddDefine( str, "r_precomputedLighting", 1 );

	if ( r_showLightMaps->integer )
	{
		AddDefine( str, "r_showLightMaps", 1 );
	}

	if ( r_showDeluxeMaps->integer )
	{
		AddDefine( str, "r_showDeluxeMaps", 1 );
	}

	if ( r_showNormalMaps->integer )
	{
		AddDefine( str, "r_showNormalMaps", 1 );
	}

	if ( r_showMaterialMaps->integer )
	{
		AddDefine( str, "r_showMaterialMaps", 1 );
	}

	if ( glConfig2.vboVertexSkinningAvailable )
	{
		AddDefine( str, "r_vertexSkinning", 1 );
		AddConst( str, "MAX_GLSL_BONES", glConfig2.maxVertexSkinningBones );
	}
	else
	{
		AddConst( str, "MAX_GLSL_BONES", 4 );
	}

	if ( r_wrapAroundLighting->value )
		AddConst( str, "r_wrapAroundLighting", r_wrapAroundLighting->value );

	if ( r_halfLambertLighting->integer )
		AddDefine( str, "r_halfLambertLighting", 1 );

	if ( r_rimLighting->integer )
	{
		AddDefine( str, "r_rimLighting", 1 );
		AddConst( str, "r_RimExponent", r_rimExponent->value );
	}

	if ( r_showLightTiles->integer )
	{
		AddDefine( str, "r_showLightTiles", 1 );
	}

	if ( r_normalMapping->integer )
	{
		AddDefine( str, "r_normalMapping", 1 );
	}

	if ( r_liquidMapping->integer )
	{
		AddDefine( str, "r_liquidMapping", 1 );
	}

	if ( r_specularMapping->integer )
	{
		AddDefine( str, "r_specularMapping", 1 );
	}

	if ( r_physicalMapping->integer )
	{
		AddDefine( str, "r_physicalMapping", 1 );
	}

	if ( r_glowMapping->integer )
	{
		AddDefine( str, "r_glowMapping", 1 );
	}

	return str;
}

void GLShaderManager::InitDriverInfo()
{
	std::string driverInfo = std::string(glConfig.renderer_string) + glConfig.version_string;
	_driverVersionHash = Com_BlockChecksum(driverInfo.c_str(), static_cast<int>(driverInfo.size()));
	_shaderBinaryCacheInvalidated = false;
}

void GLShaderManager::GenerateBuiltinHeaders() {
	GLVersionDeclaration = GLHeader("GLVersionDeclaration", GenVersionDeclaration(), this);
	GLCompatHeader = GLHeader("GLCompatHeader", GenCompatHeader(), this);
	GLVertexHeader = GLHeader("GLVertexHeader", GenVertexHeader(), this);
	GLFragmentHeader = GLHeader("GLFragmentHeader", GenFragmentHeader(), this);
	GLEngineConstants = GLHeader("GLEngineConstants", GenEngineConstants(), this);
}

std::string GLShaderManager::BuildDeformShaderText( const std::string& steps )
{
	std::string shaderText;

	shaderText = steps + "\n";

	// We added a lot of stuff but if we do something bad
	// in the GLSL shaders then we want the proper line
	// so we have to reset the line counting.
	shaderText += "#line 0\n";
	shaderText += GetShaderText("glsl/deformVertexes_vp.glsl");
	return shaderText;
}

int GLShaderManager::getDeformShaderIndex( deformStage_t *deforms, int numDeforms )
{
	std::string steps = BuildDeformSteps( deforms, numDeforms );
	int index = _deformShaderLookup[ steps ] - 1;

	if( index < 0 )
	{
		// compile new deform shader
		std::string shaderText = GLShaderManager::BuildDeformShaderText( steps );
		_deformShaders.push_back(CompileShader( "deformVertexes",
							shaderText,
							{ &GLVersionDeclaration,
							  &GLVertexHeader },
							GL_VERTEX_SHADER ) );
		index = _deformShaders.size();
		_deformShaderLookup[ steps ] = index--;
	}

	return index;
}

std::string     GLShaderManager::BuildGPUShaderText( Str::StringRef mainShaderName,
    Str::StringRef libShaderNames,
    GLenum shaderType ) const
{
	char        filename[ MAX_QPATH ];
	char        *token;

	const char        *libNames = libShaderNames.c_str();

	GL_CheckErrors();

	std::string libs; // All libs concatenated
	libs.reserve(8192); // Might help, just an estimate.
	while ( true )
	{
		token = COM_ParseExt2( &libNames, false );

		if ( !token[ 0 ] )
		{
			break;
		}

		if ( shaderType == GL_VERTEX_SHADER )
			Com_sprintf( filename, sizeof( filename ), "glsl/%s_vp.glsl", token );
		else
			Com_sprintf( filename, sizeof( filename ), "glsl/%s_fp.glsl", token );

		libs += GetShaderText(filename);
		// We added a lot of stuff but if we do something bad
		// in the GLSL shaders then we want the proper line
		// so we have to reset the line counting.
		libs += "#line 0\n";
	}

	// load main() program
	if ( shaderType == GL_VERTEX_SHADER )
		Com_sprintf( filename, sizeof( filename ), "glsl/%s_vp.glsl", mainShaderName.c_str() );
	else
		Com_sprintf( filename, sizeof( filename ), "glsl/%s_fp.glsl", mainShaderName.c_str() );

	std::string env;
	env.reserve( 1024 ); // Might help, just an estimate.

	if ( glConfig2.textureRGAvailable )
		AddDefine( env, "TEXTURE_RG", 1 );

	if ( glConfig2.uniformBufferObjectAvailable )
		AddDefine( env, "UNIFORM_BUFFER_OBJECT", 1 );

	if ( glConfig2.textureIntegerAvailable )
		AddDefine( env, "TEXTURE_INTEGER", 1 );

	AddDefine( env, "r_AmbientScale", r_ambientScale->value );
	AddDefine( env, "r_SpecularScale", r_specularScale->value );
	AddDefine( env, "r_zNear", r_znear->value );

	AddDefine( env, "M_PI", static_cast<float>( M_PI ) );
	AddDefine( env, "MAX_SHADOWMAPS", MAX_SHADOWMAPS );
	AddDefine( env, "MAX_REF_LIGHTS", MAX_REF_LIGHTS );
	AddDefine( env, "TILE_SIZE", TILE_SIZE );

	AddDefine( env, "r_FBufSize", glConfig.vidWidth, glConfig.vidHeight );

	AddDefine( env, "r_tileStep", glState.tileStep[ 0 ], glState.tileStep[ 1 ] );

	// We added a lot of stuff but if we do something bad
	// in the GLSL shaders then we want the proper line
	// so we have to reset the line counting.
	env += "#line 0\n";

	std::string shaderText = env + libs + GetShaderText(filename);

	return shaderText;
}

static bool IsUnusedPermutation( const char *compileMacros )
{
	const char* token;
	while ( *( token = COM_ParseExt2( &compileMacros, false ) ) )
	{
		if ( strcmp( token, "USE_NORMAL_MAPPING" ) == 0 )
		{
			if ( r_normalMapping->integer == 0 ) return true;
		}
		else if ( strcmp( token, "USE_DELUXE_MAPPING" ) == 0 )
		{
			if ( r_deluxeMapping->integer == 0 ) return true;
		}
		else if ( strcmp( token, "USE_PHYSICAL_MAPPING" ) == 0 )
		{
			if ( r_physicalMapping->integer == 0 ) return true;
		}
		else if ( strcmp( token, "USE_REFLECTIVE_SPECULAR" ) == 0 )
		{
			/* FIXME: add to the following test: && r_physicalMapping->integer == 0
			when reflective specular is implemented for physical mapping too
			see https://github.com/DaemonEngine/Daemon/issues/355 */
			if ( r_specularMapping->integer == 0 ) return true;
		}
		else if ( strcmp( token, "USE_RELIEF_MAPPING" ) == 0 )
		{
			if ( r_reliefMapping->integer == 0 ) return true;
		}
		else if ( strcmp( token, "USE_HEIGHTMAP_IN_NORMALMAP" ) == 0 )
		{
			if ( r_reliefMapping->integer == 0 && r_normalMapping->integer == 0 ) return true;
		}
	}

	return false;
}

void GLShaderManager::buildPermutation( GLShader *shader, int macroIndex, int deformIndex )
{
	std::string compileMacros;
	int  startTime = ri.Milliseconds();
	int  endTime;
	size_t i = macroIndex + ( deformIndex << shader->_compileMacros.size() );

	// program already exists
	if ( i < shader->_shaderPrograms.size() &&
	     shader->_shaderPrograms[ i ].program )
	{
		return;
	}

	if( shader->GetCompileMacrosString( macroIndex, compileMacros ) )
	{
		shader->BuildShaderCompileMacros( compileMacros );

		if ( IsUnusedPermutation( compileMacros.c_str() ) )
			return;

		if( i >= shader->_shaderPrograms.size() )
			shader->_shaderPrograms.resize( (deformIndex + 1) << shader->_compileMacros.size() );

		shaderProgram_t *shaderProgram = &shader->_shaderPrograms[ i ];
		shaderProgram->attribs = shader->_vertexAttribsRequired; // | _vertexAttribsOptional;

		if( deformIndex > 0 )
		{
			shaderProgram_t *baseShader = &shader->_shaderPrograms[ macroIndex ];
			if( !baseShader->VS || !baseShader->FS )
				CompileGPUShaders( shader, baseShader, compileMacros );

			shaderProgram->program = glCreateProgram();
			glAttachShader( shaderProgram->program, baseShader->VS );
			glAttachShader( shaderProgram->program, _deformShaders[ deformIndex ] );
			glAttachShader( shaderProgram->program, baseShader->FS );

			BindAttribLocations( shaderProgram->program );
			LinkProgram( shaderProgram->program );
		}
		else if ( !LoadShaderBinary( shader, i ) )
		{
			CompileAndLinkGPUShaderProgram(	shader, shaderProgram, compileMacros, deformIndex );
			SaveShaderBinary( shader, i );
		}

		UpdateShaderProgramUniformLocations( shader, shaderProgram );
		GL_BindProgram( shaderProgram );
		shader->SetShaderProgramUniforms( shaderProgram );
		GL_BindProgram( nullptr );

		GL_CheckErrors();

		endTime = ri.Milliseconds();
		_totalBuildTime += ( endTime - startTime );
	}
}

void GLShaderManager::buildAll()
{
	while ( !_shaderBuildQueue.empty() )
	{
		GLShader& shader = *_shaderBuildQueue.front();

		std::string shaderName = shader.GetMainShaderName();

		size_t numPermutations = static_cast<size_t>(1) << shader.GetNumOfCompiledMacros();
		size_t i;

		for( i = 0; i < numPermutations; i++ )
		{
			buildPermutation( &shader, i, 0 );
		}

		_shaderBuildQueue.pop();
	}

	Log::Notice( "glsl shaders took %d msec to build", _totalBuildTime );
}

void GLShaderManager::InitShader( GLShader *shader )
{
	shader->_shaderPrograms = std::vector<shaderProgram_t>( static_cast<size_t>(1) << shader->_compileMacros.size() );

	shader->_uniformStorageSize = 0;
	for ( std::size_t i = 0; i < shader->_uniforms.size(); i++ )
	{
		GLUniform *uniform = shader->_uniforms[ i ];
		uniform->SetLocationIndex( i );
		uniform->SetFirewallIndex( shader->_uniformStorageSize );
		shader->_uniformStorageSize += uniform->GetSize();
	}

	for ( std::size_t i = 0; i < shader->_uniformBlocks.size(); i++ )
	{
		GLUniformBlock *uniformBlock = shader->_uniformBlocks[ i ];
		uniformBlock->SetLocationIndex( i );
	}

	std::string vertexInlines;
	shader->BuildShaderVertexLibNames( vertexInlines );

	std::string fragmentInlines;
	shader->BuildShaderFragmentLibNames( fragmentInlines );

	shader->_vertexShaderText = BuildGPUShaderText( shader->GetMainShaderName(), vertexInlines, GL_VERTEX_SHADER );
	shader->_fragmentShaderText = BuildGPUShaderText( shader->GetMainShaderName(), fragmentInlines, GL_FRAGMENT_SHADER );
	std::string combinedShaderText = GLEngineConstants.getText() + shader->_vertexShaderText + shader->_fragmentShaderText;

	shader->_checkSum = Com_BlockChecksum( combinedShaderText.c_str(), combinedShaderText.length() );
}

bool GLShaderManager::LoadShaderBinary( GLShader *shader, size_t programNum )
{
#ifdef GL_ARB_get_program_binary
	GLint          success;
	const byte    *binaryptr;
	GLBinaryHeader shaderHeader;

	if (!GetShaderPath().empty())
		return false;

	// don't even try if the necessary functions aren't available
	if( !glConfig2.getProgramBinaryAvailable )
		return false;

	if (_shaderBinaryCacheInvalidated)
		return false;

	std::error_code err;

	std::string shaderFilename = Str::Format("glsl/%s/%s_%u.bin", shader->GetName(), shader->GetName(), (unsigned int)programNum);
	FS::File shaderFile = FS::HomePath::OpenRead(shaderFilename, err);
	if (err)
		return false;

	std::string shaderData = shaderFile.ReadAll(err);
	if (err)
		return false;

	if (shaderData.size() < sizeof(shaderHeader))
		return false;

	binaryptr = reinterpret_cast<const byte*>(shaderData.data());

	// get the shader header from the file
	memcpy( &shaderHeader, binaryptr, sizeof( shaderHeader ) );
	binaryptr += sizeof( shaderHeader );

	// check if the header struct is the correct format
	// and the binary was produced by the same gl driver
	if (shaderHeader.version != GL_SHADER_VERSION || shaderHeader.driverVersionHash != _driverVersionHash)
	{
		// These two fields should be the same for all shaders. So if there is a mismatch,
		// don't bother opening any of the remaining files.
		Log::Notice("Invalidating shader binary cache");
		_shaderBinaryCacheInvalidated = true;
		return false;
	}

	// make sure this shader uses the same number of macros
	if ( shaderHeader.numMacros != shader->GetNumOfCompiledMacros() )
		return false;

	// make sure this shader uses the same macros
	for ( unsigned int i = 0; i < shaderHeader.numMacros; i++ )
	{
		if ( shader->_compileMacros[ i ]->GetType() != shaderHeader.macros[ i ] )
			return false;
	}

	// make sure the checksums for the source code match
	if ( shaderHeader.checkSum != shader->_checkSum )
		return false;

	// load the shader
	shaderProgram_t *shaderProgram = &shader->_shaderPrograms[ programNum ];
	shaderProgram->program = glCreateProgram();
	glProgramBinary( shaderProgram->program, shaderHeader.binaryFormat, binaryptr, shaderHeader.binaryLength );
	glGetProgramiv( shaderProgram->program, GL_LINK_STATUS, &success );

	if ( !success )
		return false;

	return true;
#else
	return false;
#endif
}
void GLShaderManager::SaveShaderBinary( GLShader *shader, size_t programNum )
{
#ifdef GL_ARB_get_program_binary
	GLint                 binaryLength;
	GLuint                binarySize = 0;
	byte                  *binary;
	byte                  *binaryptr;
	GLBinaryHeader        shaderHeader{}; // Zero init.
	shaderProgram_t       *shaderProgram;

	if (!GetShaderPath().empty())
		return;

	// don't even try if the necessary functions aren't available
	if( !glConfig2.getProgramBinaryAvailable )
	{
		return;
	}

	shaderProgram = &shader->_shaderPrograms[ programNum ];

	// find output size
	binarySize += sizeof( shaderHeader );
	glGetProgramiv( shaderProgram->program, GL_PROGRAM_BINARY_LENGTH, &binaryLength );

	// The binary length may be 0 if there is an error.
	if ( binaryLength <= 0 )
	{
		return;
	}

	binarySize += binaryLength;

	binaryptr = binary = ( byte* )ri.Hunk_AllocateTempMemory( binarySize );

	// reserve space for the header
	binaryptr += sizeof( shaderHeader );

	// get the program binary and write it to the buffer
	glGetProgramBinary( shaderProgram->program, binaryLength, nullptr, &shaderHeader.binaryFormat, binaryptr );

	// set the header
	shaderHeader.version = GL_SHADER_VERSION;
	shaderHeader.numMacros = shader->_compileMacros.size();

	for ( unsigned int i = 0; i < shaderHeader.numMacros; i++ )
	{
		shaderHeader.macros[ i ] = shader->_compileMacros[ i ]->GetType();
	}

	shaderHeader.binaryLength = binaryLength;
	shaderHeader.checkSum = shader->_checkSum;
	shaderHeader.driverVersionHash = _driverVersionHash;

	// write the header to the buffer
	memcpy(binary, &shaderHeader, sizeof( shaderHeader ) );

	auto fileName = Str::Format("glsl/%s/%s_%u.bin", shader->GetName(), shader->GetName(), (unsigned int)programNum);
	ri.FS_WriteFile(fileName.c_str(), binary, binarySize);

	ri.Hunk_FreeTempMemory( binary );
#endif
}

void GLShaderManager::CompileGPUShaders( GLShader *shader, shaderProgram_t *program,
					 const std::string &compileMacros )
{
	// permutation macros
	std::string macrosString;

	const char* compileMacrosP = compileMacros.c_str();
	while ( true )
	{
		const char *token = COM_ParseExt2( &compileMacrosP, false );

		if ( !token[ 0 ] )
		{
			break;
		}

		macrosString += Str::Format( "#ifndef %s\n#define %s 1\n#endif\n", token, token );
	}

	Log::Debug( "building %s shader permutation with macro: %s",
		shader->GetMainShaderName(),
		compileMacros.empty() ? "none" : compileMacros );

	// add them
	std::string vertexShaderTextWithMacros = macrosString + shader->_vertexShaderText;
	std::string fragmentShaderTextWithMacros = macrosString + shader->_fragmentShaderText;
	program->VS = CompileShader( shader->GetName(),
				     vertexShaderTextWithMacros,
				     { &GLVersionDeclaration,
				       &GLVertexHeader,
				       &GLCompatHeader,
				       &GLEngineConstants },
				     GL_VERTEX_SHADER );
	program->FS = CompileShader( shader->GetName(),
				     fragmentShaderTextWithMacros,
				     { &GLVersionDeclaration,
				       &GLFragmentHeader,
				       &GLCompatHeader,
				       &GLEngineConstants },
				     GL_FRAGMENT_SHADER );
}

void GLShaderManager::CompileAndLinkGPUShaderProgram( GLShader *shader, shaderProgram_t *program,
						      Str::StringRef compileMacros, int deformIndex )
{
	GLShaderManager::CompileGPUShaders( shader, program, compileMacros );

	program->program = glCreateProgram();
	glAttachShader( program->program, program->VS );
	glAttachShader( program->program, _deformShaders[ deformIndex ] );
	glAttachShader( program->program, program->FS );

	BindAttribLocations( program->program );
	LinkProgram( program->program );
}

GLuint GLShaderManager::CompileShader( Str::StringRef programName,
				       Str::StringRef shaderText,
				       std::initializer_list<const GLHeader *> headers,
				       GLenum shaderType ) const
{
	GLuint shader = glCreateShader( shaderType );
	std::vector<const GLchar*> texts(headers.size() + 1);
	std::vector<GLint> lengths(headers.size() + 1);
	int i;

	i = 0;
	for(const GLHeader *hdr : headers) {
	  texts[i++] = hdr->getText().data();
	}
	texts[i++] = shaderText.data();

	i = 0;
	for(const GLHeader *hdr : headers) {
	  lengths[i++] = (GLint)hdr->getText().size();
	}
	lengths[i++] = (GLint)shaderText.size();

	GL_CheckErrors();

	glShaderSource( shader, i, texts.data(), lengths.data() );

	// compile shader
	glCompileShader( shader );

	GL_CheckErrors();

	// check if shader compiled
	GLint compiled;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );

	if ( !compiled )
	{
		PrintShaderSource( programName, shader );
		PrintInfoLog( shader );
		ThrowShaderError(Str::Format("Couldn't compile %s shader: %s", ( shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment", programName));
	}

	return shader;
}

void GLShaderManager::PrintShaderSource( Str::StringRef programName, GLuint object ) const
{
	char        *dump;
	int         maxLength = 0;

	glGetShaderiv( object, GL_SHADER_SOURCE_LENGTH, &maxLength );

	dump = ( char * ) ri.Hunk_AllocateTempMemory( maxLength );

	glGetShaderSource( object, maxLength, &maxLength, dump );

	std::string buffer;
	std::string delim("\n");
	std::string src(dump);

	ri.Hunk_FreeTempMemory( dump );

	int i = 0;
	size_t pos = 0;
	while ( ( pos = src.find(delim) ) != std::string::npos )
	{
		std::string line = src.substr( 0, pos );
		if ( line.compare( "#line 0" ) == 0 )
		{
			i = 0;
		}

		std::string number = std::to_string(i);

		int p = 4 - number.length();
		p = p < 0 ? 0 : p;
		number.insert( number.begin(), p, ' ' );

		buffer.append(number);
		buffer.append(": ");
		buffer.append(line);
		buffer.append(delim);

		src.erase(0, pos + delim.length());

		i++;
	}

	Log::Warn("Source for shader program %s:\n%s", programName, buffer.c_str());
}

void GLShaderManager::PrintInfoLog( GLuint object) const
{
	char        *msg;
	int         maxLength = 0;
	std::string msgText;

	if ( glIsShader( object ) )
	{
		glGetShaderiv( object, GL_INFO_LOG_LENGTH, &maxLength );
	}
	else if ( glIsProgram( object ) )
	{
		glGetProgramiv( object, GL_INFO_LOG_LENGTH, &maxLength );
	}
	else
	{
		Log::Warn( "object is not a shader or program\n" );
		return;
	}

	msg = ( char * ) ri.Hunk_AllocateTempMemory( maxLength );

	if ( glIsShader( object ) )
	{
		glGetShaderInfoLog( object, maxLength, &maxLength, msg );
		msgText = "Compile log:";
	}
	else if ( glIsProgram( object ) )
	{
		glGetProgramInfoLog( object, maxLength, &maxLength, msg );
		msgText = "Link log:";
	}
	if (maxLength > 0)
		msgText += '\n';
	msgText += msg;
	Log::Warn(msgText);

	ri.Hunk_FreeTempMemory( msg );
}

void GLShaderManager::LinkProgram( GLuint program ) const
{
	GLint linked;

#ifdef GL_ARB_get_program_binary
	// Apparently, this is necessary to get the binary program via glGetProgramBinary
	if( glConfig2.getProgramBinaryAvailable )
	{
		glProgramParameteri( program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
	}
#endif
	glLinkProgram( program );

	glGetProgramiv( program, GL_LINK_STATUS, &linked );

	if ( !linked )
	{
		PrintInfoLog( program );
		ThrowShaderError( "Shaders failed to link!" );
	}
}

void GLShaderManager::BindAttribLocations( GLuint program ) const
{
	for ( uint32_t i = 0; i < ATTR_INDEX_MAX; i++ )
	{
		glBindAttribLocation( program, i, attributeNames[ i ] );
	}
}

// reflective specular not implemented for PBR yet
bool GLCompileMacro_USE_REFLECTIVE_SPECULAR::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ( ( permutation & macro->GetBit() ) != 0 && (macro->GetType() == USE_PHYSICAL_MAPPING || macro->GetType() == USE_VERTEX_SPRITE) )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_VERTEX_SKINNING::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		//if(GLCompileMacro_USE_VERTEX_ANIMATION* m = dynamic_cast<GLCompileMacro_USE_VERTEX_ANIMATION*>(macro))
		if ( ( permutation & macro->GetBit() ) != 0 && (macro->GetType() == USE_VERTEX_ANIMATION || macro->GetType() == USE_VERTEX_SPRITE) )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_VERTEX_SKINNING::MissesRequiredMacros( size_t /*permutation*/, const std::vector< GLCompileMacro * > &/*macros*/ ) const
{
	return !glConfig2.vboVertexSkinningAvailable;
}

bool GLCompileMacro_USE_VERTEX_ANIMATION::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ( ( permutation & macro->GetBit() ) != 0 && (macro->GetType() == USE_VERTEX_SKINNING || macro->GetType() == USE_VERTEX_SPRITE) )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

uint32_t GLCompileMacro_USE_VERTEX_ANIMATION::GetRequiredVertexAttributes() const
{
	uint32_t attribs = ATTR_POSITION2 | ATTR_QTANGENT2;

	return attribs;
}

bool GLCompileMacro_USE_VERTEX_SPRITE::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ( ( permutation & macro->GetBit() ) != 0 && (macro->GetType() == USE_VERTEX_SKINNING || macro->GetType() == USE_VERTEX_ANIMATION || macro->GetType() == USE_DEPTH_FADE))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_TCGEN_ENVIRONMENT::HasConflictingMacros( size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_TCGEN_LIGHTMAP))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_TCGEN_LIGHTMAP::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_TCGEN_ENVIRONMENT))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_DEPTH_FADE::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_VERTEX_SPRITE))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLShader::GetCompileMacrosString( size_t permutation, std::string &compileMacrosOut ) const
{
	compileMacrosOut.clear();

	for (const GLCompileMacro* macro : _compileMacros)
	{
		if ( permutation & macro->GetBit() )
		{
			if ( macro->HasConflictingMacros( permutation, _compileMacros ) )
			{
				//Log::Notice("conflicting macro! canceling '%s'", macro->GetName());
				return false;
			}

			if ( macro->MissesRequiredMacros( permutation, _compileMacros ) )
				return false;

			compileMacrosOut += macro->GetName();
			compileMacrosOut += " ";
		}
	}

	return true;
}

int GLShader::SelectProgram()
{
	int    index = 0;

	size_t numMacros = _compileMacros.size();

	for ( size_t i = 0; i < numMacros; i++ )
	{
		if ( _activeMacros & BIT( i ) )
		{
			index += BIT( i );
		}
	}

	return index;
}

void GLShader::BindProgram( int deformIndex )
{
	int macroIndex = SelectProgram();
	size_t index = macroIndex + ( size_t(deformIndex) << _compileMacros.size() );

	// program may not be loaded yet because the shader manager hasn't yet gotten to it
	// so try to load it now
	if ( index >= _shaderPrograms.size() || !_shaderPrograms[ index ].program )
	{
		_shaderManager->buildPermutation( this, macroIndex, deformIndex );
	}

	// program is still not loaded
	if ( index >= _shaderPrograms.size() || !_shaderPrograms[ index ].program )
	{
		std::string activeMacros;
		size_t      numMacros = _compileMacros.size();

		for ( size_t j = 0; j < numMacros; j++ )
		{
			GLCompileMacro *macro = _compileMacros[ j ];

			int           bit = macro->GetBit();

			if ( IsMacroSet( bit ) )
			{
				activeMacros += macro->GetName();
				activeMacros += " ";
			}
		}

		ThrowShaderError(Str::Format("Invalid shader configuration: shader = '%s', macros = '%s'", _name, activeMacros ));
	}

	_currentProgram = &_shaderPrograms[ index ];

	if ( r_logFile->integer )
	{
		std::string macros;

		this->GetCompileMacrosString( index, macros );

		auto msg = Str::Format("--- GL_BindProgram( name = '%s', macros = '%s' ) ---\n", this->GetName(), macros);
		GLimp_LogComment(msg.c_str());
	}

	GL_BindProgram( _currentProgram );
}

void GLShader::SetRequiredVertexPointers()
{
	uint32_t macroVertexAttribs = 0;
	size_t   numMacros = _compileMacros.size();

	for ( size_t j = 0; j < numMacros; j++ )
	{
		GLCompileMacro *macro = _compileMacros[ j ];

		int           bit = macro->GetBit();

		if ( IsMacroSet( bit ) )
		{
			macroVertexAttribs |= macro->GetRequiredVertexAttributes();
		}
	}

	GL_VertexAttribsState( ( _vertexAttribsRequired | _vertexAttribs | macroVertexAttribs ) );  // & ~_vertexAttribsUnsupported);
}

GLShader_generic2D::GLShader_generic2D( GLShaderManager *manager ) :
	GLShader( "generic", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_AlphaThreshold( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorModulate( this ),
	u_Color( this ),
	u_DepthScale( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_DEPTH_FADE( this ),
	GLCompileMacro_USE_ALPHA_TESTING( this )
{
}

void GLShader_generic2D::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation vertexSprite ";
}

void GLShader_generic2D::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "generic2D";
}

void GLShader_generic2D::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 1 );
}

GLShader_generic::GLShader_generic( GLShaderManager *manager ) :
	GLShader( "generic", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ViewUp( this ),
	u_AlphaThreshold( this ),
	u_ModelMatrix( this ),
	u_ProjectionMatrixTranspose( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorModulate( this ),
	u_Color( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_DepthScale( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_VERTEX_SPRITE( this ),
	GLCompileMacro_USE_TCGEN_ENVIRONMENT( this ),
	GLCompileMacro_USE_TCGEN_LIGHTMAP( this ),
	GLCompileMacro_USE_DEPTH_FADE( this ),
	GLCompileMacro_USE_ALPHA_TESTING( this )
{
}

void GLShader_generic::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation vertexSprite ";
}

void GLShader_generic::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 1 );
}

GLShader_lightMapping::GLShader_lightMapping( GLShaderManager *manager ) :
	GLShader( "lightMapping",
	ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR, manager ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_ColorModulate( this ),
	u_Color( this ),
	u_AlphaThreshold( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_EnvironmentInterpolation( this ),
	u_LightWrapAround( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	u_numLights( this ),
	u_Lights( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_BSP_SURFACE( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_LIGHT_MAPPING( this ),
	GLCompileMacro_USE_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_REFLECTIVE_SPECULAR( this ),
	GLCompileMacro_USE_PHYSICAL_MAPPING( this )
{
}

void GLShader_lightMapping::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation vertexSprite ";
}

void GLShader_lightMapping::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "computeLight reliefMapping";
}

void GLShader_lightMapping::BuildShaderCompileMacros( std::string& /*compileMacros*/ )
{
}

void GLShader_lightMapping::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DiffuseMap" ), BIND_DIFFUSEMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_NormalMap" ), BIND_NORMALMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_HeightMap" ), BIND_HEIGHTMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_MaterialMap" ),  BIND_MATERIALMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_LightMap" ), BIND_LIGHTMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DeluxeMap" ), BIND_DELUXEMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_GlowMap" ), BIND_GLOWMAP );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_EnvironmentMap0" ), BIND_ENVIRONMENTMAP0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_EnvironmentMap1" ), BIND_ENVIRONMENTMAP1 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_LightTiles" ), BIND_LIGHTTILES );
	if( !glConfig2.uniformBufferObjectAvailable ) {
		glUniform1i( glGetUniformLocation( shaderProgram->program, "u_Lights" ), BIND_LIGHTS );
	}
}

GLShader_shadowFill::GLShader_shadowFill( GLShaderManager *manager ) :
	GLShader( "shadowFill", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_AlphaThreshold( this ),
	u_LightOrigin( this ),
	u_LightRadius( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Color( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_LIGHT_DIRECTIONAL( this )
{
}

void GLShader_shadowFill::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation ";
}

void GLShader_shadowFill::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_reflection::GLShader_reflection( GLShaderManager *manager ):
	GLShader("reflection", "reflection_CB", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this )
{
}

void GLShader_reflection::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation ";
}

void GLShader_reflection::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "reliefMapping";
}

void GLShader_reflection::BuildShaderCompileMacros( std::string& )
{
}

void GLShader_reflection::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_NormalMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_HeightMap" ), 15 );
}

GLShader_skybox::GLShader_skybox( GLShaderManager *manager ) :
	GLShader( "skybox", ATTR_POSITION, manager ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this )
{
}

void GLShader_skybox::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_fogQuake3::GLShader_fogQuake3( GLShaderManager *manager ) :
	GLShader( "fogQuake3", ATTR_POSITION | ATTR_QTANGENT, manager ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Color( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_FogDistanceVector( this ),
	u_FogDepthVector( this ),
	u_FogEyeT( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this )
{
}

void GLShader_fogQuake3::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation ";
}

void GLShader_fogQuake3::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_fogGlobal::GLShader_fogGlobal( GLShaderManager *manager ) :
	GLShader( "fogGlobal", ATTR_POSITION, manager ),
	u_ViewOrigin( this ),
	u_ViewMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectMatrix( this ),
	u_Color( this ),
	u_FogDistanceVector( this ),
	u_FogDepthVector( this )
{
}

void GLShader_fogGlobal::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 1 );
}

GLShader_heatHaze::GLShader_heatHaze( GLShaderManager *manager ) :
	GLShader( "heatHaze", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ViewUp( this ),
	u_DeformMagnitude( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ModelViewMatrixTranspose( this ),
	u_ProjectionMatrixTranspose( this ),
	u_ColorModulate( this ),
	u_Color( this ),
	u_Bones( this ),
	u_NormalScale( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_VERTEX_SPRITE( this )
{
}

void GLShader_heatHaze::BuildShaderVertexLibNames( std::string& vertexInlines )
{
	vertexInlines += "vertexSimple vertexSkinning vertexAnimation vertexSprite ";
}

void GLShader_heatHaze::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "reliefMapping";
}

void GLShader_heatHaze::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_NormalMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_HeightMap" ), 15 );
}

GLShader_screen::GLShader_screen( GLShaderManager *manager ) :
	GLShader( "screen", ATTR_POSITION, manager ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_screen::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 0 );
}

GLShader_portal::GLShader_portal( GLShaderManager *manager ) :
	GLShader( "portal", ATTR_POSITION, manager ),
	u_ModelViewMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_PortalRange( this )
{
}

void GLShader_portal::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 0 );
}

GLShader_contrast::GLShader_contrast( GLShaderManager *manager ) :
	GLShader( "contrast", ATTR_POSITION, manager ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_contrast::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_cameraEffects::GLShader_cameraEffects( GLShaderManager *manager ) :
	GLShader( "cameraEffects", ATTR_POSITION | ATTR_TEXCOORD, manager ),
	u_ColorModulate( this ),
	u_TextureMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_DeformMagnitude( this ),
	u_InverseGamma( this )
{
}

void GLShader_cameraEffects::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 3 );
}

GLShader_blurX::GLShader_blurX( GLShaderManager *manager ) :
	GLShader( "blurX", ATTR_POSITION, manager ),
	u_ModelViewProjectionMatrix( this ),
	u_DeformMagnitude( this ),
	u_TexScale( this )
{
}

void GLShader_blurX::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_blurY::GLShader_blurY( GLShaderManager *manager ) :
	GLShader( "blurY", ATTR_POSITION, manager ),
	u_ModelViewProjectionMatrix( this ),
	u_DeformMagnitude( this ),
	u_TexScale( this )
{
}

void GLShader_blurY::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

GLShader_debugShadowMap::GLShader_debugShadowMap( GLShaderManager *manager ) :
	GLShader( "debugShadowMap", ATTR_POSITION, manager ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_debugShadowMap::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 0 );
}

GLShader_liquid::GLShader_liquid( GLShaderManager *manager ) :
	GLShader( "liquid", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_RefractionIndex( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectMatrix( this ),
	u_FresnelPower( this ),
	u_FresnelScale( this ),
	u_FresnelBias( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_FogDensity( this ),
	u_FogColor( this ),
	u_SpecularExponent( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this )
{
}

void GLShader_liquid::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "computeLight reliefMapping";
}

void GLShader_liquid::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_CurrentMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_PortalMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 2 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_NormalMap" ), 3 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_LightGrid1" ), 6 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_LightGrid2" ), 7 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_HeightMap" ), 15 );
}

GLShader_motionblur::GLShader_motionblur( GLShaderManager *manager ) :
	GLShader( "motionblur", ATTR_POSITION, manager ),
	u_blurVec( this )
{
}

void GLShader_motionblur::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 1 );
}

GLShader_ssao::GLShader_ssao( GLShaderManager *manager ) :
	GLShader( "ssao", ATTR_POSITION, manager ),
	u_zFar( this )
{
}

void GLShader_ssao::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 0 );
}

GLShader_depthtile1::GLShader_depthtile1( GLShaderManager *manager ) :
	GLShader( "depthtile1", ATTR_POSITION, manager ),
	u_zFar( this )
{
}

void GLShader_depthtile1::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 0 );
}

GLShader_depthtile2::GLShader_depthtile2( GLShaderManager *manager ) :
	GLShader( "depthtile2", ATTR_POSITION, manager )
{
}

void GLShader_depthtile2::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 0 );
}

GLShader_lighttile::GLShader_lighttile( GLShaderManager *manager ) :
	GLShader( "lighttile", ATTR_POSITION | ATTR_TEXCOORD, manager ),
	u_ModelMatrix( this ),
	u_numLights( this ),
	u_lightLayer( this ),
	u_Lights( this ),
	u_zFar( this )
{
}

void GLShader_lighttile::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_DepthMap" ), 0 );

	if( !glConfig2.uniformBufferObjectAvailable ) {
		glUniform1i( glGetUniformLocation( shaderProgram->program, "u_Lights" ), 1 );
	}
}

GLShader_fxaa::GLShader_fxaa( GLShaderManager *manager ) :
	GLShader( "fxaa", ATTR_POSITION, manager )
{
}

void GLShader_fxaa::SetShaderProgramUniforms( shaderProgram_t *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->program, "u_ColorMap" ), 0 );
}

void GLShader_fxaa::BuildShaderFragmentLibNames( std::string& fragmentInlines )
{
	fragmentInlines += "fxaa3_11";
}
