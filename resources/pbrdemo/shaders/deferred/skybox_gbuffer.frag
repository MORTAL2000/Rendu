
#define MATERIAL_ID 0 ///< The material ID.

in INTERFACE {
	vec3 pos; ///< Position in model space.
} In ;

layout(binding = 0) uniform samplerCube texture0; ///< Albedo.

layout (location = 0) out vec4 fragColor; ///< Color.
layout (location = 1) out vec3 fragNormal; ///< View space normal.
layout (location = 2) out vec3 fragEffects; ///< Effects.

/** Transfer albedo along with the material ID, and output a null normal. */
void main(){

	fragColor.rgb = textureLod(texture0, normalize(In.pos), 0.0).rgb;
	fragColor.a = MATERIAL_ID;
	fragNormal = vec3(0.5);
	fragEffects = vec3(0.0);

}
