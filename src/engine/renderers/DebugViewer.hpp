#pragma once
#include "graphics/Program.hpp"

class Framebuffer;
class Texture;

/**
 \brief Provide helper GUI to display the content of texture and framebuffer attachments.
 This can be useful to validate the content rendered to a specific texture zhen debugging.
 \ingroup Renderers
 */
class DebugViewer {

public:

	/** Constructor.
	\param silent if true, don't register or display anything.
	 */
	explicit DebugViewer(bool silent);

	/** Register a texture for debug.
	\param tex the texture to monitor
	\param name the display name of the texture
	*/
	void track(const Texture * tex, const std::string & name);

	/** Register a framebuffer for debug. All attachment textures will be visible.
	\param buffer the framebuffer to monitor
	\param name the display name of the framebuffer
	*/
	void track(const Framebuffer * buffer, const std::string & name);

	/** Display interface and monitored data. */
	void interface();

	/** Destructor */
	~DebugViewer() = default;

	/** Copy constructor.*/
	DebugViewer(const DebugViewer &) = delete;

	/** Copy assignment.
	 \return a reference to the object assigned to
	 */
	DebugViewer & operator=(const DebugViewer &) = delete;

	/** Move constructor.*/
	DebugViewer(DebugViewer &&) = delete;

	/** Move assignment.
	 \return a reference to the object assigned to
	 */
	DebugViewer & operator=(DebugViewer &&) = delete;

private:

	/** Texture display information */
	struct Infos {
		const Texture * tex = nullptr; ///< The texture to display.
		std::string name; ///< Texture name.
		std::unique_ptr<Framebuffer> display; ///< Framebuffer used for visualization.
		std::string displayName; ///< Texture name with extra information about the layout,...
		glm::vec2 range = glm::vec2(0.0f, 1.0f); ///< Range of values to display normalized.
		glm::bvec4 channels = glm::bvec4(true, true, true, false); ///< Channels that should be displayed.
		int mip	= 0; ///< Mipmap level to display.
		int layer = 0; ///< Layer to display for arrays and 3D textures.
		bool gamma = false; ///< Should gamma correction be applied.
		bool visible = false; ///< Is the texture window visible.
	};

	/** Framebuffer display information */
	struct FramebufferInfos {
		std::string name; ///< The framebuffer name.
		std::vector<Infos> attachments; ///< Color and depth attachment infos.
	};

	/** Populate texture information based on an input texture.
	\param name the display name of the texture
	\param tex the texture to monitor
	\param infos the information that should be populated
	*/
	void registerTexture(const std::string & name, const Texture * tex, Infos & infos);

	/** Display a texture with some helper GUI.
	\param tex the texture information to display
	*/
	void displayTexture(Infos & tex);

	/** Update the visualization associated to a texture/
	\param tex the texture to update the display of
	*/
	void updateDisplay(const Infos & tex);

	std::vector<Infos> _textures; ///< The registered textures.
	std::vector<FramebufferInfos> _framebuffers; ///< The registered framebuffers.

	const Program * _texDisplay; ///< Texture display shader.
	bool _silent; ///< Don't register or display anything.
	uint _textureId = 0;
	uint _bufferId	= 0;
};