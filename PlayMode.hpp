#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up, space;

	struct Proj {
		Scene::Transform *transform = nullptr;
		glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f);
	} a1, a2, a3;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//hexapod leg to wobble:
	Scene::Transform *base = nullptr;
	Scene::Transform *body = nullptr;
	Scene::Transform *cage = nullptr;
	//Scene::Transform *a1 = nullptr;
	//Scene::Transform *a2= nullptr;
	//Scene::Transform *a3 = nullptr;

	//glm::vec3 get_leg_tip_position();

	//music:
	std::shared_ptr< Sound::PlayingSample > bgm_loop;

	//sfx (max 3)
	std::shared_ptr< Sound::PlayingSample > sfx_a1;
	std::shared_ptr< Sound::PlayingSample > sfx_a2;
	std::shared_ptr< Sound::PlayingSample > sfx_a3;

	std::shared_ptr< Sound::PlayingSample > bonk;
	//camera:
	Scene::Camera *camera = nullptr;

};
