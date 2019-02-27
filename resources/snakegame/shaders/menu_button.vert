#version 330


// Attributes
layout(location = 0) in vec3 v;///< Position.

// Uniforms.

uniform vec2 position;
uniform vec2 scale;
uniform float depth;

void main(){
	gl_Position.xy = scale * v.xy + position;
	gl_Position.zw = vec2(depth, 1.0);
	
}
