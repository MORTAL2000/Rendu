#include "Common.hpp"

#include "resources/ResourcesManager.hpp"
#include <iostream>

/**
 \defgroup ShaderValidator Shader Validation
 \brief Validate shaders compilation on the GPU and output IDE-compatible errors. Can be integrated to the build preprocess.
 \ingroup Tools
 */

/**  Convert a shader compilation log into a IDE-compatible error reporting format and output it to stderr.
 	\param compilationLog the compilation log to process.
 	\param filePath the path to the shader file, relative to the directory containing the IDE project.
 	\return a boolean denoting if at least one error was reported by the log.
 	\warning If filePath is not expressed relative to the directory containing the IDE project, error links (for instance "src/foo/bar.frag:18") won't be functional.
 	\ingroup ShaderValidator
 */
bool processLog(const std::string & compilationLog, const std::string & filePath){
	if(!compilationLog.empty()){
		std::stringstream str(compilationLog);
		std::string line;
		// Iterate over the lines of the log.
		while(std::getline(str, line)){
			// Parse the log and output it as a compiler readable error.
			// Find the global file ID limits.
			const std::string::size_type firstIDDigitPos = line.find_first_of("0123456789");
			const std::string::size_type lastIDDigitPos = line.find_first_not_of("0123456789", firstIDDigitPos);
			if(firstIDDigitPos == std::string::npos || lastIDDigitPos == std::string::npos){
				continue;
			}
			// Find the line number limits.
			const std::string::size_type firstLineDigitPos = line.find_first_of("0123456789", lastIDDigitPos+1);
			const std::string::size_type lastLineDigitPos = line.find_first_not_of("0123456789", firstLineDigitPos);
			if(firstLineDigitPos == std::string::npos || lastLineDigitPos == std::string::npos){
				continue;
			}
			// Generate the corresponding int.
			const std::string lineNumberRaw = line.substr(firstLineDigitPos, lastLineDigitPos - 1 - firstLineDigitPos + 1);
			const unsigned int lineId = std::stoi(lineNumberRaw);
			
			// Find the error message.
			const std::string::size_type firstMessagePos = line.find_first_not_of(" :)]", lastLineDigitPos+1);
			std::string errorMessage = "Unknown error.";
			if(firstMessagePos != std::string::npos){
				errorMessage = line.substr(firstMessagePos);
			}
			
			// The path should be relative to the root build directory.
			const std::string adjustedPath = filePath;
			
			// Output in an IDE compatible format, to display warning and errors properly.
#ifdef _WIN32
			std::cerr << adjustedPath << "(" << lineId << "): error: " << errorMessage << std::endl;
#else
			std::cerr << adjustedPath << ":" << lineId << ": error: " << errorMessage << std::endl;
#endif
		}
		// At least one issue was encountered.
		return true;
	}
	// No log, no problem.
	return false;
}

/**
 Perform shader validation: load all shaders in the resources directory, compile them on the GPU and output error logs.
 \param argc the number of input arguments.
 \param argv a pointer to the raw input arguments.
 \return a boolean denoting if at least one shader failed to compile.
 \ingroup ShaderValidator
 */
int main(int argc, char** argv) {
	
	Log::setDefaultVerbose(false);
	if(argc < 2){
		Log::Error() << "Missing resource path." << std::endl;
		return 1;
	}
	Resources::manager().addResources(std::string(argv[1]));

	// Initialize glfw, which will create and setup an OpenGL context.
	if (!glfwInit()) {
		Log::Error() << Log::OpenGL << "Could not start GLFW3" << std::endl;
		return 1;
	}
	
	glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	
	GLFWwindow* window = glfwCreateWindow(100,100,"validation", NULL, NULL);
	if (!window) {
		Log::Error() << Log::OpenGL << "Could not open window with GLFW3" << std::endl;
		glfwTerminate();
		return 1;
	}
	
	// Bind the OpenGL context and the new window.
	glfwMakeContextCurrent(window);
	
	if (gl3wInit()) {
		Log::Error() << Log::OpenGL << "Failed to initialize OpenGL" << std::endl;
		return -1;
	}
	if (!gl3wIsSupported(3, 2)) {
		Log::Error() << Log::OpenGL << "OpenGL 3.2 not supported\n" << std::endl;
		return -1;
	}
	
	// Query the renderer identifier, and the supported OpenGL version.
	const GLubyte* vendorString = glGetString(GL_VENDOR);
	const GLubyte* rendererString = glGetString(GL_RENDERER);
	const GLubyte* versionString = glGetString(GL_VERSION);
	const GLubyte* glslVersionString = glGetString(GL_SHADING_LANGUAGE_VERSION);
	Log::Info() << Log::OpenGL << "Vendor: " << vendorString << "." << std::endl;
	Log::Info() << Log::OpenGL << "Internal renderer: " << rendererString << "." << std::endl;
	Log::Info() << Log::OpenGL << "Versions: Driver: " << versionString << ", GLSL: " << glslVersionString << "." << std::endl;
	
	
	bool encounteredIssues = false;
	
	// Load all vertex shaders from disk.
	std::map<std::string, std::string> verts;
	Resources::manager().getFiles("vert", verts);
	for(auto & vert : verts){
		std::map<std::string, int> bindings;
		std::string compilationLog;
		// Load and compile the shader.
		const std::string shader = Resources::manager().getString(vert.first + ".vert");
		GLUtilities::loadShader(shader, GL_VERTEX_SHADER, bindings, compilationLog);
		// Process the log.
		const bool newIssues = processLog(compilationLog, vert.second);
		encounteredIssues = encounteredIssues || newIssues;
	}
	
	// Load all geometry shaders from disk.
	std::map<std::string, std::string> geoms;
	Resources::manager().getFiles("geom", geoms);
	for(auto & geom : geoms){
		std::map<std::string, int> bindings;
		std::string compilationLog;
		// Load and compile the shader.
		const std::string shader = Resources::manager().getString(geom.first + ".geom");
		GLUtilities::loadShader(shader, GL_GEOMETRY_SHADER, bindings, compilationLog);
		// Process the log.
		const bool newIssues = processLog(compilationLog, geom.second);
		encounteredIssues = encounteredIssues || newIssues;
	}
	
	// Load all fragment shaders from disk.
	std::map<std::string, std::string> frags;
	Resources::manager().getFiles("frag", frags);
	for(auto & frag : frags){
		std::map<std::string, int> bindings;
		std::string compilationLog;
		// Load and compile the shader.
		const std::string shader = Resources::manager().getString(frag.first + ".frag");
		GLUtilities::loadShader(shader, GL_FRAGMENT_SHADER, bindings, compilationLog);
		// Process the log.
		const bool newIssues = processLog(compilationLog, frag.second);
		encounteredIssues = encounteredIssues || newIssues;
	}
	
	// Remove the window.
	glfwDestroyWindow(window);
	// Close GL context and any other GLFW resources.
	glfwTerminate();
	// Has any of the shaders encountered a compilation issue?
	return encounteredIssues ? 1 : 0;
}


