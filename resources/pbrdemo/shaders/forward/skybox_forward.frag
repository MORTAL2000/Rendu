
in INTERFACE {
	vec3 pos; ///< Position in model space.
} In ;

layout(binding = 0) uniform samplerCube texture0; ///< Cubemap color.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Use the normalized position to read in the cube map. */
void main(){
	fragColor.rgb = textureLod(texture0, normalize(In.pos), 0.0).rgb;
	fragColor.a = -1.0;
}
