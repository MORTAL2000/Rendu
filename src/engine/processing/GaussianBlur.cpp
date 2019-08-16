#include "processing/GaussianBlur.hpp"
#include "graphics/GLUtilities.hpp"


GaussianBlur::GaussianBlur(unsigned int width, unsigned int height, unsigned int depth, Layout preciseFormat) : Blur() {
	_passthroughProgram = Resources::manager().getProgram("passthrough");
	_blurProgramDown = Resources::manager().getProgram2D("blur-dual-filter-down");
	_blurProgramUp = Resources::manager().getProgram2D("blur-dual-filter-up");
	
	// Create a series of framebuffers smaller and smaller.
	_frameBuffers = std::vector<std::unique_ptr<Framebuffer>>(depth);
	
	for(size_t i = 0; i < (size_t)depth; ++i){
		_frameBuffers[i] = std::unique_ptr<Framebuffer>(new Framebuffer((unsigned int)(width/std::pow(2,i)), (unsigned int)(height/std::pow(2,i)), preciseFormat , false));
	}

	_finalTexture = _frameBuffers[0]->textureId();
	checkGLError();
}

void GaussianBlur::process(const Texture * textureId) {
	if(_frameBuffers.size() == 0){
		return;
	}
	
	// First, copy the input texture to the first framebuffer.
	_frameBuffers[0]->bind();
	_frameBuffers[0]->setViewport();
	GLUtilities::clearColor(glm::vec4(0.0f));
	_passthroughProgram->use();
	ScreenQuad::draw(textureId);
	_frameBuffers[0]->unbind();
	
	// Downscale filter.
	_blurProgramDown->use();
	for(size_t d = 1; d < _frameBuffers.size(); ++d){
		_frameBuffers[d]->bind();
		_frameBuffers[d]->setViewport();
		GLUtilities::clearColor(glm::vec4(0.0f));
		ScreenQuad::draw(_frameBuffers[d-1]->textureId());
		_frameBuffers[d]->unbind();
	}
	
	// Upscale filter.
	_blurProgramUp->use();
	for(int d = int(_frameBuffers.size())-2; d >= 0; --d){
		_frameBuffers[d]->bind();
		_frameBuffers[d]->setViewport();
		GLUtilities::clearColor(glm::vec4(0.0f));
		ScreenQuad::draw(_frameBuffers[d+1]->textureId());
		_frameBuffers[d]->unbind();
	}
	
}


void GaussianBlur::clean() const {
	for(auto & frameBuffer : _frameBuffers){
		frameBuffer->clean();
	}
	Blur::clean();
}


void GaussianBlur::resize(unsigned int width, unsigned int height){
	for(size_t i = 0; i < _frameBuffers.size(); ++i){
		const unsigned int hwidth = (unsigned int)(width/std::pow(2,i));
		const unsigned int hheight = (unsigned int)(height/std::pow(2,i));
		_frameBuffers[i]->resize(hwidth, hheight);
	}
}

