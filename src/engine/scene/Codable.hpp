#pragma once

#include "resources/ResourcesManager.hpp"
#include "Common.hpp"
#include "Config.hpp"

/** \brief Provides helpers for serialization/deserialization of basic types.
 	\ingroup Scene
 */
class Codable {
public:
	
	/** Decode a boolean from a parameters tuple, at a specified position.
	A boolean is set to true if its value is 'true', 'True', 'yes', 'Yes' or '1'.
	 \param param the parameters tuple
	 \param position index of the boolean in the values array
	 \return a boolean
	 */
	static bool decodeBool(const KeyValues & param, unsigned int position = 0);
	
	/** Decode a 3D vector from a parameters tuple, starting at a specified position.
	 A 3D vector is described as a set of 3 floats separated by spaces or commas: X,Y,Z.
	 \param param the parameters tuple
	 \param position index of the first vector component in the values array
	 \return a 3D vector
	 */
	static glm::vec3 decodeVec3(const KeyValues & param, unsigned int position = 0);
	
	/** Decode a transformation from a series of parameters tuple. For now this function look for three keywords in the whole params vector: translation, orientation, scaling.  A transformation is described as follows:
	 \verbatim
	 translation: X,Y,Z
	 orientation: axisX,axisY,axisZ angle
	 scaling: scale
	 \endverbatim
	 \param params a list of parameters tuples
	 \return the 4x4 matrix representation of the transformation
	 */
	static glm::mat4 decodeTransformation(const std::vector<KeyValues> & params);
	
	/** Decode a texture from a parameters tuple and load it. A texture is described as follows:
	 \verbatim
	 texturetype: texturename
	 \endverbatim
	 (where texturetype can be one of 'rgb', 'srgb', 'rgb32', 'rgbcube', 'srgbcube', 'rgb32cube' depending on the desired format).
	 \param param the parameters tuple
	 \return a pointer to the allocated texture infos, or null/
	 \todo We could extract the interaction with the Resources manager and only output the proper name/descriptor/type.
	 */
	static TextureInfos * decodeTexture(const KeyValues & param);
	
	/** Split a Codable-compatible text file in a list of (key,values) tuples, getting rid of extraneous spaces and punctuations.
	 \param codableFile the text content to parse
	 \return a list of (key, value) tokens
	 */
	static std::vector<KeyValues> parse(const std::string & codableFile);
	
};
