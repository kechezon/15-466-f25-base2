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
#include <numbers>

const float GROUND_LEVEL = 0.0f;
GLuint burning_meshes_for_lit_color_texture_program = 0;

Load< MeshBuffer > burnin_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("burnin.pnct"));
	burning_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > burnin_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("burnin.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = burnin_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

Scene::Drawable new_drawable(Mesh const &mesh, Scene::Transform *tf) {
	Scene::Drawable drawable = burnin_scene->drawables.back();
	drawable.pipeline = lit_color_texture_program_pipeline;

	drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
	drawable.pipeline.type = mesh.type;
	drawable.pipeline.start = mesh.start;
	drawable.pipeline.count = mesh.count;

	drawable.transform = tf;

	return drawable;
}

/*************************
 * General Object Structs
 *************************/
struct GameObject {
	// for reference, Tireler has a radius of 1 unit, and a width of 1 unit

	// the actual object (will be reset to apply anim animations)
	Scene::Transform *transform;
	Scene::Drawable &drawable;

	glm::vec3 transform_forward() {
		// std::cout << (transform->rotation * glm::vec3(0.0f, 1.0f, 0.0f)) << std::endl;
		return transform->rotation * glm::vec3(0.0f, 1.0f, 0.0f);
	};
};

struct ColliderBox {
	std::string tag;
	glm::vec3 dimensions;
	glm::vec3 offset; // from a gameObject

	bool collider_test(GameObject myObj, GameObject otherObj, ColliderBox other) {
		glm::vec3 myPosition = myObj->transform->position;
		glm::vec3 otherPosition = otherObj->transform->position;

		// TODO: test each corner of this box against the bounds of the other box

		// for (int myIdx = 0; myIdx < 3; myIdx++) {
		// 	// glm::vec3 toMyBorder0 = glm::vec3(0.0f);
		// 	// toBorderMe0[myIdx] = myPosition[myIdx] + (dimensions[myIdx] / 2) + offset[myIdx];
		// 	// toBorderMe0 = myObj->transform->rotation * toBorderMe0;

		// 	// glm::vec3 toBorderMe1 = glm::vec3(0.0f);
		// 	// toBorderMe1[myIdx] = myPosition[myIdx] - (dimensions[myIdx] / 2) + offset[myIdx];
		// 	// toBorderMe1 = myObj->transform->rotation * toBorderMe1;

		// 	for (int otherIdx = 0; otherIdx < 3; otherIdx++) {
		// 		glm::vec3 toBorderOther0 = glm::vec3(0.0f);
		// 		toBorderOther0[otherIdx] = myPosition[otherIdx] + (dimensions[otherIdx] / 2) + offset[otherIdx];
		// 		toBorderOther0 = otherObj->transform->rotation * toBorderOther0;

		// 		glm::vec3 toBorderOther1 = glm::vec3(0.0f);
		// 		toBorderOther1[otherIdx] = myPosition[otherIdx] - (dimensions[otherIdx] / 2) + offset[otherIdx];
		// 		toBorderOther1 = otherObj->transform->rotation * toBorderOther1;
		// 	}
		// }

		return false;
	};
};

struct PhysicsObject {
	glm::vec3 velocity = {0, 0, 0}; // uses blender convention, so z is up!
	glm::vec3 gravity = {0, 0, 0};
	float mass = 1;

	float lateralSpeed() {
		return std::sqrtf((velocity.x * velocity.x) + (velocity.y * velocity.y));
	};
};

// TODO: Squetchear Functions
struct SquetchearAnimator {
	// TODO: Squash-Stretch-Shear ("Squetchear") animator for Tireler.
	// the animator puts rotation to (0, 0, 0), with z axis as up
	// we apply z axis, then x axis, then y.
	// then we apply squash or stretch
	// and then we shear
	
	glm::quat anim_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 anim_scale = {1, 1, 1}; // 1 is the base, <1 is squash, >1 is stretch
	glm::vec2 shear = {1.5, 1.5}; // TODO: figure out
};

/*************************
 * Specific game entities
 *************************/

// TODO: Collisions and animations
struct Player {
	// to initialize
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"player", {1, 2, 2}, {0, 0, 0}};
	PhysicsObject *physicsObject = new PhysicsObject{{0, 0, 0}, {0, 0, -9.81f}, 1};
	SquetchearAnimator *animator = nullptr; // TODO

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
	float TURN_SPEED = 8 * std::numbers::pi_v<float>; // radians per second. Scales with current lateral
	float TOP_BASE_SPEED_LATERAL = 10; // units per second^2
	float GROUND_ACCEL = 10; // units per second^2, applied while accelerating
	float AIR_ACCEL = 1; // units per second^2, applied while accelerating
	float FRICTION_DECEL = 5; // applied on the ground while not accelerating
	float JUMP_STRENGTH = 10; // initial jump speed, units per second

	// brake boost
	float BRAKE_DECEL = 8;
	float CHARGE_TIME = 1;
	float chargeTimer = 0;
	float BOOST_POWER = 1.5f; // multiplies TOP_BASE_SPEED while boosting
	float BOOST_TIME = 2;
	float boostTimer = 0;

	// spring interactions
	ColliderBox *lastSpring;
	
	// transform pre-animation
	glm::vec3 game_logic_position = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::quat game_logic_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 game_logic_scale = glm::vec3(1.0f, 1.0f, 1.0f);
	glm::vec3 game_logic_transform_forward() {
		// std::cout << game_logic_rotation * glm::vec3(0.0f, 1.0f, 0.0f) << std::endl;
		return game_logic_rotation * glm::vec3(0.0f, 1.0f, 0.0f);
	};

	Player(GameObject *obj) {
		gameObject = obj;
	};

	void turn(float direction, float t) {
		// TODO determine which direction is positive
		float turn_mod = chargeTimer <= 0 ? (physicsObject->lateralSpeed() / TOP_BASE_SPEED_LATERAL) : 0.5f;
		float angle_delta = TURN_SPEED * direction * -1 * turn_mod;
		game_logic_rotation = glm::rotate(game_logic_rotation, angle_delta, {0.0f, 0.0f, 1.0f});
	};

	void accelerate(float t) {
		// if (physicsObject->lateralSpeed() > TOP_BASE_SPEED_LATERAL * (boostTimer > 0 ? BOOST_POWER : 1))
		// 	return false;
		
		glm::vec3 *velocity = &(physicsObject->velocity);

		*velocity += (physicsObject->gravity +  (game_logic_transform_forward() * (!airborne ? GROUND_ACCEL : AIR_ACCEL))) * t;
		accelerating = true;
	};

	bool jump() {
		if (airborne) return false;

		physicsObject->velocity.z += JUMP_STRENGTH;
		airborne = true;
		return true;
	};

	void charge_brake(float t) {
		glm::vec3 *velocity = &(physicsObject->velocity);

		// slow down and charge
		if (glm::vec2((*velocity).x, (*velocity).y) != glm::vec2(0.0f)) {
			glm::vec2 oldLatVel = {(*velocity).x, (*velocity).y};

			glm::vec2 latVelNorm = glm::normalize(glm::vec2((*velocity).x, (*velocity).y));

			(*velocity).x -= latVelNorm.x * FRICTION_DECEL;
			if ((*velocity).x)

			(*velocity).y -= latVelNorm.y * FRICTION_DECEL;
		}
		if (chargeTimer < CHARGE_TIME)
			chargeTimer += t;
	}

	bool boost() {
		glm::vec3 *velocity = &(physicsObject->velocity);
		if (chargeTimer >= CHARGE_TIME) {
			// set velocity in forward direction, including jump if in the airborne
			*velocity = game_logic_transform_forward() * TOP_BASE_SPEED_LATERAL * BOOST_POWER;
			chargeTimer = 0;
			boostTimer = 2;
			return true;
		}
		chargeTimer = 0;
		return false;
	};

	bool spring_jump (ColliderBox *springBox, float strength) { // units per second
		// TODO
		if (springBox == lastSpring) return false;
		physicsObject->velocity.z += strength;
		airborne = true;
		lastSpring = springBox;
		return true;
	};

	void update(float t) {
		glm::vec3 *velocity = &(physicsObject->velocity);
		Scene::Transform *transform = gameObject->transform;

		/******************
		 * Physics Updates
		 ******************/
		glm::vec2 latVelNorm;
		if (!accelerating && !airborne && glm::vec2((*velocity).x, (*velocity).y) != glm::vec2(0.0f)) { // decel
			latVelNorm = glm::normalize(glm::vec2((*velocity).x, (*velocity).y));
			(*velocity).x -= latVelNorm.x * FRICTION_DECEL;
			(*velocity).y -= latVelNorm.y * FRICTION_DECEL;
		}

		// clamp lateral speed
		latVelNorm = glm::normalize(glm::vec2((*velocity).x, (*velocity).y));
		float maxLatSpeed = TOP_BASE_SPEED_LATERAL * (boostTimer <= 0 ? 1 : BOOST_POWER);
		float finalLatSpeed = physicsObject->lateralSpeed();
		if (physicsObject->lateralSpeed() > maxLatSpeed) { // bring it down to top speed if exceeding it
			float speedDecay = (!airborne ? FRICTION_DECEL : AIR_ACCEL) * 4 * t;
			finalLatSpeed = std::max(maxLatSpeed, finalLatSpeed - speedDecay);
		}
		glm::vec2 newLatVel = latVelNorm * finalLatSpeed;
		(*velocity).x = newLatVel.x;
		(*velocity).y = newLatVel.y;

		// Apply gravity
		*velocity += physicsObject->gravity * t;

		// Update position based on gravity
		game_logic_position += (*velocity * t);

		/******************
		 * Collision logic
		 ******************/
		// TODO: player->medal, player->building, player->tree, player->ground

		/*********************
		 * Game Logic Updates
		 *********************/
		accelerating = false;
		if (boostTimer > 0) boostTimer = std::clamp(boostTimer - t, 0.0f, BOOST_TIME);
		if (comboTimer > 0) comboTimer = std::clamp(comboTimer - t, 0.0f, COMBO_DURATION);
		score += physicsObject->lateralSpeed() * t * SCORE_GAIN * multiplier;

		/********************
		 * Animation Updates
		 ********************/
		// TODO SquetchearAnimator
		(*transform).position = game_logic_position;
		(*transform).rotation = game_logic_rotation;
		(*transform).scale = game_logic_scale;
	};
};

// TODO: Collisions
struct Meteor {
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"meteor", {3.6f, 3.6f, 3.6f}, {0, 0, 0}};
	PhysicsObject *physicsObject = new PhysicsObject{{0, 0, -50}}; // TODO

	float DAMAGE_ON_IMPACT = 20;
	float SPEED = 50;

	Meteor(GameObject *obj = nullptr){
		gameObject = obj;
	};

	void update(float t) {
		/******************
		 * Physics updates
		 ******************/
		glm::vec3 *velocity = &(physicsObject->velocity);
		*velocity += physicsObject->gravity * t;
		(*(gameObject->transform)).position += (*velocity) * t;

		/******************
		 * Collision logic
		 ******************/
		// TODO: meteor->player, meteor->building, meteor->tree, meteor->ground
	};
};

struct Flame {
	// TODO

	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"flame", {2, 2, 2}, {0, 0, 0}};; // TODO

	/*************
	 * Base Logic
	 *************/
	float damagePerSecond = 13.0f;
	float BURN_DURATION = 3.0f;
	float burnTimer = 0.0f;

	/******************
	 * Recursion Logic
	 ******************/
	int spawnLevel = 0; // >0 means you can spawn more fire
	glm::vec3 spawnDirection;

	Flame(GameObject *obj = nullptr, int level = 0, glm::vec3 dir = glm::vec3(0.0f)) {
		gameObject = obj;
		spawnLevel = level;
		spawnDirection = dir != glm::vec3(0.0f) ? glm::normalize(dir) : glm::vec3(0.0f);
	};

	void spread() {
		if (spawnLevel > 0) {
			glm::vec3 spawnOffset = {spawnDirection.x * collider->dimensions.x * 0.5f,
									spawnDirection.y * collider->dimensions.y * 0.5f,
									spawnDirection.z * collider->dimensions.z * 0.5f};
			Scene::Transform *tf = new Scene::Transform();
			tf->name = "flame";
			tf->position = spawnOffset;
			Scene::Drawable dr = (new_drawable(burnin_meshes->lookup("Flame"), tf));
			
			Flame *childFlame = new Flame{new GameObject{tf, dr}, spawnLevel - 1, spawnDirection};
			childFlame->spread();
		}
	};

	void update(float t) {
		/******************
		 * Collision logic
		 ******************/
		// TODO: fire->player

		/*********************
		 * Game Logic Updates
		 *********************/
		if (burnTimer > 0)
			burnTimer = std::clamp(burnTimer - t, 0.0f, BURN_DURATION);
		else {
			// TODO destroy self
			free(gameObject);
			free(collider);
		}
		// TODO
	};
};

struct Medal {
	// TODO

	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"medal", {1.8f, 1.8f, 1.8f}, {0, 0, 0}};; // TODO

	/*********************
	 * Spinning Animation
	 *********************/
	float ROTATE_SPEED = std::numbers::pi_v<float> / 4; // radians per second

	Medal(GameObject *obj = nullptr) {
		gameObject = obj;
	};

	void update(float t) {
		/************
		 * Animation
		 ************/
		gameObject->transform->rotation = glm::rotate(gameObject->transform->rotation, ROTATE_SPEED * t, {0.0f, 0.0f, 1.0f});
	};
};

struct Spring {
	// TODO

	/************
	 * Animation
	 ************/
	float SHOOT_TIME = 0.1f;
	float SINK_TIME = 0.9f;
	float shootTimer = 0.0f;
	float sinkTimer = 0.0f;

	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"spring", {3, 3, 2.4f}, {0, 0, 0}}; // TODO

	Spring(GameObject *obj = nullptr) {
		gameObject = obj;
	};

	void update() {
		/************
		 * Animation
		 ************/
		// TODO
	};
};

struct Building {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"building", {4, 4, 4}, {0, 0, 0}};; // TODO

	Building(GameObject *bp = nullptr) {
		gameObject = bp;
	};
};

struct Tree {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider = new ColliderBox{"tree", {3, 3, 9}, {0, 0, 0}};; // TODO

	Tree(GameObject *obj = nullptr) {
		gameObject = obj;
	};
};

struct Ground {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderBox *collider; // TODO

	Ground(GameObject *obj = nullptr) {
		gameObject = obj;
	};
};



/***************
 * Game Objects
 ***************/
Ground *ground = new Ground();

// Buildings, Trees, and Springs (fixed in the world)
std::array<Building, 6> buildings;
std::array<Tree, 3> trees;
std::array<Spring, 9> springs;

// Medals and Meteors (procedurally generated)
Medal theMedal;
std::vector<Meteor> meteors;

// Player
Player player({nullptr});

// TODO: Figure this out lol
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

// Makes a copy of a scene, in case you want to modify it.
PlayMode::PlayMode() : scene(*burnin_scene) {
	//get pointers to leg for convenience:
	// for (auto &transform : scene.transforms) {
	// // 	if (transform.name == "Hip.FL") hip = &transform;
	// // 	else if (transform.name == "UpperLeg.FL") upper_leg = &transform;
	// // 	else if (transform.name == "LowerLeg.FL") lower_leg = &transform;
	// }
	// if (hip == nullptr) throw std::runtime_error("Hip not found.");
	// if (upper_leg == nullptr) throw std::runtime_error("Upper leg not found.");
	// if (lower_leg == nullptr) throw std::runtime_error("Lower leg not found.");

	// hip_base_rotation = hip->rotation;
	// upper_leg_base_rotation = upper_leg->rotation;
	// lower_leg_base_rotation = lower_leg->rotation;

	/*************************************
	 * Set locations for fixed objects
	 *************************************/
	// Buildings
	{
		// see og design doc for reference

		// bottom right
		Scene::Transform *tf0 = new Scene::Transform();
		tf0->name = "building0";
		tf0->position = {-12.0f, -20.0f, 4.0f};
		Scene::Drawable dr0 = new_drawable(burnin_meshes->lookup("Building"), tf0);
		buildings[0] = {new GameObject{tf0, dr0}};

		// bottom left
		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "building1";
		tf1->position = {-20.0f, -20.0f, 4.0f};
		Scene::Drawable dr1 = new_drawable(burnin_meshes->lookup("Building"), tf1);
		buildings[1] = {new GameObject{tf1, dr1}};

		// middle left down
		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "building2";
		tf2->position = {-28.0f, -4.0f, 4.0f};
		Scene::Drawable dr2 = new_drawable(burnin_meshes->lookup("Building"), tf2);
		buildings[2] = {new GameObject{tf2, dr2}};

		// middle left up
		Scene::Transform *tf3 = new Scene::Transform();
		tf3->name = "building3";
		tf3->position = {-28.0f, 4.0f, 4.0f};
		Scene::Drawable dr3 = new_drawable(burnin_meshes->lookup("Building"), tf3);
		buildings[3] = {new GameObject{tf3, dr3}};

		// top right (ground floor)
		Scene::Transform *tf4 = new Scene::Transform();
		tf4->name = "building4";
		tf4->position = {-12.0f, 4.0f, 4.0f};
		Scene::Drawable dr4 = new_drawable(burnin_meshes->lookup("Building"), tf4);
		buildings[4] = {new GameObject{tf4, dr4}};

		// top right (upper floor floor)
		Scene::Transform *tf5 = new Scene::Transform();
		tf5->name = "building5";
		tf5->position = {-12.0f, 4.0f, 12.0f};
		Scene::Drawable dr5 = new_drawable(burnin_meshes->lookup("Building"), tf5);
		buildings[5] = {new GameObject{tf5, dr5}};
	}

	// Trees

	// Springs

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
	// wobble += elapsed / 10.0f;
	// wobble -= std::floor(wobble);

	// hip->rotation = hip_base_rotation * glm::angleAxis(
	// 	glm::radians(5.0f * std::sin(wobble * 2.0f * float(M_PI))),
	// 	glm::vec3(0.0f, 1.0f, 0.0f)
	// );
	// upper_leg->rotation = upper_leg_base_rotation * glm::angleAxis(
	// 	glm::radians(7.0f * std::sin(wobble * 2.0f * 2.0f * float(M_PI))),
	// 	glm::vec3(0.0f, 0.0f, 1.0f)
	// );
	// lower_leg->rotation = lower_leg_base_rotation * glm::angleAxis(
	// 	glm::radians(10.0f * std::sin(wobble * 3.0f * 2.0f * float(M_PI))),
	// 	glm::vec3(0.0f, 0.0f, 1.0f)
	// );

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
