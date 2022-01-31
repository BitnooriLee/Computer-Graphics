

#ifdef _WIN32
extern "C" _declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001;
#endif

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

#include <stb_image.h>
#include <Model.h>
#include "hdr.h"
#include "fbo.h"

#include "ParticleSystem.h"

using std::min;
using std::max;


GLuint vertexArrayObject;

///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////
SDL_Window* g_window = nullptr;
float currentTime = 0.0f;
float previousTime = 0.0f;
float deltaTime = 0.0f;
bool showUI = false;
int windowWidth, windowHeight;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;

///////////////////////////////////////////////////////////////////////////////
// Shader programs
///////////////////////////////////////////////////////////////////////////////
GLuint shaderProgram;       // Shader for rendering the final image
GLuint simpleShaderProgram; // Shader used to draw the shadow map
GLuint backgroundProgram;
GLuint particleProgram;
GLuint positionBuffer;
GLuint texture;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
float environment_multiplier = 1.5f;
GLuint environmentMap, irradianceMap, reflectionMap;
const std::string envmap_base_name = "001";

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
vec3 lightPosition;
float lightRotation = 0.f;
bool lightManualOnly = false;
vec3 point_light_color = vec3(1.f, 1.f, 1.f);
bool useSpotLight = false;
float innerSpotlightAngle = 17.5f;
float outerSpotlightAngle = 22.5f;
float point_light_intensity_multiplier = 10000.0f;

std::vector<glm::vec4> data(100000);
std::vector<glm::vec4> data1(100000);

///////////////////////////////////////////////////////////////////////////////
// Shadow map
///////////////////////////////////////////////////////////////////////////////
enum ClampMode
{
	Edge = 1,
	Border = 2
};

FboInfo shadowMapFB;
int shadowMapResolution = 2048;
int shadowMapClampMode = ClampMode::Edge;
bool shadowMapClampBorderShadowed = false;
bool usePolygonOffset = true;
bool useSoftFalloff = false;
bool useHardwarePCF = false;
float polygonOffset_factor = 1.032f; //q1
float polygonOffset_units = 1.0f;  //q2



///////////////////////////////////////////////////////////////////////////////
// Camera parameters.
///////////////////////////////////////////////////////////////////////////////
vec3 cameraPosition(-70.0f, 50.0f, 70.0f);
vec3 cameraDirection = normalize(vec3(0.0f) - cameraPosition);
float cameraSpeed = 10.f;

vec3 worldUp(0.0f, 1.0f, 0.0f);
vec3 particleStart(20.0f, 3.0f, 0.0f);


mat4 T(1.0f), R(1.0f);
//mat4 T2(1.0f), R2(1.0f);
mat4 S2(1.0f);

///////////////////////////////////////////////////////////////////////////////
// Models
///////////////////////////////////////////////////////////////////////////////
labhelper::Model* fighterModel = nullptr;
labhelper::Model* landingpadModel = nullptr;
labhelper::Model* sphereModel = nullptr;

mat4 roomModelMatrix;
mat4 landingPadModelMatrix;
mat4 fighterModelMatrix;

ParticleSystem particle_system(100000);



void loadShaders(bool is_reload)
{
	GLuint shader = labhelper::loadShaderProgram("../project/simple.vert", "../project/simple.frag",
	                                             is_reload);
	if(shader != 0)
		simpleShaderProgram = shader;
	shader = labhelper::loadShaderProgram("../project/background.vert", "../project/background.frag",
	                                      is_reload);
	if(shader != 0)
		backgroundProgram = shader;
	shader = labhelper::loadShaderProgram("../project/shading.vert", "../project/shading.frag", is_reload);
	if(shader != 0)
		shaderProgram = shader;
	shader = labhelper::loadShaderProgram("../project/particle.vert", "../project/particle.frag", is_reload);
	if (shader != 0)
		particleProgram = shader;
}



void initGL()
{
	///////////////////////////////////////////////////////////////////////
	//		Load Shaders
	///////////////////////////////////////////////////////////////////////
	backgroundProgram = labhelper::loadShaderProgram("../project/background.vert",
	                                                 "../project/background.frag");
	shaderProgram = labhelper::loadShaderProgram("../project/shading.vert", "../project/shading.frag");
	simpleShaderProgram = labhelper::loadShaderProgram("../project/simple.vert", "../project/simple.frag");
	particleProgram = labhelper::loadShaderProgram("../project/particle.vert", "../project/particle.frag");

	///////////////////////////////////////////////////////////////////////
	// Load models and set up model matrices
	///////////////////////////////////////////////////////////////////////
	fighterModel = labhelper::loadModelFromOBJ("../scenes/NewShip.obj");
	landingpadModel = labhelper::loadModelFromOBJ("../scenes/landingpad.obj");
	sphereModel = labhelper::loadModelFromOBJ("../scenes/sphere.obj");

	roomModelMatrix = mat4(1.0f);
	fighterModelMatrix = translate(15.0f * worldUp);
	T = translate(15.0f * worldUp);
	landingPadModelMatrix = mat4(1.0f);

	///////////////////////////////////////////////////////////////////////
	// Load environment map
	///////////////////////////////////////////////////////////////////////
	const int roughnesses = 8;
	std::vector<std::string> filenames;
	for(int i = 0; i < roughnesses; i++)
		filenames.push_back("../scenes/envmaps/" + envmap_base_name + "_dl_" + std::to_string(i) + ".hdr");

	reflectionMap = labhelper::loadHdrMipmapTexture(filenames);
	environmentMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + ".hdr");
	irradianceMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + "_irradiance.hdr");

	int w2, h2, comp2;
	unsigned char* image2 = stbi_load("../scenes/explosion.png", &w2, &h2, &comp2, STBI_rgb_alpha);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w2, h2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image2);
	free(image2);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);


	shadowMapFB.resize(shadowMapResolution, shadowMapResolution);

	glEnable(GL_DEPTH_TEST); // enable Z-buffering
	glEnable(GL_CULL_FACE);  // enables backface culling

	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

	/// draw particle ///

	unsigned int active_particles = particle_system.particles.size();
	 //Code for extracting data goes here 
	// sort particles with sort from c++ standard library

	/*for (int i = 0; i < 1000; i++) {

		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		//const float u = labhelper::uniform_randf(-1.f, 1.f);
		const float u = labhelper::uniform_randf(0.95f, 1.f);
		glm::vec3 pos = 10.0f *glm::vec3(sqrt(1.f - u * u) * cosf(theta), u, sqrt(1.f - u * u) * sinf(theta));

		data.push_back(glm::vec4(pos,1.0f));
		data1.push_back(glm::vec4(pos, 1.0f));
	}
	*/
	glGenBuffers(1, &positionBuffer);
	// Set the newly created buffer as the current one
	glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
	glBufferData(GL_ARRAY_BUFFER, 100000 * sizeof(glm::vec4), nullptr, GL_STATIC_DRAW);


	// Create a vertex array object and connect the vertex buffer objects to it
	glGenVertexArrays(1, &vertexArrayObject);
	// Bind the vertex array object
	glBindVertexArray(vertexArrayObject);
	glVertexAttribPointer(0, 4, GL_FLOAT, false /*normalized*/, 0 /*stride*/, 0 /*offset*/);
	glEnableVertexAttribArray(0); // Enable the vertex position attribute
	glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);

}

void debugDrawLight(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrix,
                    const glm::vec3& worldSpaceLightPos)
{
	mat4 modelMatrix = glm::translate(worldSpaceLightPos);
	glUseProgram(shaderProgram);
	labhelper::setUniformSlow(shaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * modelMatrix);
	labhelper::render(sphereModel);
}


void drawBackground(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	glUseProgram(backgroundProgram);
	labhelper::setUniformSlow(backgroundProgram, "environment_multiplier", environment_multiplier);
	labhelper::setUniformSlow(backgroundProgram, "inv_PV", inverse(projectionMatrix * viewMatrix));
	labhelper::setUniformSlow(backgroundProgram, "camera_pos", cameraPosition);
	labhelper::drawFullScreenQuad();
}

void drawScene(GLuint currentShaderProgram,
               const mat4& viewMatrix,
               const mat4& projectionMatrix,
               const mat4& lightViewMatrix,
               const mat4& lightProjectionMatrix)
{
	glUseProgram(currentShaderProgram);
	// Light source
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_intensity_multiplier",
	                          point_light_intensity_multiplier);
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightDir",
	                          normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));


	// Environment
	labhelper::setUniformSlow(currentShaderProgram, "environment_multiplier", environment_multiplier);

	// camera
	labhelper::setUniformSlow(currentShaderProgram, "viewInverse", inverse(viewMatrix));

	// landing pad
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * landingPadModelMatrix)));

	labhelper::render(landingpadModel);

	// Fighter
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * fighterModelMatrix)));

	labhelper::render(fighterModel);
}


void display(void)

{

	for (int i = 0; i < 16; i++) {

		// particle process 
		Particle p;
		p.lifetime = 0.0f;
		p.life_length = 5.0f;
		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		//const float u = labhelper::uniform_randf(-1.f, 1.f);
		const float u = labhelper::uniform_randf(0.95f, 1.f);
		//p.velocity = 10.0f * glm::vec3(sqrt(1.f - u * u) * cosf(theta), u, sqrt(1.f - u * u) * sinf(theta));
		p.velocity = glm::vec3(R * 10.0f * vec4(glm::vec3(u, sqrt(1.f - u * u) * cosf(theta), sqrt(1.f - u * u) * sinf(theta)), 1.0f));

		//particleStart = vec3(8.0f, 10.0f, 8.0f);
		//particleStart = glm::vec3((fighterModelMatrix * vec4(particleStart, 0.0f)));
		p.pos = glm::vec3((fighterModelMatrix * vec4(particleStart, 1.0f)));

		particle_system.spawn(p);   
		
	}
	particle_system.process_particles(deltaTime);
	//particle_system.process_particles(deltaTime); Q2

	///////////////////////////////////////////////////////////////////////////
	// Check if window size has changed and resize buffers as needed
	///////////////////////////////////////////////////////////////////////////
	{
		int w, h;
		SDL_GetWindowSize(g_window, &w, &h);
		if(w != windowWidth || h != windowHeight)
		{
			windowWidth = w;
			windowHeight = h;
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// setup matrices
	///////////////////////////////////////////////////////////////////////////
	mat4 projMatrix = perspective(radians(45.0f), float(windowWidth) / float(windowHeight), 5.0f, 2000.0f);
	mat4 viewMatrix = lookAt(cameraPosition, cameraPosition + cameraDirection, worldUp);

	vec4 lightStartPosition = vec4(40.0f, 40.0f, 0.0f, 1.0f);
	lightPosition = vec3(rotate(currentTime, worldUp) * lightStartPosition);
	mat4 lightViewMatrix = lookAt(lightPosition, vec3(0.0f), worldUp);
	mat4 lightProjMatrix = perspective(radians(45.0f), 1.0f, 25.0f, 100.0f);

	///////////////////////////////////////////////////////////////////////////
	// Bind the environment map(s) to unused texture units
	///////////////////////////////////////////////////////////////////////////
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, environmentMap);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, irradianceMap);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, reflectionMap);
	glActiveTexture(GL_TEXTURE0);

	


	///////////////////////////////////////////////////////////////////////////
// Set up shadow map parameters
///////////////////////////////////////////////////////////////////////////
// >>> @task 1
	if (shadowMapFB.width != shadowMapResolution || shadowMapFB.height != shadowMapResolution) {
		shadowMapFB.resize(shadowMapResolution, shadowMapResolution);
	}


	//Task 4 Outside the shadow map
	if (shadowMapClampMode == ClampMode::Edge) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	if (shadowMapClampMode == ClampMode::Border) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		vec4 border(shadowMapClampBorderShadowed ? 0.f : 1.f);
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border.x);
	}


	///////////////////////////////////////////////////////////////////////////
	// Draw Shadow Map
	///////////////////////////////////////////////////////////////////////////

	glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFB.framebufferId);
	glViewport(0, 0, shadowMapFB.width, shadowMapFB.height);
	glClearColor(0.2, 0.2, 0.8, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//able the polygon offset 
	if (usePolygonOffset) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(polygonOffset_factor, polygonOffset_units);
	}


	//able PCF 



	if (useHardwarePCF) {


		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	}

	drawScene(simpleShaderProgram, lightViewMatrix, lightProjMatrix, lightViewMatrix, lightProjMatrix);

	labhelper::Material& screen = landingpadModel->m_materials[8];
	screen.m_emission_texture.gl_id = shadowMapFB.colorTextureTargets[0];


	//disable the polygon offset again
	if (usePolygonOffset) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}



	///////////////////////////////////////////////////////////////////////////
	// Draw from camera
	///////////////////////////////////////////////////////////////////////////
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	drawBackground(viewMatrix, projMatrix);
	drawScene(shaderProgram, viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));


	glUseProgram(shaderProgram);

	mat4 lightMatrix = translate(vec3(0.5f)) * scale(vec3(0.5f)) * lightProjMatrix * lightViewMatrix * inverse(viewMatrix);
	labhelper::setUniformSlow(shaderProgram, "lightMatrix", lightMatrix);


	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);


	drawBackground(viewMatrix, projMatrix);
	drawScene(shaderProgram, viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));



	








	for (int i = 0; i < particle_system.particles.size(); i++) {
		data1[i] = viewMatrix * vec4(particle_system.particles[i].pos, 1.0f);
		data1[i].w = particle_system.particles[i].lifetime/particle_system.particles[i].life_length;
	}

	std::sort(data1.begin(), std::next(data1.begin(), particle_system.particles.size()),
		[](const vec4& lhs, const vec4& rhs) { return lhs.z < rhs.z; });



	glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
	// Send the vertex position data to the current buffer
	//glBufferData(GL_ARRAY_BUFFER, particle_system.particles.size() *sizeof(glm::vec4), data1.data(), GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, particle_system.particles.size() * sizeof(glm::vec4), data1.data());
	
	//Q1

	glUseProgram(particleProgram);




	labhelper::setUniformSlow(particleProgram, "P", projMatrix);


	glBindVertexArray(vertexArrayObject);
	//glDrawArrays(GL_POINTS, 0, 1000); //active particles 

	// Enable shader program point size modulation.
	glEnable(GL_PROGRAM_POINT_SIZE);
	// Enable blending.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);




	labhelper::setUniformSlow(particleProgram, "screen_x", (float)windowWidth);
	labhelper::setUniformSlow(particleProgram, "screen_y", (float)windowHeight);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glDrawArrays(GL_POINTS, 0, particle_system.particles.size()); //active particles 

	glDisable(GL_PROGRAM_POINT_SIZE);
	// Enable blending.
	glDisable(GL_BLEND);

	//glUseProgram(0);

	CHECK_GL_ERROR();

	
}


/*

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
		vec3 RandVelocity = glm::mat3(vel) * vec3(u, sqrt(1.f - u * u) * cosf(theta), sqrt(1.f - u * u) * sinf(theta));

		// particles.at(i).velocity += 0.01f * mat3(fighterModelMatrix) * RandVelocity;
		particles.at(i).velocity += dt* RandVelocity;
		particles.at(i).pos += (dt * particles.at(i).velocity);
		particles.at(i).lifetime += dt;
	}
}
*/


bool handleEvents(void)
{
	// check events (keyboard among other)
	SDL_Event event;
	bool quitEvent = false;
	while(SDL_PollEvent(&event))
	{
		if(event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
		{
			quitEvent = true;
		}
		if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
		{
			showUI = !showUI;
		}
		if(event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
		   && (!showUI || !ImGui::GetIO().WantCaptureMouse))
		{
			g_isMouseDragging = true;
			int x;
			int y;
			SDL_GetMouseState(&x, &y);
			g_prevMouseCoords.x = x;
			g_prevMouseCoords.y = y;
		}

		if(!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
		{
			g_isMouseDragging = false;
		}

		if(event.type == SDL_MOUSEMOTION && g_isMouseDragging)
		{
			// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
			int delta_x = event.motion.x - g_prevMouseCoords.x;
			int delta_y = event.motion.y - g_prevMouseCoords.y;
			float rotationSpeed = 0.1f;
			mat4 yaw = rotate(rotationSpeed * deltaTime * -delta_x, worldUp);
			mat4 pitch = rotate(rotationSpeed * deltaTime * -delta_y,
			                    normalize(cross(cameraDirection, worldUp)));
			cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
			g_prevMouseCoords.x = event.motion.x;
			g_prevMouseCoords.y = event.motion.y;
		}
	}

	// check keyboard state (which keys are still pressed)
	const uint8_t* state = SDL_GetKeyboardState(nullptr);
	vec3 cameraRight = cross(cameraDirection, worldUp);

	if(state[SDL_SCANCODE_W])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraDirection;
	}
	if(state[SDL_SCANCODE_S])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraDirection;
	}
	if(state[SDL_SCANCODE_A])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraRight;
	}
	if(state[SDL_SCANCODE_D])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraRight;
	}
	if(state[SDL_SCANCODE_Q])
	{
		cameraPosition -= cameraSpeed * deltaTime * worldUp;
	}
	if(state[SDL_SCANCODE_E])
	{
		cameraPosition += cameraSpeed * deltaTime * worldUp;
	}
	return quitEvent;
}

void gui()
{
	// Inform imgui of new frame
	ImGui_ImplSdlGL3_NewFrame(g_window);

	// ----------------- Set variables --------------------------
	ImGui::SliderInt("Shadow Map Resolution", &shadowMapResolution, 32, 2048);
	ImGui::Text("Polygon Offset");
	ImGui::Checkbox("Use polygon offset", &usePolygonOffset);
	ImGui::SliderFloat("Factor", &polygonOffset_factor, 0.0f, 10.0f);
	ImGui::SliderFloat("Units", &polygonOffset_units, 0.0f, 100.0f);
	ImGui::Text("Clamp Mode");
	ImGui::RadioButton("Clamp to edge", &shadowMapClampMode, ClampMode::Edge);
	ImGui::RadioButton("Clamp to border", &shadowMapClampMode, ClampMode::Border);
	ImGui::Checkbox("Border as shadow", &shadowMapClampBorderShadowed);
	ImGui::Checkbox("Use spot light", &useSpotLight);
	ImGui::Checkbox("Use soft falloff", &useSoftFalloff);
	ImGui::SliderFloat("Inner Deg.", &innerSpotlightAngle, 0.0f, 90.0f);
	ImGui::SliderFloat("Outer Deg.", &outerSpotlightAngle, 0.0f, 90.0f);
	ImGui::Checkbox("Use hardware PCF", &useHardwarePCF);
	ImGui::Checkbox("Manual light only (right-click drag to move)", &lightManualOnly);
	ImGui::DragFloat3("¨Drag", &particleStart.x, 1.0f, -20.0f, 20.f);
	//Q1
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
		ImGui::GetIO().Framerate);
	// ----------------------------------------------------------

	// Render the GUI.
	ImGui::Render();
}

int main(int argc, char* argv[])
{
	g_window = labhelper::init_window_SDL("OpenGL Project");

	initGL();

	bool stopRendering = false;
	auto startTime = std::chrono::system_clock::now();

	/*while (!stopRendering)
	{
		//update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		previousTime = currentTime;
		currentTime = timeSinceStart.count();
		deltaTime = currentTime - previousTime;
		// render to window
		display();

		// Render overlay GUI.
		if(showUI)
		{
			gui();
		}

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);

		// check events (keyboard among other)


		stopRendering = handleEvents();
	}
	*/
	

	while (!stopRendering)
	{
		// update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		deltaTime = timeSinceStart.count() - currentTime;
		currentTime = timeSinceStart.count();

		// render to window
		display();

		// Render overlay GUI.
		if (showUI)
		{
			gui();
		}

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);

		// check new events (keyboard among other)
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			// Allow ImGui to capture events.
			ImGui_ImplSdlGL3_ProcessEvent(&event);

			// More info at https://wiki.libsdl.org/SDL_Event
			if (event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
			{
				stopRendering = true;
			}
			if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
			{
				showUI = !showUI;
			}
			else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
				&& (!showUI || !ImGui::GetIO().WantCaptureMouse))
			{
				g_isMouseDragging = true;
				int x;
				int y;
				SDL_GetMouseState(&x, &y);
				g_prevMouseCoords.x = x;
				g_prevMouseCoords.y = y;
			}

			if (!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
			{
				g_isMouseDragging = false;
			}

			if (event.type == SDL_MOUSEMOTION && g_isMouseDragging)
			{
				// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
				int delta_x = event.motion.x - g_prevMouseCoords.x;
				int delta_y = event.motion.y - g_prevMouseCoords.y;

				/*if (event.button.button == SDL_BUTTON_LEFT)
				{
					printf("Mouse motion while left button down (%i, %i)\n", event.motion.x, event.motion.y);
				}


				g_prevMouseCoords.x = event.motion.x;
				g_prevMouseCoords.y = event.motion.y;

				*/

				if (event.button.button & SDL_BUTTON(SDL_BUTTON_LEFT)) {
					float rotationSpeed = 0.005f;
					mat4 yaw = rotate(rotationSpeed * -delta_x, worldUp);
					mat4 pitch = rotate(rotationSpeed * -delta_y, normalize(cross(cameraDirection, worldUp)));
					cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
				}


			}



		}

		// check keyboard state (which keys are still pressed)
		const uint8_t* state = SDL_GetKeyboardState(nullptr);

		/*if (state[SDL_SCANCODE_LEFT]) {
			T[3] += speed * deltaTime * vec4(1.0f, 0.0f, 0.0f, 0.0f);
		}
		if (state[SDL_SCANCODE_RIGHT]) {
			T[3] -= speed * deltaTime * vec4(1.0f, 0.0f, 0.0f, 0.0f);
		}*/
		fighterModelMatrix = T; //use T as the model matrix


		
		// implement Steering
		const float rotateSpeed = 10.0f;
		if (state[SDL_SCANCODE_LEFT]) {
			R[0] -= rotateSpeed * deltaTime * R[2];
		}
		if (state[SDL_SCANCODE_RIGHT]) {
			R[0] += rotateSpeed * deltaTime * R[2];
		}
		

		// implement controls based on key states
		const float speed = 10.0f;
		if (state[SDL_SCANCODE_UP]) {
			T[3] += R * speed * deltaTime * vec4(0.0f, 1.0f, 0.0f, 0.0f); //fix to new direction 
		}
		if (state[SDL_SCANCODE_DOWN]) {
			T[3] -= R * speed * deltaTime * vec4(0.0f, 1.0f, 0.0f, 0.0f); //fix to new direction 
		}


		// Make R orthonormal again
		R[0] = normalize(R[0]);
		R[2] = vec4(cross(vec3(R[0]), vec3(R[1])), 0.0f);

		
		fighterModelMatrix = T * R;
		//particleStart = glm::vec3((fighterModelMatrix * vec4(particleStart, 0.0f)));

		/*


		// implement controls based on currentTime and rotate on y axis
		T2 = glm::translate(vec3(5.0f, 1.0f, 1.0f));
		R2 = glm::rotate(-currentTime, vec3(0.0f, 1.0f, 0.0f));
		S2 = glm::scale(vec3(1.0f, 2.0f, 1.0f));

		//fighterModelMatrix = S2 * R2 * T2; */

	}
	// Free Models
	labhelper::freeModel(fighterModel);
	labhelper::freeModel(landingpadModel);
	labhelper::freeModel(sphereModel);

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}
