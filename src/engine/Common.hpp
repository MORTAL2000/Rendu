//
//  Common.hpp
//  GL_Template
//
//  Created by Simon Rodriguez on 08/08/2018.
//  Copyright © 2017 Simon Rodriguez. All rights reserved.
//
/**
 
 	\defgroup Engine
 	\brief Module containing the general rendering code.
 
	\defgroup Renderers
	\brief Module containing the renderer-specific objects.
 
	\defgroup Lights
	\brief Module containing classes representing light objects.
 
	\defgroup Processing
	\brief Module containing general image processing utilities.
 
	\defgroup Input
	\brief Module containing the input handling code.
 
	\defgroup Resources
	\brief Module containing all resources loading/saving functions.
 
	\defgroup Helpers
	\brief Module containing various helpers.
 
 	\defgroup Shaders
 	\brief Module containing all OpenGL GLSL shaders.
 	\details Those shaders are small programs compiled at runtime and executed by the GPU cores. They can process vertices (vertex shader), primitives (geometry shader) and compute per-pixel values (fragment shader).
 
 	\defgroup Applications
 	\brief Applications built with GL_Template.
 
 	\defgroup Tools
 	\brief Preprocess tools for shader validation, data precomputations, etc.
 
 */

#ifndef Common_hpp
#define Common_hpp

#include <gl3w/gl3w.h>
#include <GLFW/glfw3.h>

#include "helpers/Logger.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#include <string>
#include <vector>
#include <algorithm>
#include <memory>

#ifdef _WIN32

#undef near
#undef far
#define NOMINMAX
#define M_PI	3.14159265358979323846

#endif

#endif /* Common_hpp */
