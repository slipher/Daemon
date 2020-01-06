/*
===========================================================================
Copyright (C) 2007-2009 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/* liquid_fp.glsl */

uniform sampler2D	u_CurrentMap;
uniform sampler2D	u_PortalMap;
uniform sampler2D	u_DepthMap;
uniform vec3		u_ViewOrigin;
uniform float		u_FogDensity;
uniform vec3		u_FogColor;
uniform float		u_RefractionIndex;
uniform float		u_FresnelPower;
uniform float		u_FresnelScale;
uniform float		u_FresnelBias;
uniform mat4		u_ModelMatrix;
uniform mat4		u_UnprojectMatrix;

uniform sampler3D       u_LightGrid1;
uniform sampler3D       u_LightGrid2;
uniform vec3            u_LightGridOrigin;
uniform vec3            u_LightGridScale;

IN(smooth) vec3		var_Position;
IN(smooth) vec2		var_TexCoords;
IN(smooth) vec3		var_Tangent;
IN(smooth) vec3		var_Binormal;
IN(smooth) vec3		var_Normal;

DECLARE_OUTPUT(vec4)

void ReadLightGrid(in vec3 pos, out vec3 lgtDir,
		   out vec3 ambCol, out vec3 lgtCol ) {
	vec4 texel1 = texture3D(u_LightGrid1, pos);
	vec4 texel2 = texture3D(u_LightGrid2, pos);
	float ambLum, lgtLum;

	texel1.xyz = (texel1.xyz * 255.0 - 128.0) / 127.0;
	texel2.xyzw = texel2.xyzw - 0.5;

	lgtDir = normalize(texel1.xyz);

	lgtLum = 2.0 * length(texel1.xyz) * texel1.w;
	ambLum = 2.0 * texel1.w - lgtLum;

	// YCoCg decode chrominance
	ambCol.g = ambLum + texel2.x;
	ambLum   = ambLum - texel2.x;
	ambCol.r = ambLum + texel2.y;
	ambCol.b = ambLum - texel2.y;

	lgtCol.g = lgtLum + texel2.z;
	lgtLum   = lgtLum - texel2.z;
	lgtCol.r = lgtLum + texel2.w;
	lgtCol.b = lgtLum - texel2.w;
}


void	main()
{
	// compute incident ray
	vec3 viewDir = normalize(u_ViewOrigin - var_Position);

	mat3 tangentToWorldMatrix = mat3(var_Tangent.xyz, var_Binormal.xyz, var_Normal.xyz);
	if(gl_FrontFacing)
	{
		tangentToWorldMatrix = -tangentToWorldMatrix;
	}

	// calculate the screen texcoord in the 0.0 to 1.0 range
	vec2 texScreen = gl_FragCoord.st * r_FBufScale;
	vec2 texNormal = var_TexCoords;

#if defined(USE_PARALLAX_MAPPING)
	// ray intersect in view direction

	// compute texcoords offset from heightmap
	vec2 texOffset = ParallaxTexOffset(texNormal, viewDir, tangentToWorldMatrix);

	texScreen += texOffset;
	texNormal += texOffset;
#endif

	// compute normal in world space from normalmap
	vec3 normal = NormalInWorldSpace(texNormal, tangentToWorldMatrix);

	// compute fresnel term
	float fresnel = clamp(u_FresnelBias + pow(1.0 - dot(viewDir, normal), u_FresnelPower) *
			u_FresnelScale, 0.0, 1.0);

	vec3 refractColor;
	vec4 reflectColor;
	vec4 color;

#if defined(r_liquidMapping)
	refractColor = texture2D(u_CurrentMap, texScreen).rgb;
	reflectColor.rgb = texture2D(u_PortalMap, texScreen).rgb;
	reflectColor.a = 1.0;
#else // !r_liquidMapping
	// dummy fallback color
	refractColor = vec3(0.7, 0.7, 0.7);
	reflectColor = vec4(0.7, 0.7, 0.7, 1.0);
#endif // !r_liquidMapping

	color.rgb = mix(refractColor, reflectColor.rgb, fresnel);
	color.a = 1.0;

	if(u_FogDensity > 0.0)
	{
		// reconstruct vertex position in world space
		float depth = texture2D(u_DepthMap, texScreen).r;
		vec4 P = u_UnprojectMatrix * vec4(gl_FragCoord.xy, depth, 1.0);
		P.xyz /= P.w;

		// calculate fog distance
		float fogDistance = distance(P.xyz, var_Position);

		// calculate fog exponent
		float fogExponent = fogDistance * u_FogDensity;

		// calculate fog factor
		float fogFactor = exp2(-abs(fogExponent));

		color.rgb = mix(u_FogColor, color.rgb, fogFactor);
	}

	vec3 lightDir;
	vec3 ambientColor;
	vec3 lightColor;

	ReadLightGrid( (var_Position - u_LightGridOrigin) * u_LightGridScale,
		       lightDir, ambientColor, lightColor );

	vec4 diffuse = vec4(0.0, 0.0, 0.0, 1.0);

	// compute the specular term
	computeLight(lightDir, normal, viewDir, lightColor, diffuse, reflectColor, color);

	outputColor = color;

#if defined(r_showNormalMaps)
	// convert normal to [0,1] color space
	normal = normal * 0.5 + 0.5;
	outputColor = vec4(normal, 1.0);
#endif
}
