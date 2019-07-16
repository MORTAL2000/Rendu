#include "PathTracer.hpp"
#include "helpers/System.hpp"
#include "helpers/Random.hpp"
#include <chrono>

PathTracer::PathTracer(const std::shared_ptr<Scene> & scene){
	// Add all scene objects to the raycaster.
	 for(const auto & obj : scene->objects){
		 _raycaster.addMesh(obj.mesh()->geometry, obj.model());
	 }
	_raycaster.updateHierarchy();
	_scene = scene;
}

void PathTracer::render(const Camera & camera, size_t samples, size_t depth, Image & render){
	
	// Safety checks.
	if(!_scene){
		Log::Error() << "[PathTracer] No scene available." << std::endl;
		return;
	}
	if(render.components != 3){
		Log::Warning() << "[PathTracer] Expected a RGB image." << std::endl;
	}
	const size_t samplesOld = samples;
	samples = std::pow(2, std::round(std::log2(float(samplesOld))));
	if(samplesOld != samples){
		Log::Warning() << "[PathTracer] Non power-of-2 samples count. Using " << samples << " instead." << std::endl;
	}
	
	// Compute incremental pixel shifts.
	glm::vec3 corner, dx, dy;
	camera.pixelShifts(corner, dx, dy);
	
	// Prepare the stratified grid.
	// We know that we have 2^k samples.
	const int k = int(std::floor(std::log2(samples)));
	// If even, just use k samples on each side.
	glm::ivec2 stratesCount(std::pow(2, k/2));
	if(k % 2 == 1){
		//  Else dispatch the extraneous factor of 2 on the horizontal axis.
		stratesCount[0] = std::pow(2, (k+1)/2);
		stratesCount[1] = std::pow(2, (k-1)/2);
	}
	const glm::vec2 stratesSize = 1.0f / glm::vec2(stratesCount);
	
	// Start chrono.
	auto start = std::chrono::steady_clock::now();
	
	// Parallelize on each row of the image.
	System::forParallel(0, render.height, [&](size_t y){
		for(size_t x = 0; x < render.width; ++x){
			for(size_t sid = 0; sid < samples; ++sid){
				// Find the grid location.
				const int sidy = sid / stratesCount.x;
				const int sidx = sid % stratesCount.x;
				
				// Draw random shift in [0.0,1.0f) for jittering.
				const float jx = Random::Float();
				const float jy = Random::Float();
				// Compute position in the stratification grid.
				const glm::vec2 gridPos = glm::vec2(sidx + jx, sidy + jy);
				// Position in screen space.
				const glm::vec2 screenPos = gridPos * stratesSize + glm::vec2(x, y);
				
				// Derive a position on the image plane from the pixel.
				const glm::vec2 ndcPos = screenPos / glm::vec2(render.width, render.height);
				// Place the point on the near plane in clip space.
				const glm::vec3 worldPos = corner + ndcPos.x * dx + ndcPos.y * dy;
				
				glm::vec3 rayPos = camera.position();
				glm::vec3 rayDir = glm::normalize(worldPos - camera.position());
				
				glm::vec3 sampleColor(0.0f);
				glm::vec3 attenColor(1.0f);
				
				for(size_t did = 0; did < depth; ++did){
					// Query closest intersection.
					const Raycaster::RayHit hit = _raycaster.intersects(rayPos, rayDir);
					
					// If no hit, background.
					if(!hit.hit){
						const Scene::Background mode = _scene->backgroundMode;
						
						// If direct background hit, produce the correct color.
						if(did == 0){
							if (mode == Scene::Background::IMAGE){
								const Image & image = _scene->background->textures()[0]->images[0];
								sampleColor = image.rgbl(ndcPos.x, ndcPos.y);
							} else if (mode == Scene::Background::SKYBOX){
								const auto & images = _scene->background->textures()[0]->images;
								sampleColor = sampleCubemap(images, glm::normalize(rayDir));
							} else {
								sampleColor = _scene->backgroundColor;
							}
							break;
						}
						
						// Else, we only care about environment maps, for indirect illumination.
						if (mode == Scene::Background::SKYBOX) {
							const auto & images = _scene->background->textures()[0]->images;
							sampleColor += attenColor * sampleCubemap(images, glm::normalize(rayDir));
						}
						break;
					}
					
					glm::vec3 illumination(0.0f);
					
					// Fetch geometry infos...
					const Mesh & mesh = _scene->objects[hit.meshId].mesh()->geometry;
					const glm::vec3 p = rayPos + hit.dist * rayDir;
					const glm::vec3 n = Raycaster::interpolateNormal(hit, mesh);
					const glm::vec2 uv = Raycaster::interpolateUV(hit, mesh);
					
					// Compute lighting.
					// Check light visibility and direct lighting.
					for(const auto light : _scene->lights){
						glm::vec3 direction;
						float attenuation;
						if(light->visible(p, _raycaster, direction, attenuation)){
							const float diffuse = glm::max(glm::dot(n, direction), 0.0f);
							illumination += attenuation * diffuse * light->intensity();
						}
					}
					
					// Fetch base color from texture.
					const Image & image = _scene->objects[hit.meshId].textures()[0]->images[0];
					const glm::vec3 baseColor = glm::pow(image.rgbl(uv.x, uv.y), glm::vec3(2.2f));
					
					// Bounce decay.
					attenColor *= baseColor;
					sampleColor += attenColor * illumination;
					
					// Update position and ray direction.
					if(did < depth-1){
						rayPos = p;
						// For the direction, we want to sample the hemisphere, weighted by the cosine weight to better use our samples.
						// We use the trick described by Peter Shirley in 'Raytracing in One Week-End':
						// Uniformly sample a sphere tangent to the surface, add this to the normal.
						rayDir = glm::normalize(n + Random::sampleSphere());
					}
					
				}
				// Modulate and store.
				render.rgb(x,y) += glm::min(sampleColor, 4.0f);
			}
		}
	});
	
	// Normalize and gamma correction.
	System::forParallel(0, render.height, [&render, &samples](size_t y){
		for(size_t x = 0; x < render.width; ++x){
			const glm::vec3 color = render.rgb(x,y) / float(samples);
			render.rgb(x,y) = glm::pow(color, glm::vec3(1.0f/2.2f));
		}
	});
	
	// Display duration.
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	Log::Info() << "[PathTracer] Rendering took " << duration.count() << " ms at " << render.width << "x" << render.height << "." << std::endl;
}

glm::vec3 PathTracer::sampleCubemap(const std::vector<Image> & images, const glm::vec3 & dir){
	// Images are stored in the following order:
	// px, nx, py, ny, pz, nz
	const glm::vec3 abs = glm::abs(dir);
	int side = 0;
	float x = 0.0f, y = 0.0f;
	float denom = 1.0f;
	if(abs.x >= abs.y && abs.x >= abs.z){
		denom = abs.x;
		y = dir.y;
		// X faces.
		if(dir.x >= 0.0f){
			side = 0;
			x = -dir.z;
		} else {
			side = 1;
			x = dir.z;
		}
		
	} else if(abs.y >= abs.x && abs.y >= abs.z){
		denom = abs.y;
		x = dir.x;
		// Y faces.
		if(dir.y >= 0.0f){
			side = 2;
			y = -dir.z;
		} else {
			side = 3;
			y = dir.z;
		}
	} else if(abs.z >= abs.x && abs.z >= abs.y){
		denom = abs.z;
		y = dir.y;
		// Z faces.
		if(dir.z >= 0.0f){
			side = 4;
			x = dir.x;
		} else {
			side = 5;
			x = -dir.x;
		}
	}
	x = 0.5f * ( x / denom) + 0.5f;
	y = 0.5f * (-y / denom) + 0.5f;
	// Ensure seamless borders between faces by never sampling closer than one pixel to the edge.
	const float eps = 1.0f / float(std::min(images[side].width, images[side].height));
	x = glm::clamp(x, 0.0f + eps, 1.0f - eps);
	y = glm::clamp(y, 0.0f + eps, 1.0f - eps);
	return images[side].rgbl(x, y);
}