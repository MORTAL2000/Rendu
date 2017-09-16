#include <stdio.h>
#include <iostream>
#include <vector>
// glm additional header to generate transformation matrices directly.
#include <glm/gtc/matrix_transform.hpp>
#include "Scene.h"


Scene::~Scene(){}

Scene::Scene(){
}

void Scene::init(){
	
	// Create directional light.
	directionalLights.emplace_back(glm::vec3(0.0f), 1.2f*glm::vec3(1.0f,1.0f, 0.92f), glm::ortho(-0.75f,0.75f,-0.75f,0.75f,1.0f,6.0f));
	
	// Create point lights.
	const float lI = 6.0; // Light intensity.
	std::vector<glm::vec3> colors = { glm::vec3(lI,0.0,0.0), glm::vec3(0.0,lI,0.0), glm::vec3(0.0,0.0,lI), glm::vec3(lI,lI,0.0)};
	for(size_t i = 0; i < 4; ++i){
		glm::vec3 position = glm::vec3(-1.0f+2.0f*(i%2),-0.1f,-1.0f+2.0f*(i/2));
		pointLights.emplace_back(position, colors[i], 0.7f);
	}
	
	//Position fixed objects.
	const glm::mat4 dragonModel = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-0.1,-0.05,-0.25)),glm::vec3(0.5f));
	const glm::mat4 planeModel = glm::scale(glm::translate(glm::mat4(1.0f),glm::vec3(0.0f,-0.35f,-0.5f)), glm::vec3(2.0f));
	
	// Objects creation.
	Object suzanne(Object::Type::Regular, "suzanne", { {"suzanne_texture_color", true }, {"suzanne_texture_normal", false}, {"suzanne_texture_ao_specular_reflection", false} });
	Object dragon(Object::Type::Regular, "dragon", { { "dragon_texture_color", true }, { "dragon_texture_normal", false }, { "dragon_texture_ao_specular_reflection", false } });
	Object plane(Object::Type::Parallax, "plane", { { "plane_texture_color", true }, { "plane_texture_normal", false }, { "plane_texture_depthmap", false } }, {}, false);
	
	dragon.update(dragonModel);
	plane.update(planeModel);
	
	objects.push_back(suzanne);
	objects.push_back(dragon);
	objects.push_back(plane);
	
	// Background creation.
	background = Object(Object::Type::Skybox, "skybox", {}, {{"corsica_beach_cube", true }});
	
}

void Scene::update(double timer, double elapsedTime){
	// Update lights.
	directionalLights[0].update(glm::vec3(-2.0f, 1.5f + sin(0.5*timer),0.0f));
	
	for(size_t i = 0; i <pointLights.size(); ++i){
		auto& pointLight = pointLights[i];
		glm::vec4 newPosition = glm::rotate(glm::mat4(1.0f), (float)elapsedTime, glm::vec3(0.0f, 1.0f, 0.0f))*glm::vec4(pointLight.local(), 1.0f);
		pointLight.update(glm::vec3(newPosition));
	}
	
	// Update objects.
	const glm::mat4 suzanneModel = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.2,0.0,0.0)),float(timer),glm::vec3(0.0f,1.0f,0.0f)),glm::vec3(0.25f));
	objects[0].update(suzanneModel);
}

/// Clean function
void Scene::clean() const {
	for(auto & object : objects){
		object.clean();
	}
	background.clean();
	for(auto& dirLight : directionalLights){
		dirLight.clean();
	}
}
