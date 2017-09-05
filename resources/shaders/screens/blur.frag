#version 330

// Input: UV coordinates
in INTERFACE {
	vec2 uv;
} In ;

// Uniforms: the texture, inverse of the screen size.
uniform sampler2D screenTexture;
uniform vec2 inverseScreenSize; // contains the texture coordinates offset along the correct axis.

// Output: the fragment color
out vec3 fragColor;


void main(){
	// 3 taps only are required for a 5x5 gaussian blur, thanks to separation into
	// an horizontal and vertical 1D blurs, and to bilinear interpolation.
	vec3 col = texture(screenTexture, In.uv).rgb * 6.0/16.0;
	col += texture(screenTexture, In.uv - inverseScreenSize).rgb * 5.0/16.0;
	col += texture(screenTexture, In.uv + inverseScreenSize).rgb * 5.0/16.0;
	fragColor = col;
}
