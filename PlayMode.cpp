#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <cmath>

const float GROUND_LEVEL = 0;

/*************************
 * General Object Structs
 *************************/
struct GameObject {
	// for reference, Tireler has a radius of 1 unit
	Scene::Transform *transform;

	glm::vec3 transform_forward() {
		// TODO: get forward
		std::cout << transform->rotation.axis() << std::endl;
		return {0,0,0};
	};
};

struct ColliderCube {
	glm::vec3<float> offset; // from a gameObject
	std::vec3<float> dimensions;
};

struct PhysicsObject {
	glm::vec3<float> velocity; // uses blender convention, so z is up!
	glm::vec3<float> gravity;
	float mass;

	float lateralSpeed() {
		return std::sqrtf(velocity.x * velocity.x, velocity.y * velocity.y);
	};
};

struct SquetchearAnimator {
	// TODO: Squash-Stretch-Shear ("Squetchear") animator for Tireler.
	// the animator puts rotation to (0, 0, 0), with z axis as up
	// we apply z axis, then x axis, then y.
	// then we apply squash or stretch
	// and then we shear
	
	glm::quat pre_anim_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	float verticality = 1; // 1 is the base, <1 is squash, >1 is stretch
	float shear = 1;
};

/*************************
 * Specific game entities
 *************************/
struct Player {
	/*******************
	 * Game Rules Logic
	 *******************/
	float score = 0;
	float SCORE_GAIN = 100; // 100 points per second of driving
	float multiplier = 1;
	float MULTIPLIER_GAIN = 1; // adds this to multiplier every time a medal is collected
	float COMBO_DURATION = 10; // if you don't collect a new medal in this time, the multiplier resets
	float comboTimer = 0;

	/*********************
	 * physics components
	 *********************/
	// TODO: adjust values as needed

	// state components
	float health = 100;
	bool accelerating = false;
	bool airborne = false;
	// timers are also used as state components

	// basic movement
	float TURN_SPEED = 8 * pi_v; // radians per second. Scales with current lateral
	float TOP_BASE_SPEED_LATERAL = 10; // units per second^2
	float GROUND_ACCEL = 10; // units per second^2, applied while accelerating
	float AIR_ACCEL = 1; // units per second^2, applied while accelerating
	float FRICTION_DECEL = 5; // applied on the ground while not accelerating
	float JUMP_STREGTH = 10; // initial jump speed, units per second

	// brake boost
	float BRAKE_DECEL = 8;
	float CHARGE_TIME = 1;
	float chargeTimer = 0;
	float BOOST_POWER = 1.5f; // multiplies TOP_BASE_SPEED while boosting
	float BOOST_TIME = 2;
	float boostTimer = 0;

	// TODO: initialize
	GameObject *gameObject;
	ColliderCube *collider;
	PhysicsObject *physicsObject;
	SquetchearAnimator *animator;

	void turn(float direction, float t) {
		// TODO (direction = -1 or 1)
		const glm::vec3 *old_rotation = &(gameObject->transform->rotation);
		glm::vec3 axis = glm::axis(old_rotation);
		glm::vec3 angles = glm::eulerAngles(old_rotation);

		glm::vec3 new_rotation;
		float angle_delta = TURN_SPEED * direction * -1 * (physicsObject->lateralSpeed() / TOP_BASE_SPEED_LATERAL);
		angles.z += angle_delta;
		new_rotation = glm::angleAxis(angles, axis);

		// TODO: apply rotation to old_angles, and construct the new quaternion.
	};

	void accelerate() {
		glm::vec3 *rotation = &(gameObject->transform->rotation);
		glm::vec3 *velocity = &(physicsObject->velocity);

		*velocity += (physicsObject->gravity +  (rotation->forward * (!airborne ? GROUND_ACCEL : AIR_ACCEL))) * t;

		accelerating = true;
	};

	bool jump() {
		if (airborne) return false;

		physicsObject->velocity.z += JUMP_STRENGTH;
		airborne = true;
		return true;
	};

	void charge(float t) {
		if (chargeTimer < CHARGE_TIME)
			chargeTimer += t;
	};

	bool boost() {
		glm::vec3 *rotation = &(gameObject->transform->rotation);
		glm::vec3 *velocity = &(physicsObject->velocity);
		if (chargeTimer >= CHARGE_TIME) {
			// TODO: set velocity in forward direction, including jump if in the airborne
			*velocity = *rotation->forward() * TOP_BASE_SPEED_LATERAL * BOOST_POWER;
			chargeTimer = 0;
			return true;
		}
		return false;
	};

	void update(float t) {
		glm::vec3 *position = &(gameObject->transform->position);
		glm::vec3 *velocity = &(physicsObject->velocity);

		/******************
		 * Physics Updates
		 ******************/
		glm::vec2 lat_vel_norm;
		if (!accelerating && !airborne) {
			lat_vel_norm = glm::normalize({(*velocity).x, (*velocity).y});
			(*velocity).x -= lat_vel_norm.x * FRICTION_DECEL;
			(*velocity).y -= lat_vel_norm.y * FRICTION_DECEL;
		}

		// clamp lateral speed
		lat_vel_norm = glm::normalize({(*velocity).x, (*velocity).y});
		float max_lat_speed = TOP_BASE_SPEED_LATERAL * (boostTimer <= 0 ? 1 : BOOST_POWER);
		float final_lat_speed = physicsObject->lateralSpeed();
		if (physicsObject->lateralSpeed() > max_lat_speed) { // bring it down to top speed if exceeding it
			final_lat_speed = std::max(max_lat_speed, final_lat_speed - (FRICTION_DECEL * 2 * t));
		}
		glm::vec2 new_lat_vel = lat_vel_norm * final_lat_speed;
		(*velocity).x = new_lat_vel.x;
		(*velocity).y = new_lat_vel.y;

		// Apply gravity
		*velocity += physicsObject->gravity * t;

		// Update position based on gravity
		*position += (*velocity * t);


		/*************
		 * Collision logic
		 ************/
		// TODO

		/*********************
		 * Game Logic Updates
		 *********************/
		accelerating = false;
		if (boostTimer > 0) boostTimer = std::clamp(boostTimer - t, 0, BOOST_TIME);
		if (chargeTimer > 0) chargeTimer = std::clamp(chargeTimer - t, 0, CHARGE_TIME);
		if (comboTimer > 0) comboTimer = std::clamp(comboTimer - t, 0, COMBO_DURATION);
		score += physicsObject->lateralSpeed() * t * SCORE_GAIN * multiplier;

		/********************
		 * Animation Updates
		 ********************/
		// TODO
	};
}

struct Meteor {
	// TODO
};

struct Fire {
	// TODO
};

struct Medal {
	// TODO
};

struct Spring {
	// TODO
};

struct Building {
	// TODO
};

struct Tree {
	// TODO
};

GLuint burning_meshes_for_lit_color_texture_program = 0;
// Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
// 	MeshBuffer const *ret = new MeshBuffer(data_path("hexapod.pnct"));
// 	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
// 	return ret;
// });

// Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
// 	return new Scene(data_path("hexapod.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
// 		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

// 		scene.drawables.emplace_back(transform);
// 		Scene::Drawable &drawable = scene.drawables.back();

// 		drawable.pipeline = lit_color_texture_program_pipeline;

// 		drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
// 		drawable.pipeline.type = mesh.type;
// 		drawable.pipeline.start = mesh.start;
// 		drawable.pipeline.count = mesh.count;

// 	});
// });

Load< MeshBuffer > burnin_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("burnin.pnct"));
	burning_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > burnin_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("burnin.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

// Makes a copy of a scene, in case you want to modify it.
PlayMode::PlayMode() : scene(*hexapod_scene) {
	//get pointers to leg for convenience:
	for (auto &transform : scene.transforms) {
	// 	if (transform.name == "Hip.FL") hip = &transform;
	// 	else if (transform.name == "UpperLeg.FL") upper_leg = &transform;
	// 	else if (transform.name == "LowerLeg.FL") lower_leg = &transform;
	}
	// if (hip == nullptr) throw std::runtime_error("Hip not found.");
	// if (upper_leg == nullptr) throw std::runtime_error("Upper leg not found.");
	// if (lower_leg == nullptr) throw std::runtime_error("Lower leg not found.");

	// hip_base_rotation = hip->rotation;
	// upper_leg_base_rotation = upper_leg->rotation;
	// lower_leg_base_rotation = lower_leg->rotation;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		} else if (evt.key.key == SDLK_A) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//slowly rotates through [0,1):
	wobble += elapsed / 10.0f;
	wobble -= std::floor(wobble);

	hip->rotation = hip_base_rotation * glm::angleAxis(
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
	);

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

		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];

		camera->transform->position += move.x * frame_right + move.y * frame_forward;
	}

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

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

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
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
}
