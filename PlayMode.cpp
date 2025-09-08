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
#include <iostream>

const float GROUND_LEVEL = 0.0f;
std::vector<glm::vec2> four_corners = {glm::vec2(-32.0f, 32.0f), glm::vec2(32.0f, 32.0f),
										  glm::vec2(-32.0f, -32.0f), glm::vec2(32.0f, -32.0f)};

// const std::array<std::array<float, 2>, 4> four_corners = {};
GLuint burning_meshes_for_lit_color_texture_program = 0;

Load< MeshBuffer > burnin_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("burnin.pnct"));
	burning_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > burnin_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("burnin.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		// Mesh const &mesh = burnin_meshes->lookup(mesh_name);

		// scene.drawables.emplace_back(transform);
		// Scene::Drawable &drawable = scene.drawables.back();

		// drawable.pipeline = lit_color_texture_program_pipeline;

		// drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
		// drawable.pipeline.type = mesh.type;
		// drawable.pipeline.start = mesh.start;
		// drawable.pipeline.count = mesh.count;
	});
});

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

/*******************************************************************************************************
 * According to this Math Stack Exchange Answer:
 * https://math.stackexchange.com/questions/2651710/simplest-way-to-determine-if-two-3d-boxes-intersect
 * 
 * Working with spheres/a sphere cluster is significantly easier than working with boxes,
 * and... I agree, so we're gonna do that!
 *******************************************************************************************************/
struct ColliderSphere {
	glm::vec3 offset; // from a gameObject
	std::string collider_tag;
	float radius;
	GameObject *obj;

	bool collider_test(ColliderSphere *other) {
		GameObject *otherObj = other->obj;
		// calculate vector from me to other, determine if magnitude is < myRadius + otherRadius
		glm::vec3 myCentroid = obj->transform->position + offset;
		glm::vec3 otherCentroid = otherObj->transform->position + other->offset;
		glm::vec3 vecToOther = otherCentroid - myCentroid;
		float magnitude = std::sqrtf((vecToOther.x * vecToOther.x) +
									 (vecToOther.y * vecToOther.y) +
									 (vecToOther.z * vecToOther.z));
		float collisionThreshold = radius + other->radius;

		return magnitude < collisionThreshold;
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
	std::vector<ColliderSphere*> colliders = {new ColliderSphere{{-0.5f, -0.5f, 0}, "player", 1},
											   new ColliderSphere{{-0.5f, 0.5f, 0}, "player", 1},
											   new ColliderSphere{{0.5f, -0.5f, 0}, "player", 1},
											   new ColliderSphere{{0.5f, 0.5f, 0}, "player", 1}};
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

	/******************
	 * Hazard values
	 ******************/
	float FLAME_DPS = 13.0f;
	float METEOR_DAMAGE = 20.0f;

	/*********************
	 * Physics Components
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
	ColliderSphere *lastSpring;
	
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
		for (ColliderSphere* collider : colliders)
			collider->obj = obj;
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

	bool spring_jump (ColliderSphere *springBox, float strength) { // units per second
		// TODO
		if (springBox == lastSpring) return false;
		physicsObject->velocity.z += strength;
		airborne = true;
		lastSpring = springBox;
		return true;
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders) {
		glm::vec3 *velocity = &(physicsObject->velocity);
		Scene::Transform *transform = gameObject->transform;

		/******************
		 * Physics Updates
		 ******************/
		{
			glm::vec2 latVelNorm;
			if (!accelerating && boostTimer <= 0 && !airborne && glm::vec2((*velocity).x, (*velocity).y) != glm::vec2(0.0f)) { // decel
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
		}

		/******************
		 * Collision logic
		 ******************/
		// TODO: player->building, player->tree, player->medal, player->ground, player->flame, player->meteor
		{
			for (size_t i = 0; i < otherColliders.size(); i++) {
				ColliderSphere *other = otherColliders[i];
				std::string otherTag = other->collider_tag;
				if (otherTag == "building" || otherTag == "tree") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							// TODO: push back
						}
					}
				}
				else if (otherTag == "medal") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							// add points, move medal
						}
					}
				}
				else if (otherTag == "medal") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							// TODO: create flame
						}
					}
				}
			}
		}
		

		/*********************
		 * Game Logic Updates
		 *********************/
		{
			accelerating = false;
			if (boostTimer > 0) boostTimer = std::clamp(boostTimer - t, 0.0f, BOOST_TIME);
			if (comboTimer > 0) comboTimer = std::clamp(comboTimer - t, 0.0f, COMBO_DURATION);
			score += physicsObject->lateralSpeed() * t * SCORE_GAIN * multiplier;
			
			if (transform->position.z <= GROUND_LEVEL + 1) {
				if (transform->position.x >= four_corners[0][0] && transform->position.y <= four_corners[0][1] &&
					transform->position.x <= four_corners[1][0] && transform->position.y <= four_corners[1][1] &&
					transform->position.x >= four_corners[2][0] && transform->position.y >= four_corners[2][1] &&
					transform->position.x <= four_corners[3][0] && transform->position.y >= four_corners[3][1]) {
						transform->position.z = GROUND_LEVEL + 1;
						(*velocity).z = 0.0f;
						airborne = false;
				}
				else
					airborne = true;
			}
		}

		/********************
		 * Animation Updates
		 ********************/
		// TODO SquetchearAnimator
		(*transform).position = game_logic_position;
		(*transform).rotation = game_logic_rotation;
		(*transform).scale = game_logic_scale;
	};
};

struct Flame {
	// TODO

	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "flame", 1}; // TODO

	/*************
	 * Base Logic
	 *************/
	float BURN_DURATION = 3.0f;
	float burnTimer = 0.0f;

	/******************
	 * Recursion Logic
	 ******************/
	int spawnLevel = 0; // >0 means you can spawn more fire
	glm::vec3 spawnDirection;

	Flame(GameObject *obj = nullptr, int level = 0, glm::vec3 dir = glm::vec3(0.0f)) {
		gameObject = obj;
		collider->obj = obj;
		spawnLevel = level;
		spawnDirection = dir != glm::vec3(0.0f) ? glm::normalize(dir) : glm::vec3(0.0f);
	};

	void spread(PlayMode *pm) {
		if (spawnLevel > 0) {
			glm::vec3 spawnOffset = {spawnDirection.x * collider->radius,
									spawnDirection.y * collider->radius,
									spawnDirection.z * collider->radius};
			Scene::Transform *tf = new Scene::Transform();
			tf->name = "flame";
			tf->position = spawnOffset;
			Scene::Drawable dr = (pm->new_drawable(burnin_meshes->lookup("Flame"), tf, pm));
			
			Flame *childFlame = new Flame{new GameObject{tf, dr}, spawnLevel - 1, spawnDirection};
			childFlame->spread(pm);
		}
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders, PlayMode *pm) {
		/*********************
		 * Game Logic Updates
		 *********************/
		if (burnTimer > 0)
			burnTimer = std::clamp(burnTimer - t, 0.0f, BURN_DURATION);
		else {
			// TODO destroy self
			// pm->scene.drawables.remove(const gameObject->drawable);
			free(gameObject);
			free(collider);
		}
		// TODO
	};
};

// TODO: Collisions
struct Meteor {
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "meteor", 1.8f};
	PhysicsObject *physicsObject = new PhysicsObject{{0, 0, -50}}; // TODO

	float SPEED = 10;

	Meteor(GameObject *obj = nullptr){
		gameObject = obj;
		collider->obj = obj;
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders) {
		/******************
		 * Physics updates
		 ******************/
		glm::vec3 *velocity = &(physicsObject->velocity);
		*velocity += glm::vec3(0.0f, 0.0f, SPEED);
		(*(gameObject->transform)).position += (*velocity) * t;

		/******************
		 * Collision logic
		 ******************/
		// TODO: meteor->building, meteor->tree, meteor->ground
		for (size_t i = 0; i < otherColliders.size(); i++) {
			ColliderSphere *other = otherColliders[i];
			std::string otherTag = other->collider_tag;
			if (other->obj->transform->position.z <= GROUND_LEVEL + other->radius ||
				otherTag == "building" || otherTag == "tree") {
				if (collider->collider_test(other)) {
					// TODO: create flame
				}
			}
			else if (otherTag == "player") {
				if (collider->collider_test(other)) {
					// TODO: create flame
				}
			}
		}
	};
};

struct Medal {
	// TODO

	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "medal", 0.9f};; // TODO

	/*********************
	 * Spinning Animation
	 *********************/
	float ROTATE_SPEED = std::numbers::pi_v<float> / 4; // radians per second

	Medal(GameObject *obj = nullptr) {
		gameObject = obj;
		collider->obj = obj;
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
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "spring", 1.5f}; // TODO

	Spring(GameObject *obj = nullptr) {
		gameObject = obj;
		collider->obj = obj;
	};

	void update(float t) {
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
	std::vector<std::vector<std::vector<ColliderSphere*>>> colliders = {}; // TODO

	Building(GameObject *obj = nullptr) {
		gameObject = obj;
		for (int z = 0; z < 8; z++) {
			colliders.emplace_back();
			for (int y = 0; y < 8; y++) {
				colliders[z].emplace_back();
				for (int x = 0; x < 8; x++) {
					glm::vec3 sphere_offset = glm::vec3(((x-4) * 2.0f) + 1.0f,
														((y-4) * 2.0f) + 1.0f,
														((z-4) * 2.0f) + 1.0f);
					colliders[z][y].emplace_back(new ColliderSphere{sphere_offset, "building", 1.0f, obj});
				}
			}
		}
	};
};

struct Tree {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	std::array<ColliderSphere*, 3> colliders = {new ColliderSphere{{0, 0, -3}, "tree", 1.5f},
												new ColliderSphere{{0, 0, 0}, "tree", 1.5f},
												new ColliderSphere{{0, 0, 3}, "tree", 1.5f}};

	Tree(GameObject *obj = nullptr) {
		gameObject = obj;
		for (ColliderSphere* collider : colliders)
			collider->obj = obj;
	};
};

struct Ground {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	// no collider: use ground_level variable

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
std::array<glm::vec3, 8> medalSpawnPositions = {glm::vec3(0, 4, 2), glm::vec3(0, -4, 2),
												glm::vec3(-28, -4, 10), glm::vec3(-12, -12, 18), glm::vec3(-24, 24, 2),
												glm::vec3(8, -16, 2), glm::vec3(24, 20, 2), glm::vec3(28, -20, 2)};
std::vector<Meteor> meteors;

// Player
Player player({nullptr});

std::vector<ColliderSphere*> allColliders;

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
		Scene::Drawable dr0 = new_drawable(burnin_meshes->lookup("Building"), tf0, this);
		buildings[0] = {new GameObject{tf0, dr0}};

		// bottom left
		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "building1";
		tf1->position = {-20.0f, -20.0f, 4.0f};
		Scene::Drawable dr1 = new_drawable(burnin_meshes->lookup("Building"), tf1, this);
		buildings[1] = {new GameObject{tf1, dr1}};

		// middle left down
		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "building2";
		tf2->position = {-28.0f, -4.0f, 4.0f};
		Scene::Drawable dr2 = new_drawable(burnin_meshes->lookup("Building"), tf2, this);
		buildings[2] = {new GameObject{tf2, dr2}};

		// middle left up
		Scene::Transform *tf3 = new Scene::Transform();
		tf3->name = "building3";
		tf3->position = {-28.0f, 4.0f, 4.0f};
		Scene::Drawable dr3 = new_drawable(burnin_meshes->lookup("Building"), tf3, this);
		buildings[3] = {new GameObject{tf3, dr3}};

		// top right (ground floor)
		Scene::Transform *tf4 = new Scene::Transform();
		tf4->name = "building4";
		tf4->position = {-12.0f, 4.0f, 4.0f};
		Scene::Drawable dr4 = new_drawable(burnin_meshes->lookup("Building"), tf4, this);
		buildings[4] = {new GameObject{tf4, dr4}};

		// top right (upper floor floor)
		Scene::Transform *tf5 = new Scene::Transform();
		tf5->name = "building5";
		tf5->position = {-12.0f, 4.0f, 12.0f};
		Scene::Drawable dr5 = new_drawable(burnin_meshes->lookup("Building"), tf5, this);
		buildings[5] = {new GameObject{tf5, dr5}};
	}

	// Trees

	// Springs

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
}

Scene::Drawable PlayMode::new_drawable(Mesh const &mesh, Scene::Transform *tf, PlayMode *pm) {
	pm->scene.drawables.emplace_back(tf);
	Scene::Drawable drawable = pm->scene.drawables.back();
	drawable.pipeline = lit_color_texture_program_pipeline;

	drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
	drawable.pipeline.type = mesh.type;
	drawable.pipeline.start = mesh.start;
	drawable.pipeline.count = mesh.count;

	return drawable;
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
			return true; }
		// } else if (evt.key.key == SDLK_J) {
		// 	jBtn.downs += 1;
		// 	jBtn.pressed = true;
		// 	return true;
		// } else if (evt.key.key == SDLK_K) {
		// 	kBtn.downs += 1;
		// 	kBtn.pressed = true;
		// 	return true;
		// } else if (evt.key.key == SDLK_SPACE) {
		// 	space.downs += 1;
		// 	space.pressed = true;
		// 	return true;
		// }
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
			return true;}
		// } else if (evt.key.key == SDLK_J) {
		// 	jBtn.pressed = false;
		// 	return true;
		// } else if (evt.key.key == SDLK_K) {
		// 	kBtn.pressed = false;
		// 	return true;
		// } else if (evt.key.key == SDLK_SPACE) {
		// 	space.pressed = false;
		// 	return true;
		// }
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

		// combine inputs into a move:
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

	// meteor spawn manager
	{
		
	}

	// // player movement
	// {
	// 	if (space.pressed) player.jump();
	// 	if (!jBtn.pressed && kBtn.pressed) player.charge_brake();
	// 	if (left.pressed && !right.pressed) player.turn(-1, elapsed);
	// 	if (!left.pressed && right.pressed) player.turn(1, elapsed);
	// 	if (jBtn.pressed && !kBtn.pressed) player.accelerate();
	// }

	// // entity updates
	// {
	// 	// add colliders
	// 	allColliders = {};
	// 	buildings;
	// 	trees;
	// 	springs;

	// 	// player
	// 	for (ColliderSphere* collider : player->colliders)
	// 		allColliders.emplace_back(collider);
	// 	// medal
	// 	allColliders.emplace_back(theMedal.collider);

	// 	for (Building building : buildings) {
	// 		for (ColliderSphere* collider : building->colliders)
	// 			allColliders.emplace_back(collider);
	// 	}
	// 	for (Tree tree : trees) {
	// 		for (ColliderSphere* collider : tree->colliders)
	// 			allColliders.emplace_back(collider);
			
	// 	}
	// 	for (Spring spring : springs) {
	// 		allColliders.emplace_back(spring.collider);
	// 	}
	// 	for (Meteor meteor : meteors) {
	// 		allColliders.emplace_back(meteor.collider);
	// 	}

	// 	player.update(elapsed, allColliders);
	// 	theMedal.update(elapsed);

	// 	for (Spring spring : springs) {
	// 		spring.update(elapsed)
	// 	}
	// 	for (Meteor meteor : meteors) {
	// 		meteor.update(elapsed, otherColliders);
	// 	}
	// }


	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
	// space.downs = 0;
	// jBtn.downs = 0;
	// kBtn.downs = 0;
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

	for (auto iter = scene.drawables.begin(); iter != scene.drawables.end(); iter++) {
		auto tf = iter->transform;
		std::cout << tf->name << " (" << tf->position.x << ", " << tf->position.y << ", " << tf->position.z << ")" << std::endl;
	}

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
