#include "graphics/GPU.hpp"
#include "graphics/Framebuffer.hpp"
#include "resources/Texture.hpp"
#include "resources/Image.hpp"
#include "system/TextUtilities.hpp"
#include "system/Window.hpp"


#include "graphics/GPUInternal.hpp"

#include <sstream>
#include <GLFW/glfw3.h>

#include <set>

const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

GPUContext _context;

void * GPU::getInternal(){
	return (void*)&_context;
}

bool GPU::setup(const std::string & appName) {

	if(volkInitialize() != VK_SUCCESS){
		Log::Error() << Log::GPU << "Could not load Vulkan" << std::endl;
		return false;
	}

	bool debugEnabled = false;
#if defined(_DEBUG) || defined(FORCE_DEBUG_VULKAN)
	// Only enable if the layers are supported.
	debugEnabled = VkUtils::checkLayersSupport(validationLayers) && VkUtils::checkExtensionsSupport({ VK_EXT_DEBUG_UTILS_EXTENSION_NAME });
#endif

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = appName.c_str();
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Rendu";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;

	// We have to tell Vulkan the extensions we need.
	const std::vector<const char*> extensions = VkUtils::getRequiredInstanceExtensions(debugEnabled);
	if(!VkUtils::checkExtensionsSupport(extensions)){
		Log::Error() << Log::GPU << "Unsupported extensions." << std::endl;
		return false;
	}
	instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	instanceInfo.ppEnabledExtensionNames = extensions.data();

	// Validation layers.
	instanceInfo.enabledLayerCount = 0;
	if(debugEnabled){
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		instanceInfo.ppEnabledLayerNames = validationLayers.data();
	}

	// Debug callbacks if supported.
	VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
	if(debugEnabled){
		debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = vkDebugCallback;
		debugInfo.pUserData = nullptr;
		instanceInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugInfo;
	}

	if(vkCreateInstance(&instanceInfo, nullptr, &_context.instance) != VK_SUCCESS){
		Log::Info() << Log::GPU << "Unable to create a Vulkan instance." << std::endl;
		return false;
	}

	volkLoadInstance(_context.instance);

	if(debugEnabled){
		VK_RET(vkCreateDebugUtilsMessengerEXT(_context.instance, &debugInfo, nullptr, &_context.debugMessenger));
		//vkDestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
	}

	// Pick a physical device.
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(_context.instance, &deviceCount, nullptr);
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(_context.instance, &deviceCount, devices.data());

	VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
	// Check which one is ok for our requirements.
	for(const auto& device : devices) {
		bool hasPortability = false;
		// We want a device with swapchain support.
		const bool supportExtensions = VkUtils::checkDeviceExtensionsSupport(device, deviceExtensions, hasPortability);
		// Ask for anisotropy and tessellation.
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(device, &features);
		const bool hasFeatures = features.samplerAnisotropy && features.tessellationShader;

		if(supportExtensions && hasFeatures){
			// Prefere a discrete GPU if possible.
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(device, &properties);
			const bool isDiscrete = properties.deviceType;

			if(selectedDevice == VK_NULL_HANDLE || isDiscrete){
				selectedDevice = device;
				_context.portability = hasPortability;
				//uniformOffset = properties.limits.minUniformBufferOffsetAlignment;
			}
		}
	}

	if(selectedDevice == VK_NULL_HANDLE){
		Log::Error() << Log::GPU << "Unable to find proper physical device." << std::endl;
		return false;
	}

	_context.physicalDevice = selectedDevice;

	// Query a few infos.
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(_context.physicalDevice, &properties);
	if(!properties.limits.timestampComputeAndGraphics){
		Log::Warning() << Log::GPU << "Timestamp queries are not supported on the selected device." << std::endl;
	}

	_context.timestep = double(properties.limits.timestampPeriod);
	_context.uniformAlignment = properties.limits.minUniformBufferOffsetAlignment;
	// minImageTransferGranularity is guaranteed to be (1,1,1) on graphics/compute queues

	// Create empty VAO for screenquad.
	//	glGenVertexArrays(1, &_vao);
	//	glBindVertexArray(_vao);
	//	glBindVertexArray(0);
	//	_state.vertexArray = 0;
	return true;
}

bool GPU::setupWindow(Window * window){
	// Create a surface.
	if(glfwCreateWindowSurface(_context.instance, window->_window, nullptr, &_context.surface) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create surface." << std::endl;
		return false;
	}
	// Query the available queues.
	uint graphicsIndex, presentIndex;
	bool found = VkUtils::getQueueFamilies(_context.physicalDevice, _context.surface, graphicsIndex, presentIndex);
	if(!found){
		Log::Error() << Log::GPU << "Unable to find compatible queue families." << std::endl;
		return false;
	}

	// Select queues.
	std::set<uint> families;
	families.insert(graphicsIndex);
	families.insert(presentIndex);

	float queuePriority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	for(int queueFamily : families) {
		VkDeviceQueueCreateInfo queueInfo = {};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = queueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;
		queueInfos.push_back(queueInfo);
	}

	// Device setup.
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
	deviceInfo.pQueueCreateInfos = queueInfos.data();
	// Features we want.
	VkPhysicalDeviceFeatures features = {};
	features.samplerAnisotropy = VK_TRUE;
	features.tessellationShader = VK_TRUE;
	deviceInfo.pEnabledFeatures = &features;
	// Extensions.
	auto extensions = deviceExtensions;
	// If portability is available, we have to enabled it.
	if(_context.portability){
		extensions.push_back("VK_KHR_portability_subset");
	}
	deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	deviceInfo.ppEnabledExtensionNames = extensions.data();

	if(vkCreateDevice(_context.physicalDevice, &deviceInfo, nullptr, &_context.device) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create logical device." << std::endl;
		return false;
	}
	_context.graphicsId = graphicsIndex;
	_context.presentId = presentIndex;
	vkGetDeviceQueue(_context.device, graphicsIndex, 0, &_context.graphicsQueue);
	vkGetDeviceQueue(_context.device, presentIndex, 0, &_context.presentQueue);

	// Create the command pool.
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = graphicsIndex;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if(vkCreateCommandPool(_context.device, &poolInfo, nullptr, &_context.commandPool) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create command pool." << std::endl;
		return false;
	}


	// Finally setup the swapchain.
	window->_swapchain.init(_context, window->_config);

	// Create command buffers.
	_context.commandBuffers.resize(window->_swapchain.count());
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = _context.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(_context.commandBuffers.size());
	if(vkAllocateCommandBuffers(_context.device, &allocInfo, _context.commandBuffers.data()) != VK_SUCCESS) {
		Log::Error() << Log::GPU  << "Unable to create command buffers." << std::endl;
		return false;
	}
	
	return true;
}

int GPU::checkError(const char * file, int line, const std::string & infos) {
//	const GLenum glErr = glGetError();
//	if(glErr != GL_NO_ERROR) {
//		const std::string filePath(file);
//		size_t pos = std::min(filePath.find_last_of('/'), filePath.find_last_of('\\'));
//		if(pos == std::string::npos) {
//			pos = 0;
//		}
//		Log::Error() << Log::GPU << "Error " << getGLErrorString(glErr) << " in " << filePath.substr(pos + 1) << " (" << line << ").";
//		if(!infos.empty()) {
//			Log::Error() << " Infos: " << infos;
//		}
//		Log::Error() << std::endl;
//		return 1;
//	}
	return 0;
}

int GPU::checkFramebufferStatus() {
//	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
//	if(status != GL_FRAMEBUFFER_COMPLETE) {
//		switch(status) {
//			case GL_FRAMEBUFFER_UNDEFINED:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_UNDEFINED" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_UNSUPPORTED:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_UNSUPPORTED" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE" << std::endl;
//				break;
//			case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
//				Log::Error() << Log::GPU << "Error GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS" << std::endl;
//				break;
//			default:
//				Log::Error() << Log::GPU << "Unknown framebuffer error." << std::endl;
//				break;
//		}
//		return 1;
//	}
	return 0;
}

GPU::Handle GPU::loadShader(const std::string & prog, ShaderType type, Bindings & bindings, std::string & finalLog) {
	// We need to detect texture slots and store them, to avoid having to register them in
	// the rest of the code (object, renderer), while not having support for 'layout(binding=n)' in OpenGL <4.2.
//	std::stringstream inputLines(prog);
//	std::vector<std::string> outputLines;
//	std::string line;
//	bool isInMultiLineComment = false;
//	while(std::getline(inputLines, line)) {
//
//		// Comment handling.
//		const std::string::size_type commentPosBegin = line.find("/*");
//		const std::string::size_type commentPosEnd	 = line.rfind("*/");
//		const std::string::size_type commentMonoPos	 = line.find("//");
//		// We suppose no multi-line comment nesting, that way we can tackle them linearly.
//		if(commentPosBegin != std::string::npos && commentPosEnd != std::string::npos) {
//			// Both token exist.
//			// Either this is "end begin", in which case we are still in a comment.
//			// Or this is "begin end", ie a single ligne comment.
//			isInMultiLineComment = commentPosBegin > commentPosEnd;
//		} else if(commentPosEnd != std::string::npos) {
//			// Only an end token.
//			isInMultiLineComment = false;
//		} else if(commentPosBegin != std::string::npos) {
//			// Only a begin token.
//			isInMultiLineComment = true;
//		}
//
//		// Find a line containing "layout...binding...uniform..."
//		const std::string::size_type layoutPos	= line.find("layout");
//		const std::string::size_type bindingPos = line.find("binding");
//		const std::string::size_type uniformPos = line.find("uniform");
//
//		const bool isNotALayoutBindingUniform		 = (layoutPos == std::string::npos || bindingPos == std::string::npos || uniformPos == std::string::npos);
//		const bool isALayoutInsideAMultiLineComment	 = isInMultiLineComment && (layoutPos > commentPosBegin || uniformPos < commentPosEnd);
//		const bool isALayoutInsideASingleLineComment = commentMonoPos != std::string::npos && layoutPos > commentMonoPos;
//		if(isNotALayoutBindingUniform || isALayoutInsideAMultiLineComment || isALayoutInsideASingleLineComment) {
//			// We don't modify the line.
//			outputLines.push_back(line);
//			continue;
//		}
//		// Extract the statement.
//		const std::string::size_type startStatement = std::min(layoutPos, uniformPos);
//		const std::string::size_type endStatement	= line.find_first_of(";{", startStatement);
//		int slot									= 0;
//		std::string name;
//		{
//			const std::string statement = TextUtilities::trim(line.substr(startStatement, endStatement - startStatement), "\t ");
//			// Extract the location and the name.
//			const std::string::size_type bindingPosSub = statement.find("binding");
//			const std::string::size_type firstSlotPos  = statement.find_first_of("0123456789", bindingPosSub);
//			const std::string::size_type lastSlotPos   = statement.find_first_not_of("0123456789", firstSlotPos) - 1;
//			const std::string::size_type startPosName  = statement.find_last_of(" \t") + 1;
//			name									   = statement.substr(startPosName);
//			slot									   = std::stoi(statement.substr(firstSlotPos, lastSlotPos - firstSlotPos + 1));
//		}
//		// Two possibles cases, sampler or buffer.
//		const std::string::size_type samplerPos = line.find("sampler", layoutPos);
//		const bool isSampler					= samplerPos != std::string::npos;
//		if(isSampler) {
//			const std::string::size_type endSamplerPos	 = line.find_first_of(' ', samplerPos) - 1;
//			const std::string::size_type startSamplerPos = line.find_last_of(' ', samplerPos) + 1;
//			const std::string samplerType				 = line.substr(startSamplerPos, endSamplerPos - startSamplerPos + 1);
//			std::string outputLine						 = "uniform " + samplerType + " ";
//			outputLine += name + ";";
//			outputLines.push_back(outputLine);
//		} else {
//			// We just need to remove the binding spec from the layout.
//			const std::string::size_type layoutContentStart = line.find_first_of("(", layoutPos) + 1;
//			const std::string::size_type layoutContentEnd	= line.find_first_of(")", layoutContentStart);
//			// Two options: either binding is the only argument.
//			const std::string::size_type splitPos = line.find_first_of(",", layoutContentStart, layoutContentEnd - layoutContentStart);
//			if(splitPos == std::string::npos) {
//				// Remove layout entirely.
//				const std::string outputLine = line.substr(0, layoutPos) + line.substr(layoutContentEnd + 1);
//				outputLines.push_back(outputLine);
//			} else {
//				// Or there are other specifiers to preserve.
//				std::string::size_type sepBefore = line.find_last_of("(,", bindingPos);
//				std::string::size_type sepAfter	 = line.find_first_of("),", bindingPos);
//				if(line[sepBefore] == '(') {
//					sepBefore += 1;
//				}
//				if(line[sepAfter] == ')') {
//					sepAfter -= 1;
//				}
//				const std::string outputLine = line.substr(0, sepBefore) + line.substr(sepAfter + 1);
//				outputLines.push_back(outputLine);
//			}
//		}
//
//		if(bindings.count(name) > 0 && bindings[name].location != slot) {
//			Log::Warning() << Log::GPU << "Inconsistent binding location between linked shaders for \"" << name << "\"." << std::endl;
//		}
//		bindings[name].location = slot;
//		bindings[name].type		= isSampler ? BindingType::TEXTURE : BindingType::UNIFORM_BUFFER;
//		Log::Verbose() << Log::GPU << "Detected binding (" << name << ", " << slot << ") => " << outputLines.back() << std::endl;
//	}
//	// Add OpenGL version.
//	std::string outputProg = "#version 400\n#line 1 0\n";
//	for(const auto & outputLine : outputLines) {
//		outputProg.append(outputLine + "\n");
//	}
//
//	// Create shader object.
//	static const std::map<ShaderType, GLenum> types = {
//		{ShaderType::VERTEX, GL_VERTEX_SHADER},
//		{ShaderType::FRAGMENT, GL_FRAGMENT_SHADER},
//		{ShaderType::GEOMETRY, GL_GEOMETRY_SHADER},
//		{ShaderType::TESSCONTROL, GL_TESS_CONTROL_SHADER},
//		{ShaderType::TESSEVAL, GL_TESS_EVALUATION_SHADER}
//	};
//
//	GLuint id = glCreateShader(types.at(type));
//	checkGPUError();
//	// Setup string as source.
//	const char * shaderProg = outputProg.c_str();
//	glShaderSource(id, 1, &shaderProg, static_cast<const GLint *>(nullptr));
//	// Compile the shader on the GPU.
//	glCompileShader(id);
//	checkGPUError();
//
//	GLint success;
//	glGetShaderiv(id, GL_COMPILE_STATUS, &success);
//	finalLog = "";
//	// If compilation failed, get information and display it.
//	if(success != GL_TRUE) {
//		// Get the log string length for allocation.
//		GLint infoLogLength;
//		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infoLogLength);
//		// Get the log string.
//		std::vector<char> infoLog(size_t(std::max(infoLogLength, int(1))));
//		glGetShaderInfoLog(id, infoLogLength, nullptr, &infoLog[0]);
//		// Indent and clean.
//		std::string infoLogString(infoLog.data(), infoLogLength);
//
//		TextUtilities::replace(infoLogString, "\n", "\n\t");
//		infoLogString.insert(0, "\t");
//		finalLog = infoLogString;
//	}
	// Return the id to the successfuly compiled shader program.
	return 0;
}

GPU::Handle GPU::createProgram(const std::string & vertexContent, const std::string & fragmentContent, const std::string & geometryContent, const std::string & tessControlContent, const std::string & tessEvalContent, Bindings & bindings, const std::string & debugInfos) {
//	GLuint vp(0), fp(0), gp(0), tcp(0), tep(0);
//	const GLuint id = glCreateProgram();
//	checkGPUError();
//
//	Log::Verbose() << Log::GPU << "Compiling " << debugInfos << "." << std::endl;
//
//	std::string compilationLog;
//	// If vertex program code is given, compile it.
//	if(!vertexContent.empty()) {
//		vp = loadShader(vertexContent, ShaderType::VERTEX, bindings, compilationLog);
//		glAttachShader(id, vp);
//		if(!compilationLog.empty()) {
//			Log::Error() << Log::GPU << "Vertex shader failed to compile:" << std::endl
//						 << compilationLog << std::endl;
//		}
//	}
//	// If fragment program code is given, compile it.
//	if(!fragmentContent.empty()) {
//		fp = loadShader(fragmentContent, ShaderType::FRAGMENT, bindings, compilationLog);
//		glAttachShader(id, fp);
//		if(!compilationLog.empty()) {
//			Log::Error() << Log::GPU << "Fragment shader failed to compile:" << std::endl
//						 << compilationLog << std::endl;
//		}
//	}
//	// If geometry program code is given, compile it.
//	if(!geometryContent.empty()) {
//		gp = loadShader(geometryContent, ShaderType::GEOMETRY, bindings, compilationLog);
//		glAttachShader(id, gp);
//		if(!compilationLog.empty()) {
//			Log::Error() << Log::GPU << "Geometry shader failed to compile:" << std::endl
//						 << compilationLog << std::endl;
//		}
//	}
//	// If tesselation control program code is given, compile it.
//	if(!tessControlContent.empty()) {
//		tcp = loadShader(tessControlContent, ShaderType::TESSCONTROL, bindings, compilationLog);
//		glAttachShader(id, tcp);
//		if(!compilationLog.empty()) {
//			Log::Error() << Log::GPU << "Tessellation control shader failed to compile:" << std::endl
//						 << compilationLog << std::endl;
//		}
//	}
//	// If tessellation evaluation program code is given, compile it.
//	if(!tessEvalContent.empty()) {
//		tep = loadShader(tessEvalContent, ShaderType::TESSEVAL, bindings, compilationLog);
//		glAttachShader(id, tep);
//		if(!compilationLog.empty()) {
//			Log::Error() << Log::GPU << "Tessellation evaluation shader failed to compile:" << std::endl
//						 << compilationLog << std::endl;
//		}
//	}
//
//	// Link everything
//	glLinkProgram(id);
//	checkGPUError();
//	//Check linking status.
//	GLint success = GL_FALSE;
//	glGetProgramiv(id, GL_LINK_STATUS, &success);
//
//	// If linking failed, query info and display it.
//	if(!success) {
//		// Get the log string length for allocation.
//		GLint infoLogLength;
//		glGetProgramiv(id, GL_INFO_LOG_LENGTH, &infoLogLength);
//		// Get the log string.
//		std::vector<char> infoLog(size_t(std::max(infoLogLength, int(1))));
//		glGetProgramInfoLog(id, infoLogLength, nullptr, &infoLog[0]);
//		// Indent and clean.
//		std::string infoLogString(infoLog.data(), infoLogLength);
//		TextUtilities::replace(infoLogString, "\n", "\n\t");
//		infoLogString.insert(0, "\t");
//		// Output.
//		Log::Error() << Log::GPU
//					 << "Failed linking program " << debugInfos << ": " << std::endl
//					 << infoLogString << std::endl;
//		return 0;
//	}
//	// We can now clean the shaders objects, by first detaching them
//	if(vp != 0) {
//		glDetachShader(id, vp);
//	}
//	if(fp != 0) {
//		glDetachShader(id, fp);
//	}
//	if(gp != 0) {
//		glDetachShader(id, gp);
//	}
//	if(tcp != 0) {
//		glDetachShader(id, tcp);
//	}
//	if(tep != 0) {
//		glDetachShader(id, tep);
//	}
//	checkGPUError();
//	//And deleting them
//	glDeleteShader(vp);
//	glDeleteShader(fp);
//	glDeleteShader(gp);
//	glDeleteShader(tcp);
//	glDeleteShader(tep);
//
//	checkGPUError();
	// Return the id to the successfuly linked GLProgram.
	return 0;
}

void GPU::bindProgram(const Program & program){
//	if(_state.program != program._id){
//		_state.program = program._id;
//		glUseProgram(program._id);
//		_metrics.programBindings += 1;
//	}
}

void GPU::bindFramebuffer(const Framebuffer & framebuffer){
//	if(_state.drawFramebuffer != framebuffer._id){
//		_state.drawFramebuffer = framebuffer._id;
//		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer._id);
	//		_metrics.framebufferBindings += 1;
//	}
}

void GPU::bindFramebuffer(const Framebuffer & framebuffer, Framebuffer::Mode mode){
	//if(mode == Framebuffer::Mode::WRITE && _state.drawFramebuffer != framebuffer._id){
	//	_state.drawFramebuffer = framebuffer._id;
	//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer._id);
	//	_metrics.framebufferBindings += 1;
	//} else if(mode == Framebuffer::Mode::READ && _state.readFramebuffer != framebuffer._id){
	//	_state.readFramebuffer = framebuffer._id;
	//	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer._id);
	//	_metrics.framebufferBindings += 1;
	//}

}

void GPU::saveFramebuffer(const Framebuffer & framebuffer, const std::string & path, bool flip, bool ignoreAlpha) {

	// Don't alter the GPU state, this is a temporary action.
	//glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer._id);

	//const std::unique_ptr<GPUTexture> & gpu = framebuffer.texture()->gpu;
	//GPU::savePixels(gpu->type, gpu->format, framebuffer.width(), framebuffer.height(), gpu->channels, path, flip, ignoreAlpha);
	
	//glBindFramebuffer(GL_READ_FRAMEBUFFER, _state.readFramebuffer);
	//_metrics.framebufferBindings += 2;
}

void GPU::bindTexture(const Texture * texture, size_t slot) {
	//auto & currId = _state.textures[slot][texture->gpu->target];
	//if(currId != texture->gpu->id){
	//	currId = texture->gpu->id;
	//	_state.activeTexture = GLenum(GL_TEXTURE0 + slot);
	//	glActiveTexture(_state.activeTexture);
	//	glBindTexture(texture->gpu->target, texture->gpu->id);
	//	_metrics.textureBindings += 1;
	//}
}

void GPU::bindTexture(const Texture & texture, size_t slot) {
//	auto & currId = _state.textures[slot][texture.gpu->target];
//	if(currId != texture.gpu->id){
//		currId = texture.gpu->id;
//		_state.activeTexture = GLenum(GL_TEXTURE0 + slot);
//		glActiveTexture(_state.activeTexture);
//		glBindTexture(texture.gpu->target, texture.gpu->id);
	//		_metrics.textureBindings += 1;
//	}
}

void GPU::bindTextures(const std::vector<const Texture *> & textures, size_t startingSlot) {
	for(size_t i = 0; i < textures.size(); ++i) {
		const Texture * infos = textures[i];
//		const int slot = startingSlot + i;
//		auto & currId = _state.textures[slot][infos->gpu->target];
//
//		if(currId != infos->gpu->id){
//			currId = infos->gpu->id;
//			_state.activeTexture = GLenum(GL_TEXTURE0 + slot);
//			glActiveTexture(_state.activeTexture);
//			glBindTexture(infos->gpu->target, infos->gpu->id);
		//			_metrics.textureBindings += 1;
//		}
	}
}

void GPU::setupTexture(Texture & texture, const Descriptor & descriptor) {

	if(texture.gpu) {
		texture.gpu->clean();
	}

	texture.gpu.reset(new GPUTexture(descriptor, texture.shape));

	const bool is3D = texture.gpu->type == VK_IMAGE_TYPE_3D;
	const bool isCube = texture.shape & TextureShape::Cube;
	const bool isArray = texture.shape & TextureShape::Array;

	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	// Create image.
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = texture.gpu->type;
	imageInfo.extent.width = static_cast<uint32_t>(texture.width);
	imageInfo.extent.height = static_cast<uint32_t>(texture.height);
	imageInfo.extent.depth = is3D ? texture.depth : 1;
	imageInfo.mipLevels = texture.levels;
	imageInfo.arrayLayers = (isCube || isArray) ? texture.depth : 1;
	imageInfo.format = texture.gpu->format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = texture.gpu->layout;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = 0;
	if(isCube){
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	} else if(texture.shape == TextureShape::Array2D){
		// Only for 2D arrays apparently.
		imageInfo.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
	}

	if (vkCreateImage(_context.device, &imageInfo, nullptr, &(texture.gpu->image)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create texture image." << std::endl;
		return;
	}

	const Layout & layout = descriptor.typedFormat();
	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	if(layout == Layout::DEPTH_COMPONENT16 || layout == Layout::DEPTH_COMPONENT24 || layout == Layout::DEPTH_COMPONENT32F){
		aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	if(layout == Layout::DEPTH24_STENCIL8 || layout == Layout::DEPTH32F_STENCIL8){
		aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	// Create view.
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = texture.gpu->image;
	viewInfo.viewType = texture.gpu->viewType;
	viewInfo.format = texture.gpu->format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = texture.levels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

	if (vkCreateImageView(_context.device, &viewInfo, nullptr, &(texture.gpu->view)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create image view." << std::endl;
		return;
	}

	// Create associated sampler.
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = texture.gpu->imgFiltering;
	samplerInfo.minFilter = texture.gpu->imgFiltering;
	samplerInfo.addressModeU = texture.gpu->wrapping;
	samplerInfo.addressModeV = texture.gpu->wrapping;
	samplerInfo.addressModeW = texture.gpu->wrapping;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = texture.gpu->mipFiltering;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = float(texture.levels);
	if (vkCreateSampler(_context.device, &samplerInfo, nullptr, &(texture.gpu->sampler)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to create a sampler." << std::endl;
	}

	// Allocate.
	GPU::allocateTexture(texture);

}

void GPU::allocateTexture(const Texture & texture) {
	if(!texture.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU texture." << std::endl;
		return;
	}

	// Allocate memory for image.
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(_context.device, texture.gpu->image, &requirements);
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = VkUtils::findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _context.physicalDevice);
	if (vkAllocateMemory(_context.device, &allocInfo, nullptr, &(texture.gpu->data)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Unable to allocate texture memory." << std::endl;
		return;
	}
	vkBindImageMemory(_context.device, texture.gpu->image, texture.gpu->data, 0);

}

void GPU::uploadTexture(const Texture & texture) {
	if(!texture.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU texture." << std::endl;
		return;
	}
	if(texture.images.empty()) {
		Log::Warning() << Log::GPU << "No images to upload." << std::endl;
		return;
	}

	// Sanity check the texture destination format.
	const unsigned int destChannels = texture.gpu->channels;
	if(destChannels != texture.images[0].components) {
		Log::Error() << Log::GPU << "Not enough values in source data for texture upload." << std::endl;
		return;
	}

	// Compute total texture size on the CPU.
	size_t totalSize = 0;
	for(const auto & img: texture.images) {
		// \todo Handle conversion to other formats.
		const size_t imgSize = img.pixels.size() * sizeof(float);
		totalSize += imgSize;
	}

	// Transfer the complete CPU image data to a staging buffer.
	BufferBase transferBuffer(totalSize, BufferType::CPUTOGPU, DataUse::STATIC);
	GPU::setupBuffer(transferBuffer);
	void* dataImg = nullptr;
	vkMapMemory(_context.device, transferBuffer.gpu->data, 0, totalSize, 0, &dataImg);

	size_t currentOffset = 0;
	for(const auto & img: texture.images) {
		const size_t imgSize = img.pixels.size() * sizeof(float);
		memcpy((uchar*)dataImg + currentOffset, img.pixels.data(), imgSize);
		currentOffset += imgSize;
	}
	vkUnmapMemory(_context.device, transferBuffer.gpu->data);

	VkUtils::transitionImageLayout(_context, texture.gpu->image, texture.gpu->format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.levels, texture.depth);

	VkCommandBuffer commandBuffer = VkUtils::startOneTimeCommandBuffer(_context);

	// Copy operation for each mip level that is available on the CPU.
	size_t currentImg = 0;
	currentOffset = 0;

	for(size_t mid = 0; mid < texture.levels; ++mid) {
		// How deep is the image for 3D textures.
		const size_t depth = texture.shape == TextureShape::D3 ? (texture.depth / (1 << mid)) : 1;
		// How many images in the mip level (for arrays and cubes)
		const size_t layers = texture.shape == TextureShape::D3 ? 1 : texture.depth;

		// First image of the mip level (they all have the same size.
		const Image & image = texture.images[currentImg];
		const size_t imgSize = image.pixels.size() * sizeof(float);

		// Perform copy for this mip level.
		VkBufferImageCopy region = {};
		region.bufferOffset = currentOffset;
		region.bufferRowLength = 0; // Tightly packed.
		region.bufferImageHeight = 0; // Tightly packed.
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = mid;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = layers;
		// Offset *in the subregion*
		region.imageOffset = {0, 0, 0};
		region.imageExtent = { image.width, image.height, (uint32_t)depth};

		vkCmdCopyBufferToImage(commandBuffer, transferBuffer.gpu->buffer, texture.gpu->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		currentImg += depth;
		currentOffset += depth * imgSize;
		// We might have more levels allocated on the GPU than we had available on the CPU.
		// Stop, these will be generated automatically.
		if(currentImg >= texture.images.size()){
			break;
		}

	}
	VkUtils::endOneTimeCommandBuffer(commandBuffer, _context);

	transferBuffer.clean();

}

void GPU::downloadTexture(Texture & texture) {
	downloadTexture(texture, -1);
}

void GPU::downloadTexture(Texture & texture, int level) {
	if(!texture.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU texture." << std::endl;
		return;
	}
	if(texture.shape != TextureShape::D2 && texture.shape != TextureShape::Cube) {
		Log::Error() << Log::GPU << "Unsupported download format." << std::endl;
		return;
	}
	if(!texture.images.empty()) {
		Log::Verbose() << Log::GPU << "Texture already contain CPU data, will be erased." << std::endl;
	}
	texture.images.resize(texture.depth * texture.levels);
//
//	const GLenum target			= texture.gpu->target;
//	const GLenum type			= GL_FLOAT;
//	const GLenum format			= texture.gpu->format;
//	const unsigned int channels = texture.gpu->channels;
//
//	// We enforce float type, we can use 4 alignment.
//	glPixelStorei(GL_PACK_ALIGNMENT, 4);
//	_metrics.stateChanges += 1;
//	glBindTexture(target, texture.gpu->id);
//	_metrics.textureBindings += 1;
//
//	// For each mip level.
//	for(size_t mid = 0; mid < texture.levels; ++mid) {
//		if(level >= 0 && int(mid) != level) {
//			continue;
//		}
//		const GLsizei w = GLsizei(std::max<uint>(1, texture.width / (1 << mid)));
//		const GLsizei h = GLsizei(std::max<uint>(1, texture.height / (1 << mid)));
//		const GLint mip = GLint(mid);
//
//		if(texture.shape == TextureShape::D2) {
//			texture.images[mid] = Image(w, h, channels);
//			Image & image		= texture.images[mid];
//			glGetTexImage(GL_TEXTURE_2D, mip, format, type, &image.pixels[0]);
//			_metrics.downloads += 1;
//		} else if(texture.shape == TextureShape::Cube) {
//			for(size_t lid = 0; lid < texture.depth; ++lid) {
//				const size_t id	   = mid * texture.levels + lid;
//				texture.images[id] = Image(w, h, channels);
//				Image & image	   = texture.images[id];
//				glGetTexImage(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + lid), mip, format, type, &image.pixels[0]);
//				_metrics.downloads += 1;
//			}
//		}
//	}
//	GPU::restoreTexture(texture.shape);
}

void GPU::generateMipMaps(const Texture & texture) {
	if(!texture.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU texture." << std::endl;
		return;
	}
	// Do we support blitting?
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(_context.physicalDevice, texture.gpu->format, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		Log::Error() << Log::GPU << "Bliting not supported for this format." << std::endl;
		return;
	}

	const bool isCube = texture.shape & TextureShape::Cube;
	const bool isArray = texture.shape & TextureShape::Array;
	const size_t layers = (isCube || isArray) ? texture.depth : 1;
	size_t width = texture.width;
	size_t height = texture.height;
	size_t depth = texture.shape == TextureShape::D3 ? texture.depth : 1;

	// Prepare barrier that we will reuse at each level.
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = texture.gpu->image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = layers;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;

	// Blit the texture to each mip level.
	VkCommandBuffer commandBuffer = VkUtils::startOneTimeCommandBuffer(_context);

	// For now, don't bother with existing mip data (potentially uploaded from the CPU).
	for (size_t mid = 1; mid < texture.levels; mid++) {

		// Transition level i-1 to transfer layout.
		barrier.subresourceRange.baseMipLevel = mid - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,  0, nullptr, 1, &barrier);
		// Then, blit to level i.
		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { (int32_t)width, (int32_t)height, (int32_t)depth };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = mid - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = layers;
		blit.dstOffsets[0] = { 0, 0, 0 };

		// Divide all dimensions by 2 if possible.
		width  = (width  > 1 ? (width/2)  : 1);
		height = (height > 1 ? (height/2) : 1);
		depth  = (depth  > 1 ? (depth/2)  : 1);

		blit.dstOffsets[1] = { (int32_t)width, (int32_t)height, (int32_t)depth };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = mid;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = layers;
		// Blit using linear filtering for smoother downscaling.
		vkCmdBlitImage(commandBuffer, texture.gpu->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.gpu->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
		// Force sync and move previous layer to shader readable format.
		// \todo Could be done for all levels at once at the end ? but it has a different old layout.
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}
	// Transition the last level.
	barrier.subresourceRange.baseMipLevel = int(texture.levels)-1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	// Submit the commands.
	VkUtils::endOneTimeCommandBuffer(commandBuffer, _context);
}

void GPU::bindBuffer(const BufferBase & buffer, size_t slot) {
//	glBindBuffer(GL_UNIFORM_BUFFER, buffer.gpu->id);
//	glBindBufferBase(GL_UNIFORM_BUFFER, GLuint(slot), buffer.gpu->id);
//	glBindBuffer(GL_UNIFORM_BUFFER, 0);
//	_metrics.bufferBindings += 2;
//	_metrics.uniforms += 1;
}

void GPU::setupBuffer(BufferBase & buffer) {
	if(buffer.gpu) {
		buffer.gpu->clean();
	}
	// Create.
	buffer.gpu.reset(new GPUBuffer(buffer.type, buffer.usage));

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = buffer.sizeMax;
	bufferInfo.usage = buffer.gpu->type;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(vkCreateBuffer(_context.device, &bufferInfo, nullptr, &(buffer.gpu->buffer)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Failed to create buffer." << std::endl;
		return;
	}

	// Allocate.
	GPU::allocateBuffer(buffer);
}

void GPU::allocateBuffer(const BufferBase & buffer) {
	if(!buffer.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU buffer." << std::endl;
		return;
	}

	// Allocate memory for buffer.
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(_context.device, buffer.gpu->buffer, &memRequirements);
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = VkUtils::findMemoryType(memRequirements.memoryTypeBits, buffer.gpu->options, _context.physicalDevice);
	if(vkAllocateMemory(_context.device, &allocInfo, nullptr, &(buffer.gpu->data)) != VK_SUCCESS) {
		Log::Error() << Log::GPU << "Failed to allocate buffer." << std::endl;
		return;
	}
	// Bind buffer to memory.
	vkBindBufferMemory(_context.device, buffer.gpu->buffer, buffer.gpu->data, 0);
//	_metrics.bufferBindings += 2;
}

void GPU::uploadBuffer(const BufferBase & buffer, size_t size, uchar * data, size_t offset) {
	if(!buffer.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU buffer." << std::endl;
		return;
	}
	if(size == 0) {
		Log::Warning() << Log::GPU << "No data to upload." << std::endl;
		return;
	}
	if(offset + size > buffer.sizeMax) {
		Log::Warning() << Log::GPU << "Not enough allocated space to upload." << std::endl;
		return;
	}

	// If the buffer is visible from the CPU side, we don't need an intermediate staging buffer.
	if((buffer.gpu->options & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (buffer.gpu->options & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){
		void* dstData = nullptr;
		// Only map the region we need.
		vkMapMemory(_context.device, buffer.gpu->data, offset, size, 0, &dstData);
		memcpy((uchar*)dstData, data, size);
		vkUnmapMemory(_context.device, buffer.gpu->data);
		return;
	}

	// Otherwise, create a transfer buffer.
	BufferBase transferBuffer(size, BufferType::CPUTOGPU, DataUse::STATIC);
	transferBuffer.upload(size, data, 0);
	// Copy operation.
	VkCommandBuffer commandBuffer = VkUtils::startOneTimeCommandBuffer(_context);
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = offset;
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, transferBuffer.gpu->buffer, buffer.gpu->buffer, 1, &copyRegion);
	VkUtils::endOneTimeCommandBuffer(commandBuffer, _context);
	transferBuffer.clean();
}

void GPU::downloadBuffer(const BufferBase & buffer, size_t size, uchar * data, size_t offset) {
	if(!buffer.gpu) {
		Log::Error() << Log::GPU << "Uninitialized GPU buffer." << std::endl;
		return;
	}
	if(offset + size > buffer.sizeMax) {
		Log::Warning() << Log::GPU << "Not enough available data to download." << std::endl;
		return;
	}

	// If the buffer is visible from the CPU side, we don't need an intermediate staging buffer.
	if((buffer.gpu->options & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (buffer.gpu->options & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){
		void* srcData = nullptr;
		// Only map the region we need.
		vkMapMemory(_context.device, buffer.gpu->data, offset, size, 0, &srcData);
		memcpy(data, (uchar*)srcData, size);
		vkUnmapMemory(_context.device, buffer.gpu->data);
		return;
	}

	// Otherwise, create a transfer buffer.
	BufferBase transferBuffer(size, BufferType::GPUTOCPU, DataUse::STATIC);
	// Copy operation.
	VkCommandBuffer commandBuffer = VkUtils::startOneTimeCommandBuffer(_context);
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = offset;
	copyRegion.dstOffset = 0;
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, buffer.gpu->buffer, transferBuffer.gpu->buffer, 1, &copyRegion);
	VkUtils::endOneTimeCommandBuffer(commandBuffer, _context);
	transferBuffer.download(size, data, 0);
	transferBuffer.clean();
}

void GPU::setupMesh(Mesh & mesh) {
	if(mesh.gpu) {
		mesh.gpu->clean();
	}
	mesh.gpu.reset(new GPUMesh());


	// Compute full allocation size.
	size_t totalSize = 0;
	totalSize += 3 * mesh.positions.size();
	totalSize += 3 * mesh.normals.size();
	totalSize += 2 * mesh.texcoords.size();
	totalSize += 3 * mesh.tangents.size();
	totalSize += 3 * mesh.binormals.size();
	totalSize += 3 * mesh.colors.size();
	totalSize *= sizeof(float);

	// Create a staging buffer to host the geometry data (to avoid creating a staging buffer for each sub-upload).
	BufferBase stageVertexBuffer(totalSize, BufferType::CPUTOGPU, DataUse::STATIC);
	GPU::setupBuffer(stageVertexBuffer);

	// Fill in subregions.
	size_t offset = 0;
	if(!mesh.positions.empty()) {
		const size_t size = sizeof(float) * 3 * mesh.positions.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.positions.data()), offset);
		offset += size;
	}
	if(!mesh.normals.empty()) {
		const size_t size = sizeof(GLfloat) * 3 * mesh.normals.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.normals.data()), offset);
		offset += size;
	}
	if(!mesh.texcoords.empty()) {
		const size_t size = sizeof(GLfloat) * 2 * mesh.texcoords.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.texcoords.data()), offset);
		offset += size;
	}
	if(!mesh.tangents.empty()) {
		const size_t size = sizeof(GLfloat) * 3 * mesh.tangents.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.tangents.data()), offset);
		offset += size;
	}
	if(!mesh.binormals.empty()) {
		const size_t size = sizeof(GLfloat) * 3 * mesh.binormals.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.binormals.data()), offset);
		offset += size;
	}
	if(!mesh.colors.empty()) {
		const size_t size = sizeof(GLfloat) * 3 * mesh.colors.size();
		GPU::uploadBuffer(stageVertexBuffer, size, reinterpret_cast<unsigned char *>(mesh.colors.data()), offset);
	}

	// Copy from the staging buffer.
	BufferBase vertexBuffer(totalSize, BufferType::VERTEX, DataUse::STATIC);
	GPU::setupBuffer(vertexBuffer);
	VkCommandBuffer commandBuffer = VkUtils::startOneTimeCommandBuffer(_context);
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = totalSize;
	vkCmdCopyBuffer(commandBuffer, stageVertexBuffer.gpu->buffer, vertexBuffer.gpu->buffer, 1, &copyRegion);
	VkUtils::endOneTimeCommandBuffer(commandBuffer, _context);

	// We load the indices data directly (staging will be handled internally).
	const size_t inSize = sizeof(unsigned int) * mesh.indices.size();
	BufferBase indexBuffer(inSize, BufferType::INDEX, DataUse::STATIC);
	GPU::setupBuffer(indexBuffer);
	GPU::uploadBuffer(indexBuffer, inSize, reinterpret_cast<unsigned char *>(mesh.indices.data()));

	mesh.gpu->count		   = GLsizei(mesh.indices.size());
	mesh.gpu->indexBuffer  = std::move(indexBuffer.gpu);
	mesh.gpu->vertexBuffer = std::move(vertexBuffer.gpu);
}

void GPU::drawMesh(const Mesh & mesh) {
//	if(_state.vertexArray != mesh.gpu->id){
//		_state.vertexArray = mesh.gpu->id;
//		glBindVertexArray(mesh.gpu->id);
//		_metrics.vertexBindings += 1;
//	}
//	glDrawElements(GL_TRIANGLES, mesh.gpu->count, GL_UNSIGNED_INT, static_cast<void *>(nullptr));
//	_metrics.drawCalls += 1;
}

void GPU::drawTesselatedMesh(const Mesh & mesh, uint patchSize){
//	glPatchParameteri(GL_PATCH_VERTICES, GLint(patchSize));
//	if(_state.vertexArray != mesh.gpu->id){
//		_state.vertexArray = mesh.gpu->id;
//		glBindVertexArray(mesh.gpu->id);
//		_metrics.vertexBindings += 1;
//	}
//	glDrawElements(GL_PATCHES, mesh.gpu->count, GL_UNSIGNED_INT, static_cast<void *>(nullptr));
//	_metrics.drawCalls += 1;

}

void GPU::drawQuad(){
//	if(_state.vertexArray != _vao){
//		_state.vertexArray = _vao;
//		glBindVertexArray(_vao);
//		_metrics.vertexBindings += 1;
//	}
//	glDrawArrays(GL_TRIANGLES, 0, 3);
//	_metrics.quadCalls += 1;
}

void GPU::sync(){
//	glFlush();
//	glFinish();
}

void GPU::nextFrame(){
	// Save and reset stats.
//	_metricsPrevious = _metrics;
//	_metrics = Metrics();
}

void GPU::deviceInfos(std::string & vendor, std::string & renderer, std::string & version, std::string & shaderVersion) {
	vendor = renderer = version = shaderVersion = "";

	const std::unordered_map<uint32_t, std::string> vendors = {
		{ 0x1002, "AMD" }, { 0x10DE, "NVIDIA" }, { 0x8086, "INTEL" }, { 0x13B5, "ARM" }
	};

	if(_context.physicalDevice != VK_NULL_HANDLE){
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(_context.physicalDevice, &properties);

		const uint32_t vendorId = properties.vendorID;
		vendor = vendors.count(vendorId) ? vendors.at(vendorId) : std::to_string(vendorId);

		renderer = std::string(properties.deviceName);
		version = std::to_string(properties.driverVersion);

		const uint32_t vMaj = VK_VERSION_MAJOR(properties.apiVersion);
		const uint32_t vMin = VK_VERSION_MINOR(properties.apiVersion);
		const uint32_t vPat = VK_VERSION_PATCH(properties.apiVersion);
		shaderVersion = std::to_string(vMaj) + "." + std::to_string(vMin) + "." + std::to_string(vPat);
	}
}

std::vector<std::string> GPU::supportedExtensions() {
	std::vector<std::string> names;
	names.emplace_back("-- Instance ------");
	// Get available extensions.
	uint32_t instanceExtsCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtsCount, nullptr);
	std::vector<VkExtensionProperties> instanceExts(instanceExtsCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtsCount, instanceExts.data());
	for(const auto& ext : instanceExts){
		names.emplace_back(ext.extensionName);
	}
	// Layers too.
	names.emplace_back("-- Layers --------");
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
	for(const auto& layer : availableLayers){
		names.emplace_back(layer.layerName);
	}
	// Get available device extensions.
	if(_context.physicalDevice != VK_NULL_HANDLE){
		names.emplace_back("-- Device --------");
		uint32_t deviceExtsCount;
		vkEnumerateDeviceExtensionProperties(_context.physicalDevice, nullptr, &deviceExtsCount, nullptr);
		std::vector<VkExtensionProperties> deviceExts(deviceExtsCount);
		vkEnumerateDeviceExtensionProperties(_context.physicalDevice, nullptr, &deviceExtsCount, deviceExts.data());
		for(const auto& ext : deviceExts){
			names.emplace_back(ext.extensionName);
		}
	}
	return names;
}

void GPU::setViewport(int x, int y, int w, int h) {
//	if(_state.viewport[0] != x || _state.viewport[1] != y || _state.viewport[2] != w || _state.viewport[3] != h){
//		_state.viewport[0] = x;
//		_state.viewport[1] = y;
//		_state.viewport[2] = w;
//		_state.viewport[3] = h;
//		glViewport(GLsizei(x), GLsizei(y), GLsizei(w), GLsizei(h));
//		_metrics.stateChanges += 1;
//	}
}

void GPU::clearColor(const glm::vec4 & color) {
//	if(_state.colorClearValue != color){
//		_state.colorClearValue = color;
//		glClearColor(color[0], color[1], color[2], color[3]);
//		_metrics.stateChanges += 1;
//	}
//	glClear(GL_COLOR_BUFFER_BIT);
//	_metrics.clearAndBlits += 1;
}

void GPU::clearDepth(float depth) {
//	if(_state.depthClearValue != depth){
//		_state.depthClearValue = depth;
//		glClearDepth(depth);
//		_metrics.stateChanges += 1;
//	}
//	glClear(GL_DEPTH_BUFFER_BIT);
//	_metrics.clearAndBlits += 1;
}

void GPU::clearStencil(uchar stencil) {
	// The stencil mask applies to clearing.
	// Disable it temporarily.
//	if(!_state.stencilWriteMask){
//		glStencilMask(0xFF);
//		_metrics.stateChanges += 1;
//	}
//
//	if(_state.stencilClearValue != stencil){
//		_state.stencilClearValue = stencil;
//		glClearStencil(GLint(stencil));
//		_metrics.stateChanges += 1;
//	}
//	glClear(GL_STENCIL_BUFFER_BIT);
//	_metrics.clearAndBlits += 1;
//
//	if(!_state.stencilWriteMask){
//		glStencilMask(0x00);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::clearColorAndDepth(const glm::vec4 & color, float depth) {
//	if(_state.colorClearValue != color){
//		_state.colorClearValue = color;
//		glClearColor(color[0], color[1], color[2], color[3]);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.depthClearValue != depth){
//		_state.depthClearValue = depth;
//		glClearDepth(depth);
//		_metrics.stateChanges += 1;
//	}
//	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//	_metrics.clearAndBlits += 1;
}

void GPU::clearColorDepthStencil(const glm::vec4 & color, float depth, uchar stencil) {
	// The stencil mask applies to clearing.
	// Disable it temporarily.
//	if(!_state.stencilWriteMask){
//		glStencilMask(0xFF);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.colorClearValue != color){
//		_state.colorClearValue = color;
//		glClearColor(color[0], color[1], color[2], color[3]);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.depthClearValue != depth){
//		_state.depthClearValue = depth;
//		glClearDepth(depth);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.stencilClearValue != stencil){
//		_state.stencilClearValue = stencil;
//		glClearStencil(GLint(stencil));
//		_metrics.stateChanges += 1;
//	}
//
//	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
//	_metrics.clearAndBlits += 1;
//
//	if(!_state.stencilWriteMask){
//		glStencilMask(0x00);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setDepthState(bool test) {
//	if(_state.depthTest != test){
//		_state.depthTest = test;
//		(test ? glEnable : glDisable)(GL_DEPTH_TEST);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setDepthState(bool test, TestFunction equation, bool write) {
//	if(_state.depthTest != test){
//		_state.depthTest = test;
//		(test ? glEnable : glDisable)(GL_DEPTH_TEST);
//		_metrics.stateChanges += 1;
//	}
//
//	static const std::map<TestFunction, GLenum> eqs = {
//		{TestFunction::NEVER, GL_NEVER},
//		{TestFunction::LESS, GL_LESS},
//		{TestFunction::LEQUAL, GL_LEQUAL},
//		{TestFunction::EQUAL, GL_EQUAL},
//		{TestFunction::GREATER, GL_GREATER},
//		{TestFunction::GEQUAL, GL_GEQUAL},
//		{TestFunction::NOTEQUAL, GL_NOTEQUAL},
//		{TestFunction::ALWAYS, GL_ALWAYS}};
//
//	if(_state.depthFunc != equation){
//		_state.depthFunc = equation;
//		glDepthFunc(eqs.at(equation));
//		_metrics.stateChanges += 1;
//	}
//
//	if(_state.depthWriteMask != write){
//		_state.depthWriteMask = write;
//		glDepthMask(write ? GL_TRUE : GL_FALSE);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setStencilState(bool test, bool write){
//	if(_state.stencilTest != test){
//		_state.stencilTest = test;
//		(test ? glEnable : glDisable)(GL_STENCIL_TEST);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.stencilWriteMask != write){
//		_state.stencilWriteMask = write;
//		glStencilMask(write ? 0xFF : 0x00);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setStencilState(bool test, TestFunction function, StencilOp fail, StencilOp pass, StencilOp depthFail, uchar value){

//	if(_state.stencilTest != test){
//		_state.stencilTest = test;
//		(test ? glEnable : glDisable)(GL_STENCIL_TEST);
//		_metrics.stateChanges += 1;
//	}
//
//	static const std::map<TestFunction, GLenum> funs = {
//		{TestFunction::NEVER, GL_NEVER},
//		{TestFunction::LESS, GL_LESS},
//		{TestFunction::LEQUAL, GL_LEQUAL},
//		{TestFunction::EQUAL, GL_EQUAL},
//		{TestFunction::GREATER, GL_GREATER},
//		{TestFunction::GEQUAL, GL_GEQUAL},
//		{TestFunction::NOTEQUAL, GL_NOTEQUAL},
//		{TestFunction::ALWAYS, GL_ALWAYS}};
//
//	static const std::map<StencilOp, GLenum> ops = {
//		{ StencilOp::KEEP, GL_KEEP },
//		{ StencilOp::ZERO, GL_ZERO },
//		{ StencilOp::REPLACE, GL_REPLACE },
//		{ StencilOp::INCR, GL_INCR },
//		{ StencilOp::INCRWRAP, GL_INCR_WRAP },
//		{ StencilOp::DECR, GL_DECR },
//		{ StencilOp::DECRWRAP, GL_DECR_WRAP },
//		{ StencilOp::INVERT, GL_INVERT }};
//
//	if(_state.stencilFunc != function){
//		_state.stencilFunc = function;
//		glStencilFunc(funs.at(function), GLint(value), 0xFF);
//		_metrics.stateChanges += 1;
//	}
//	if(!_state.stencilWriteMask){
//		_state.stencilWriteMask = true;
//		glStencilMask(0xFF);
//		_metrics.stateChanges += 1;
//	}
//	if(_state.stencilFail != fail || _state.stencilPass != depthFail || _state.stencilDepthPass != pass){
//		_state.stencilFail = fail;
//		_state.stencilPass = depthFail;
//		_state.stencilDepthPass = pass;
//		glStencilOp(ops.at(fail), ops.at(depthFail), ops.at(pass));
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setBlendState(bool test) {
//	if(_state.blend != test){
//		_state.blend = test;
//		(test ? glEnable : glDisable)(GL_BLEND);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setBlendState(bool test, BlendEquation equation, BlendFunction src, BlendFunction dst) {
//
//	if(_state.blend != test){
//		_state.blend = test;
//		(test ? glEnable : glDisable)(GL_BLEND);
//		_metrics.stateChanges += 1;
//	}
//
//	static const std::map<BlendEquation, GLenum> eqs = {
//		{BlendEquation::ADD, GL_FUNC_ADD},
//		{BlendEquation::SUBTRACT, GL_FUNC_SUBTRACT},
//		{BlendEquation::REVERSE_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT},
//		{BlendEquation::MIN, GL_MIN},
//		{BlendEquation::MAX, GL_MAX}};
//	static const std::map<BlendFunction, GLenum> funcs = {
//		{BlendFunction::ONE, GL_ONE},
//		{BlendFunction::ZERO, GL_ZERO},
//		{BlendFunction::SRC_COLOR, GL_SRC_COLOR},
//		{BlendFunction::ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR},
//		{BlendFunction::SRC_ALPHA, GL_SRC_ALPHA},
//		{BlendFunction::ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
//		{BlendFunction::DST_COLOR, GL_DST_COLOR},
//		{BlendFunction::ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR},
//		{BlendFunction::DST_ALPHA, GL_DST_ALPHA},
//		{BlendFunction::ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA}};
//
//	if(_state.blendEquationRGB != equation){
//		_state.blendEquationRGB = _state.blendEquationAlpha = equation;
//		glBlendEquation(eqs.at(equation));
//		_metrics.stateChanges += 1;
//	}
//
//	if(_state.blendSrcRGB != src || _state.blendDstRGB != dst){
//		_state.blendSrcRGB = _state.blendSrcAlpha = src;
//		_state.blendDstRGB = _state.blendDstAlpha = dst;
//		glBlendFunc(funcs.at(src), funcs.at(dst));
//		_metrics.stateChanges += 1;
//	}

}

void GPU::setCullState(bool cull) {
//	if(_state.cullFace != cull){
//		_state.cullFace = cull;
//		(cull ? glEnable : glDisable)(GL_CULL_FACE);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setCullState(bool cull, Faces culledFaces) {
//	if(_state.cullFace != cull){
//		_state.cullFace = cull;
//		(cull ? glEnable : glDisable)(GL_CULL_FACE);
//		_metrics.stateChanges += 1;
//	}
//
//	static const std::map<Faces, GLenum> faces = {
//	 {Faces::FRONT, GL_FRONT},
//	 {Faces::BACK, GL_BACK},
//	 {Faces::ALL, GL_FRONT_AND_BACK}};
//
//	if(_state.cullFaceMode != culledFaces){
//		_state.cullFaceMode = culledFaces;
//		glCullFace(faces.at(culledFaces));
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setPolygonState(PolygonMode mode) {
//
//	static const std::map<PolygonMode, GLenum> modes = {
//		{PolygonMode::FILL, GL_FILL},
//		{PolygonMode::LINE, GL_LINE},
//		{PolygonMode::POINT, GL_POINT}};
//
//	if(_state.polygonMode != mode){
//		_state.polygonMode = mode;
//		glPolygonMode(GL_FRONT_AND_BACK, modes.at(mode));
//		_metrics.stateChanges += 1;
//	}
}

void GPU::setColorState(bool writeRed, bool writeGreen, bool writeBlue, bool writeAlpha){
//	if(_state.colorWriteMask.r != writeRed || _state.colorWriteMask.g != writeGreen || _state.colorWriteMask.b != writeBlue || _state.colorWriteMask.a != writeAlpha){
//		_state.colorWriteMask.r = writeRed;
//		_state.colorWriteMask.g = writeGreen;
//		_state.colorWriteMask.b = writeBlue;
//		_state.colorWriteMask.a = writeAlpha;
//		glColorMask(writeRed ? GL_TRUE : GL_FALSE, writeGreen ? GL_TRUE : GL_FALSE, writeBlue ? GL_TRUE : GL_FALSE, writeAlpha ? GL_TRUE : GL_FALSE);
//		_metrics.stateChanges += 1;
//	}

}
void GPU::setSRGBState(bool convert){
//	if(_state.framebufferSRGB != convert){
//		_state.framebufferSRGB = convert;
//		(convert ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
//		_metrics.stateChanges += 1;
//	}
}

void GPU::blitDepth(const Framebuffer & src, const Framebuffer & dst) {
//	src.bind(Framebuffer::Mode::READ);
//	dst.bind(Framebuffer::Mode::WRITE);
//	glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, dst.width(), dst.height(), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
//	_metrics.clearAndBlits += 1;
}

void GPU::blit(const Framebuffer & src, const Framebuffer & dst, Filter filter) {
//	src.bind(Framebuffer::Mode::READ);
//	dst.bind(Framebuffer::Mode::WRITE);
//	const GLenum filterGL = filter == Filter::LINEAR ? GL_LINEAR : GL_NEAREST;
//	glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, filterGL);
//	_metrics.clearAndBlits += 1;
}

void GPU::blit(const Framebuffer & src, const Framebuffer & dst, size_t lSrc, size_t lDst, Filter filter) {
	//GPU::blit(src, dst, lSrc, lDst, 0, 0, filter);
}

void GPU::blit(const Framebuffer & src, const Framebuffer & dst, size_t lSrc, size_t lDst, size_t mipSrc, size_t mipDst, Filter filter) {
//	src.bind(lSrc, mipSrc, Framebuffer::Mode::READ);
//	dst.bind(lDst, mipDst, Framebuffer::Mode::WRITE);
//	const GLenum filterGL = filter == Filter::LINEAR ? GL_LINEAR : GL_NEAREST;
//	glBlitFramebuffer(0, 0, src.width() / (1 << mipSrc), src.height() / (1 << mipSrc), 0, 0, dst.width() / (1 << mipDst), dst.height() / (1 << mipDst), GL_COLOR_BUFFER_BIT, filterGL);
//	_metrics.clearAndBlits += 1;
}

void GPU::blit(const Texture & src, Texture & dst, Filter filter) {
	// Prepare the destination.
//	dst.width  = src.width;
//	dst.height = src.height;
//	dst.depth  = src.depth;
//	dst.levels = 1;
//	dst.shape  = src.shape;
//	if(src.levels != 1) {
//		Log::Warning() << Log::GPU << "Only the first mipmap level will be used." << std::endl;
//	}
//	if(!src.images.empty()) {
//		Log::Warning() << Log::GPU << "CPU data won't be copied." << std::endl;
//	}
//	GPU::setupTexture(dst, src.gpu->descriptor());
//
//	// Create two framebuffers.
//	GLuint srcFb, dstFb;
//	glGenFramebuffers(1, &srcFb);
//	glGenFramebuffers(1, &dstFb);
//	// Because these two are temporary and will be unbound at the end of the call
//	// we do not update the cached GPU state.
//	glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFb);
//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFb);
//	_metrics.framebufferBindings += 2;
//
//	const GLenum filterGL = filter == Filter::LINEAR ? GL_LINEAR : GL_NEAREST;
//
//	if(src.shape == TextureShape::Cube) {
//		for(size_t i = 0; i < 6; ++i) {
//			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i), src.gpu->id, 0);
//			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i), dst.gpu->id, 0);
//			GPU::checkFramebufferStatus();
//			glBlitFramebuffer(0, 0, src.width, src.height, 0, 0, dst.width, dst.height, GL_COLOR_BUFFER_BIT, filterGL);
//			_metrics.clearAndBlits += 1;
//		}
//	} else {
//		if(src.shape == TextureShape::D1) {
//			glFramebufferTexture1D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src.gpu->target, src.gpu->id, 0);
//			glFramebufferTexture1D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst.gpu->target, dst.gpu->id, 0);
//
//		} else if(src.shape == TextureShape::D2) {
//			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src.gpu->target, src.gpu->id, 0);
//			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst.gpu->target, dst.gpu->id, 0);
//
//		} else {
//			Log::Error() << Log::GPU << "Unsupported texture shape for blitting." << std::endl;
//			return;
//		}
//		GPU::checkFramebufferStatus();
//		glBlitFramebuffer(0, 0, src.width, src.height, 0, 0, dst.width, dst.height, GL_COLOR_BUFFER_BIT, filterGL);
//		_metrics.clearAndBlits += 1;
//	}
//	// Restore the proper framebuffers from the cache.
//	glBindFramebuffer(GL_READ_FRAMEBUFFER, _state.readFramebuffer);
//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _state.drawFramebuffer);
//	_metrics.framebufferBindings += 2;
//	glDeleteFramebuffers(1, &srcFb);
//	glDeleteFramebuffers(1, &dstFb);
}

void GPU::blit(const Texture & src, Framebuffer & dst, Filter filter) {
	// Prepare the destination.
//	if(src.levels != 1) {
//		Log::Warning() << Log::GPU << "Only the first mipmap level will be used." << std::endl;
//	}
//	if(src.shape != dst.shape()){
//		Log::Error() << Log::GPU << "The texture and framebuffer don't have the same shape." << std::endl;
//		return;
//	}
//
//	// Create one framebuffer.
//	GLuint srcFb;
//	glGenFramebuffers(1, &srcFb);
//	// Because it's temporary and will be unbound at the end of the call
//	// we do not update the cached GPU state.
//	glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFb);
//	_metrics.framebufferBindings += 1;
//	const GLenum filterGL = filter == Filter::LINEAR ? GL_LINEAR : GL_NEAREST;
//
//	if(src.shape == TextureShape::Cube) {
//		for(size_t i = 0; i < 6; ++i) {
//			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i), src.gpu->id, 0);
//			GPU::checkFramebufferStatus();
//			dst.bind(i, 0, Framebuffer::Mode::WRITE);
//			glBlitFramebuffer(0, 0, src.width, src.height, 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, filterGL);
//			_metrics.clearAndBlits += 1;
//		}
//	} else {
//		if(src.shape == TextureShape::D1) {
//			glFramebufferTexture1D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src.gpu->target, src.gpu->id, 0);
//
//		} else if(src.shape == TextureShape::D2) {
//			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src.gpu->target, src.gpu->id, 0);
//
//		} else {
//			Log::Error() << Log::GPU << "Unsupported texture shape for blitting." << std::endl;
//			return;
//		}
//		GPU::checkFramebufferStatus();
//		dst.bind(0, 0, Framebuffer::Mode::WRITE);
//		glBlitFramebuffer(0, 0, src.width, src.height, 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, filterGL);
//		_metrics.clearAndBlits += 1;
//	}
//	// Restore the proper framebuffer from the cache.
//	glBindFramebuffer(GL_READ_FRAMEBUFFER, _state.readFramebuffer);
//	_metrics.framebufferBindings += 1;
//	glDeleteFramebuffers(1, &srcFb);
}

//void GPU::savePixels(GLenum type, GLenum format, unsigned int width, unsigned int height, unsigned int components, const std::string & path, bool flip, bool ignoreAlpha) {

//	GPU::sync();
//
//	const bool hdr = type == GL_FLOAT;
//
//	Log::Info() << Log::GPU << "Saving framebuffer to file " << path << (hdr ? ".exr" : ".png") << "... " << std::flush;
//	int ret;
//	Image image(width, height, components);
//
//	glPixelStorei(GL_PACK_ALIGNMENT, 1);
//	_metrics.stateChanges += 1;
//	const size_t fullSize = image.width * image.height * image.components;
//	if(hdr) {
//		// Get back values.
//		glReadPixels(0, 0, GLsizei(image.width), GLsizei(image.height), format, type, &image.pixels[0]);
//		_metrics.downloads += 1;
//		// Save data.
//		ret = image.save(path + ".exr", flip, ignoreAlpha);
//
//	} else {
//		// Get back values.
//		GLubyte * data = new GLubyte[fullSize];
//		glReadPixels(0, 0, GLsizei(image.width), GLsizei(image.height), format, type, &data[0]);
//		_metrics.downloads += 1;
//		// Convert to image float format.
//		for(size_t pid = 0; pid < fullSize; ++pid) {
//			image.pixels[pid] = float(data[pid]) / 255.0f;
//		}
//		// Save data.
//		ret = image.save(path + ".png", flip, ignoreAlpha);
//		delete[] data;
//	}
//	glPixelStorei(GL_PACK_ALIGNMENT, 4);
//	_metrics.stateChanges += 1;
//
//	if(ret != 0) {
//		Log::Error() << "Error." << std::endl;
//	} else {
//		Log::Info() << "Done." << std::endl;
//	}
//}

void GPU::getState(GPUState& state) {
	
	// Boolean flags.
//	state.blend = glIsEnabled(GL_BLEND);
//	state.cullFace = glIsEnabled(GL_CULL_FACE);
//	state.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
//	state.depthTest = glIsEnabled(GL_DEPTH_TEST);
//	state.framebufferSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
//	state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
//	state.polygonOffsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);
//	state.polygonOffsetPoint = glIsEnabled(GL_POLYGON_OFFSET_POINT);
//	state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
//	state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
//
//	// Blend state.
//	static const std::map<GLenum, BlendEquation> blendEqs = {
//		{GL_FUNC_ADD, BlendEquation::ADD},
//		{GL_FUNC_SUBTRACT, BlendEquation::SUBTRACT},
//		{GL_FUNC_REVERSE_SUBTRACT, BlendEquation::REVERSE_SUBTRACT},
//		{GL_MIN, BlendEquation::MIN},
//		{GL_MAX, BlendEquation::MAX}};
//	GLint ber, bea;
//	glGetIntegerv(GL_BLEND_EQUATION_RGB, &ber);
//	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &bea);
//	state.blendEquationRGB = blendEqs.at(ber);
//	state.blendEquationAlpha = blendEqs.at(bea);
//
//	static const std::map<GLenum, BlendFunction> funcs = {
//		{GL_ONE, BlendFunction::ONE},
//		{GL_ZERO, BlendFunction::ZERO},
//		{GL_SRC_COLOR, BlendFunction::SRC_COLOR},
//		{GL_ONE_MINUS_SRC_COLOR, BlendFunction::ONE_MINUS_SRC_COLOR},
//		{GL_SRC_ALPHA, BlendFunction::SRC_ALPHA},
//		{GL_ONE_MINUS_SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA},
//		{GL_DST_COLOR, BlendFunction::DST_COLOR},
//		{GL_ONE_MINUS_DST_COLOR, BlendFunction::ONE_MINUS_DST_COLOR},
//		{GL_DST_ALPHA, BlendFunction::DST_ALPHA},
//		{GL_ONE_MINUS_DST_ALPHA, BlendFunction::ONE_MINUS_DST_ALPHA}};
//	GLint bsr, bsa, bdr, bda;
//	glGetIntegerv(GL_BLEND_SRC_RGB, &bsr);
//	glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsa);
//	glGetIntegerv(GL_BLEND_DST_RGB, &bdr);
//	glGetIntegerv(GL_BLEND_DST_ALPHA, &bda);
//	state.blendSrcRGB = funcs.at(bsr);
//	state.blendSrcAlpha = funcs.at(bsa);
//	state.blendDstRGB = funcs.at(bdr);
//	state.blendDstAlpha = funcs.at(bda);
//	glGetFloatv(GL_BLEND_COLOR, &state.blendColor[0]);
//
//	// Color state.
//	glGetFloatv(GL_COLOR_CLEAR_VALUE, &state.colorClearValue[0]);
//	GLboolean cwm[4];
//	glGetBooleanv(GL_COLOR_WRITEMASK, &cwm[0]);
//	state.colorWriteMask = glm::bvec4(cwm[0], cwm[1], cwm[2], cwm[3]);
//
//	// Geometry state.
//	static const std::map<GLenum, Faces> faces = {
//		{GL_FRONT, Faces::FRONT},
//		{GL_BACK, Faces::BACK},
//		{GL_FRONT_AND_BACK, Faces::ALL}};
//	GLint cfm;
//	glGetIntegerv(GL_CULL_FACE_MODE, &cfm);
//	state.cullFaceMode = faces.at(cfm);
//	glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &state.polygonOffsetFactor);
//	glGetFloatv(GL_POLYGON_OFFSET_UNITS, &state.polygonOffsetUnits);
//
//	// Depth state.
//	static const std::map<GLenum, TestFunction> testFuncs = {
//		{GL_NEVER, 		TestFunction::NEVER},
//		{GL_LESS, 		TestFunction::LESS},
//		{GL_LEQUAL, 	TestFunction::LEQUAL},
//		{GL_EQUAL, 		TestFunction::EQUAL},
//		{GL_GREATER, 	TestFunction::GREATER},
//		{GL_GEQUAL, 	TestFunction::GEQUAL},
//		{GL_NOTEQUAL, 	TestFunction::NOTEQUAL},
//		{GL_ALWAYS, 	TestFunction::ALWAYS}};
//	GLint dfc;
//	glGetIntegerv(GL_DEPTH_FUNC, &dfc);
//	state.depthFunc = testFuncs.at(dfc);
//	glGetFloatv(GL_DEPTH_CLEAR_VALUE, &state.depthClearValue);
//	glGetFloatv(GL_DEPTH_RANGE, &state.depthRange[0]);
//	GLboolean dwm;
//	glGetBooleanv(GL_DEPTH_WRITEMASK, &dwm);
//	state.depthWriteMask = dwm;
//
//	// Stencil state
//	static const std::map<GLenum, StencilOp> ops = {
//		{ GL_KEEP, StencilOp::KEEP },
//		{ GL_ZERO, StencilOp::ZERO },
//		{ GL_REPLACE, StencilOp::REPLACE },
//		{ GL_INCR, StencilOp::INCR },
//		{ GL_INCR_WRAP, StencilOp::INCRWRAP },
//		{ GL_DECR, StencilOp::DECR },
//		{ GL_DECR_WRAP, StencilOp::DECRWRAP },
//		{ GL_INVERT, StencilOp::INVERT }};
//	GLint sfc, sof, sos, sod;
//	glGetIntegerv(GL_STENCIL_FUNC, &sfc);
//	glGetIntegerv(GL_STENCIL_FAIL, &sof);
//	glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &sos);
//	glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &sod);
//	state.stencilFunc = testFuncs.at(sfc);
//	state.stencilFail = ops.at(sof);
//	state.stencilPass = ops.at(sos);
//	state.stencilDepthPass = ops.at(sod);
//	GLint swm, scv, srv;
//	glGetIntegerv(GL_STENCIL_WRITEMASK, &swm);
//	glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &scv);
//	glGetIntegerv(GL_STENCIL_REF, &srv);
//	state.stencilWriteMask = (swm != 0);
//	state.stencilValue = uchar(srv);
//	state.stencilClearValue = uchar(scv);
//
//	// Viewport and scissor state.
//	glGetFloatv(GL_VIEWPORT, &state.viewport[0]);
//	glGetFloatv(GL_SCISSOR_BOX, &state.scissorBox[0]);
//
//	// Binding state.
//	GLint fbr, fbd, pgb, ats, vab;
//	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbr);
//	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbd);
//	glGetIntegerv(GL_CURRENT_PROGRAM, &pgb);
//	glGetIntegerv(GL_ACTIVE_TEXTURE, &ats);
//	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vab);
//
//	state.readFramebuffer = fbr;
//	state.drawFramebuffer = fbd;
//	state.program = pgb;
//	state.activeTexture = GLenum(ats);
//	state.vertexArray = vab;
//
//	static const std::vector<GLenum> bindings = {
//		GL_TEXTURE_BINDING_1D, GL_TEXTURE_BINDING_2D,
//		GL_TEXTURE_BINDING_3D, GL_TEXTURE_BINDING_CUBE_MAP,
//		GL_TEXTURE_BINDING_1D_ARRAY, GL_TEXTURE_BINDING_2D_ARRAY,
//		GL_TEXTURE_BINDING_CUBE_MAP_ARRAY,
//	};
//	static const std::vector<GLenum> shapes = {
//		GL_TEXTURE_1D, GL_TEXTURE_2D,
//		GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
//		GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY,
//		GL_TEXTURE_CUBE_MAP_ARRAY,
//	};
//	for(size_t tid = 0; tid < state.textures.size(); ++tid){
//		glActiveTexture(GLenum(GL_TEXTURE0 + tid));
//		for(size_t bid = 0; bid < bindings.size(); ++bid){
//			GLint texId = 0;
//			glGetIntegerv(bindings[bid], &texId);
//			state.textures[tid][shapes[bid]] = GLuint(texId);
//		}
//	}
//	glActiveTexture(state.activeTexture);
}


const GPU::Metrics & GPU::getMetrics(){
	return _metricsPrevious;
}


void GPU::clean(GPUTexture & tex){
	vkDestroyImageView(_context.device, tex.view, nullptr);
	vkDestroySampler(_context.device, tex.sampler, nullptr);
	vkDestroyImage(_context.device, tex.image, nullptr);
	vkFreeMemory(_context.device, tex.data, nullptr);
}

void GPU::clean(Framebuffer & framebuffer){

}

void GPU::clean(GPUMesh & mesh){

}

void GPU::clean(GPUBuffer & buffer){
	vkDestroyBuffer(_context.device, buffer.buffer, nullptr);
	vkFreeMemory(_context.device, buffer.data, nullptr);
}

GPUState GPU::_state;
GPU::Metrics GPU::_metrics;
GPU::Metrics GPU::_metricsPrevious;
//GPU::Handle GPU::_vao = 0;
