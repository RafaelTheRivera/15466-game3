#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <chrono>
#include <random>
#include <cmath>

auto timeNow(){
	return std::chrono::system_clock::now();
}
static constexpr float gravity = 0.049f;
static float velocity = 0.0f;
static bool jumping = false;
static constexpr float jumppower = 1.0f;
static constexpr glm::vec3 offscreenPosition = glm::vec3(0.0f, 0.0f, -100.0f);
static std::mt19937 rng(int(std::chrono::system_clock::now().time_since_epoch().count()));
static constexpr float projV = 15.0f;
static constexpr float projD = 30.0f;
static auto lastShot = timeNow();
static uint32_t score = 0;
static uint32_t best = 0;
static constexpr float initTempo = 2000.0f;
static float tempo = initTempo;
static constexpr float dTempo = -50.0f;
static constexpr float minTempo = 1000.0f;
static uint8_t nextShot = 0;
static constexpr float checkDist = 1.0f;
static uint32_t loops = 0;
static constexpr float cageRadius = 16.5f;
static float bonkTimer = 0.0f;

GLuint playarea_meshes_for_lit_color_texture_program = 0;


Load< MeshBuffer > playarea_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("playarea.pnct"));
	playarea_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > playarea_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("playarea.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = playarea_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = playarea_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

Load< Sound::Sample > bgm_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("bgm.opus"));
});

Load< Sound::Sample > proj_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("proj.opus"));
});

Load< Sound::Sample > bonk_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("bonk.opus"));
});

PlayMode::PlayMode() : scene(*playarea_scene) {
	//get pointers to leg for convenience:
	for (auto &transform : scene.transforms) {
		if (transform.name == "Base") base = &transform;
		else if (transform.name == "Body") body = &transform;
		else if (transform.name == "Cage") cage = &transform;
		else if (transform.name == "Arrow1") a1.transform = &transform;
		else if (transform.name == "Arrow2") a2.transform = &transform;
		else if (transform.name == "Arrow3") a3.transform = &transform;
		// else if (transform.name == "LowerLeg.FL") lower_leg = &transform;
	}
	if (base == nullptr) throw std::runtime_error("base not found.");
	if (body == nullptr) throw std::runtime_error("body not found.");
	if (cage == nullptr) throw std::runtime_error("cage not found.");
	if (a1.transform == nullptr) throw std::runtime_error("a1 not found.");
	if (a2.transform == nullptr) throw std::runtime_error("a2 not found.");
	if (a3.transform == nullptr) throw std::runtime_error("a3 not found.");

	a1.transform->position = offscreenPosition;
	a2.transform->position = offscreenPosition;
	a3.transform->position = offscreenPosition;
	//if (lower_leg == nullptr) throw std::runtime_error("Lower leg not found.");

	/*hip_base_rotation = hip->rotation;
	upper_leg_base_rotation = upper_leg->rotation;
	lower_leg_base_rotation = lower_leg->rotation;*/

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start music loop playing:
	// (note: position will be over-ridden in update())
	bgm_loop = Sound::loop(*bgm_sample, 0.3f, 0.0f);
	sfx_a1 = Sound::play_3D(*proj_sample, 0.5f, a1.transform->position, 0.0f);
	sfx_a2 = Sound::play_3D(*proj_sample, 0.5f, a2.transform->position, 0.0f);
	sfx_a3 = Sound::play_3D(*proj_sample, 0.5f, a3.transform->position, 0.0f);
	bonk = Sound::play_3D(*bonk_sample, 0.5f, a1.transform->position, 0.0f);
}

PlayMode::~PlayMode() {

}

glm::vec3 shoot(PlayMode::Proj a, glm::vec3 cameraPos){
	float yaw = float(rng()) / float(rng.max()) * 3.14159f * 2.0f;
	float pitch = float(rng()) / float(rng.max()) * 3.14159f / 2.0f;
	float x = sin(yaw);
	float y = cos(yaw);
	float z = sin(pitch);
	glm::vec3 dir = glm::normalize(glm::vec3(x,y,z));
	a.transform->position = dir * projD;
	a.transform->position += glm::vec3(cameraPos.x, cameraPos.y, 0.0f);
	//a.velocity = dir * projV;
	//std::cout << "Placed new shot at " << glm::to_string(a.transform->position) << std::endl;
	//std::cout << "With Velocity" << glm::to_string(a.velocity) << std::endl;
	nextShot = (nextShot+1) % 3;
	loops += 1;
	if (loops > 3) {
		score += 1;
		if (score > best) best = score;
	}
	return (dir * projV);
}

float dist(glm::vec3 a, glm::vec3 b) {
	return std::sqrt(std::abs((b.x-a.x) * (b.x-a.x) + (b.y-a.y) * (b.y-a.y) + (b.z-a.z) * (b.z-a.z)));
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.downs += 1;
			space.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			glm::vec3 yrp = glm::eulerAngles(camera->transform->rotation);
			yrp.y = 0.0f;
			//std::cout << glm::to_string(yrp) << std::endl;
			yrp.x = std::abs(yrp.x);

			camera->transform->rotation = glm::quat(yrp);
			return true;
		}
	}

	return false;
}


void PlayMode::update(float elapsed) {
	auto now = timeNow();

	//slowly rotates through [0,1):
	/*wobble += elapsed / 10.0f;
	wobble -= std::floor(wobble);*/

	/*hip->rotation = hip_base_rotation * glm::angleAxis(
		glm::radians(5.0f * std::sin(wobble * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);
	upper_leg->rotation = upper_leg_base_rotation * glm::angleAxis(
		glm::radians(7.0f * std::sin(wobble * 2.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);
	lower_leg->rotation = lower_leg_base_rotation * glm::angleAxis(
		glm::radians(10.0f * std::sin(wobble * 3.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);*/

	//move sound to follow leg tip position:
	// leg_tip_loop->set_position(get_leg_tip_position(), 1.0f / 60.0f);

	//move camera:
	{

		//combine inputs into a move:
		constexpr float PlayerSpeed = 30.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];
		frame_forward.z = 0.0f;
		frame_forward = glm::normalize(frame_forward);
		camera->transform->position += move.x * frame_right + move.y * frame_forward;

		float d = dist(camera->transform->position, glm::vec3(0.0f,0.0f,0.0f));
		if (d > cageRadius){
			camera->transform->position = glm::normalize(camera->transform->position) * cageRadius;
		}
	}

	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	    // check if hit
		if (dist(camera->transform->position, a1.transform->position) < checkDist && bonkTimer == 0.0f){
				score = 0;
				tempo = initTempo;
				bonk = Sound::play_3D(*bonk_sample, 0.5f, camera->transform->position, 2.0f);
				a1.transform->position = offscreenPosition;
				sfx_a1->set_volume(0.0f);
				bonkTimer = 2.0f;
		}
		else if (dist(camera->transform->position, a2.transform->position) < checkDist && bonkTimer == 0.0f){
				score = 0;
				tempo = initTempo;
				bonk = Sound::play(*bonk_sample, 0.5f);
				a2.transform->position = offscreenPosition;
				sfx_a2->set_volume(0.0f);
				bonkTimer = 2.0f;
		}
		else if (dist(camera->transform->position, a3.transform->position) < checkDist && bonkTimer == 0.0f){
				score = 0;
				tempo = initTempo;
				bonk = Sound::play(*bonk_sample, 0.5f);
				a3.transform->position = offscreenPosition;
				sfx_a3->set_volume(0.0f);
				bonkTimer = 2.0f;
		}
		
	}

	{ //handle jump
		if (space.pressed && jumping != true) {
			jumping = true;
			velocity += jumppower;
		}
	}

	{ //gravity
		velocity -= gravity;
		camera->transform->position.z += velocity;
		if (camera->transform->position.z < 0.0f){
			camera->transform->position.z = 0.0f;
			velocity = 0.0f;
			jumping = false;
		}
	}

	{ // move projectiles
		//std::cout << glm::to_string(a1.velocity);
		a1.transform->position -= a1.velocity * elapsed;
		a2.transform->position -= a2.velocity * elapsed;
		a3.transform->position -= a3.velocity * elapsed;
		sfx_a1->set_position(a1.transform->position);
		sfx_a2->set_position(a2.transform->position);
		sfx_a3->set_position(a3.transform->position);
	}
	//std::cout << "Last shot: " << std::to_string(lastShot) << "; Tempo: " << std::to_string(tempo) << std::endl;
	//std::cout << "Now: " << std::to_string(now) << "; Compared: " << std::to_string(lastShot + tempo) << std::endl;
	
	// Possibly the silliest thing I've figured out how to do (I thought elapsed was not in seconds.)
	auto passed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot).count();

	if (passed > tempo){
		//std::cout << "Shot" << std::endl;
		if (nextShot == 0){
			a1.velocity = shoot(a1, camera->transform->position);
			sfx_a1 = Sound::play_3D(*proj_sample, 0.5f, a1.transform->position, 3.0f);
		}
		else if (nextShot == 1){
			a2.velocity = shoot(a2, camera->transform->position);
			sfx_a2 = Sound::play_3D(*proj_sample, 0.5f, a2.transform->position, 3.0f);
		} else {
			a3.velocity = shoot(a3, camera->transform->position);
			sfx_a3 = Sound::play_3D(*proj_sample, 0.5f, a3.transform->position, 3.0f);
		}
		lastShot = now;
		if (tempo > minTempo){
			tempo += dTempo;
		}
	}

	bonkTimer -= elapsed;
	if (bonkTimer < 0.0f) bonkTimer = 0.0f;
	/*else {
		std::cout << std::to_string(bonkTimer) << std::endl;
	}*/

	//body follows camera (but not its rotation)
	body->position = camera->transform->position;
	
	//std::cout << glm::to_string(camera->transform->position) << std::endl;

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Current: " + std::to_string(score) + "; " + "Best: " + std::to_string(best),
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Current: " + std::to_string(score) + "; " + "Best: " + std::to_string(best),
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
	GL_ERRORS();
}

/*glm::vec3 PlayMode::get_leg_tip_position() {
	//the vertex position here was read from the model in blender:
	return lower_leg->make_local_to_world() * glm::vec4(-1.26137f, -11.861f, 0.0f, 1.0f);
}*/