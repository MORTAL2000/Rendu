#pragma once

#include "graphics/Framebuffer.hpp"
#include "graphics/GLUtilities.hpp"
#include "Common.hpp"

/**
 \brief Represent a cubemap rendering target, of any size, format and type, backed by an OpenGL framebuffer composed of six layers.
 \see GLSL::Vert::Object_layer, GLSL::Geom::Object_layer
 \ingroup Graphics
 */
class FramebufferCube {

public:
	
	/** Setup the framebuffer (attachments, renderbuffer, depth buffer, textures IDs,...)
	 \param side the width and height of each face of the framebuffer
	 \param descriptor contains the precise format and filtering to use
	 \param depthBuffer should the framebuffer contain a depth buffer to properly handle 3D geometry
	 */
	FramebufferCube(unsigned int side, const Descriptor & descriptor, bool depthBuffer);
	
	/**
	 Bind the framebuffer.
	 */
	void bind() const;
	
	/**
	 Set the viewport to the size of the framebuffer.
	 */
	void setViewport() const;

	/**
	 Unbind the framebuffer.
	 \note Technically bind the window backbuffer.
	 */
	void unbind() const;
	
	/**
	 Resize the framebuffer to new dimensions.
	 \param side the new width and height for each face
	 */
	void resize(unsigned int side);
	
	/** Clean internal resources.
	 */
	void clean() const;
	
	/**
	 Query the ID to the cubemap texture backing the framebuffer.
	 \return the texture ID
	 */
	GLuint textureId() const { return _idColor; }
	
	/**
	 Query the framebuffer side size.
	 \return the width/height of each face
	 */
	unsigned int side() const { return _side; }
	
	/**
	 Query the framebuffer ID.
	 \return the ID
	 */
	GLuint id() const { return _id; }
	
	/**
	 Query the framebuffer OpenGL type and format.
	 \return the typed format
	 */
	GLuint typedFormat() const { return _descriptor.typedFormat; }
	
private:
	
	unsigned int _side; ///< The size of each cubemap face sides.
	
	GLuint _id; ///< The framebuffer ID.
	GLuint _idColor; ///< The color texture ID.
	GLuint _idRenderbuffer; ///< The depth buffer ID.
	
	Descriptor _descriptor; ///< The color target descriptor.

	bool _useDepth; ///< Denotes if the framebuffer is backed by a depth buffer.
	
};
