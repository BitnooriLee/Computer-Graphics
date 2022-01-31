#include "ParticleSystem.h"
#include <labhelper.h>

#include <GL/glew.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#include <labhelper.h>
#include <imgui.h>
#include <imgui_impl_sdl_gl3.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;


//vec3 worldUp(0.0f, 1.0f, 0.0f);


mat4 fighterModel = translate(15.0f * vec3 (0.0f, 1.0f, 0.0f)); // use value 

void ParticleSystem::spawn(Particle particle) {

	if (particles.size() < max_size) particles.push_back(particle);
	// Spawn prticles 
}


void ParticleSystem::kill(int i) {

	{ particles.at(i) = particles.back();
	particles.pop_back();
	}
	// kill prticles 
}

void ParticleSystem::process_particles(float dt) {

	for (unsigned i = 0; i < particles.size(); ++i) {
		// Kill dead particles!

		if (particles.at(i).lifetime > particles.at(i).life_length) kill(i);
	}

	for (unsigned i = 0; i < particles.size(); ++i) {
		// Update alive particles!

		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		const float u = labhelper::uniform_randf(0.95f, 1.f);
		const float vel = labhelper::uniform_randf(10.f, 50.f);
		//vec3 RandVelocity = glm::mat3(vel) * vec3(u, sqrt(1.f - u * u) * cosf(theta), sqrt(1.f - u * u) * sinf(theta));

		//particles.at(i).velocity += 0.01f * mat3(fighterModel) * RandVelocity;
		//particles.at(i).velocity += 0.01f * mat3(fighterModel);
		particles.at(i).pos += (dt * particles.at(i).velocity);
		particles.at(i).lifetime += dt;
	}
}


