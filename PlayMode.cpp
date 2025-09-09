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
#include <cstdlib>
#include <ctime>

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
	std::list<Scene::Drawable>::iterator drawable;

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

// TODO: Animations
struct Player {
	// to initialize
	GameObject *gameObject;
	std::vector<ColliderSphere*> colliders = {new ColliderSphere{{-0.5f, -0.5f, 0}, "player", 1},
											   new ColliderSphere{{-0.5f, 0.5f, 0}, "player", 1},
											   new ColliderSphere{{0.5f, -0.5f, 0}, "player", 1},
											   new ColliderSphere{{0.5f, 0.5f, 0}, "player", 1}};
	PhysicsObject *physicsObject = new PhysicsObject{{0, 0, 0}, {0, 0, -9.81f}, 1};
	SquetchearAnimator *animator = nullptr; // TODO
	std::list<Scene::Drawable>::iterator drop_shadow;

	/*******************
	 * Game Rules Logic
	 *******************/
	float score = 0;
	float SCORE_GAIN = 100; // 100 points per second of driving
	float multiplier = 1;
	float MULTIPLIER_GAIN = 1; // adds this to multiplier every time a medal is collected
	float MAX_MULTIPLIER = 8;
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
	bool jumping = false;
	// timers are also used as state components

	// basic movement
	float TURN_SPEED = std::numbers::pi_v<float>; // radians per second. Scales with current lateral
	float TOP_BASE_SPEED_LATERAL = 10; // units per second^2
	float GROUND_ACCEL = 20; // units per second^2, applied while accelerating
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
	// glm::vec3 game_logic_position = glm::vec3(2.0f, -4.0f, 5.0f);
	// glm::quat game_logic_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	// glm::vec3 game_logic_scale = glm::vec3(1.0f, 1.0f, 1.0f);
	// glm::vec3 game_logic_transform_forward() {
	// 	// std::cout << game_logic_rotation * glm::vec3(0.0f, 1.0f, 0.0f) << std::endl;
	// 	return game_logic_rotation * glm::vec3(0.0f, 1.0f, 0.0f);
	// };

	Player(GameObject *obj) {
		gameObject = obj;
		for (ColliderSphere* collider : colliders)
			collider->obj = obj;
	};

	void turn(float direction, float t) {
		// TODO determine which direction is positive
		float turn_mod = chargeTimer <= 0 ? 0.5f + (0.5f * (physicsObject->lateralSpeed() / TOP_BASE_SPEED_LATERAL)) : 0.5f;
		float angle_delta = TURN_SPEED * direction * -1 * turn_mod * t;
		gameObject->transform->rotation = glm::rotate(gameObject->transform->rotation, angle_delta, {0.0f, 0.0f, 1.0f});
	};

	void accelerate(float t) {
		// if (physicsObject->lateralSpeed() > TOP_BASE_SPEED_LATERAL * (boostTimer > 0 ? BOOST_POWER : 1))
		// 	return false;
		
		glm::vec3 *velocity = &(physicsObject->velocity);

		*velocity += (physicsObject->gravity +  (gameObject->transform_forward() * (!airborne ? GROUND_ACCEL : AIR_ACCEL))) * t;
		accelerating = true;
	};

	bool jump() {
		if (airborne) return false;

		physicsObject->velocity.z += JUMP_STRENGTH;
		airborne = true;
		jumping = true;
		return true;
	};

	void charge_brake(float t) {
		boostTimer = 0;
		glm::vec3 *velocity = &(physicsObject->velocity);

		// slow down and charge
		if (glm::vec2((*velocity).x, (*velocity).y) != glm::vec2(0.0f)) {
			glm::vec2 oldLatVel = {(*velocity).x, (*velocity).y};

			glm::vec2 latVelNorm = latVelNorm != glm::vec2(0.0f, 0.0f) ? glm::normalize(glm::vec2((*velocity).x, (*velocity).y))
																	   : glm::vec2(0.0f, 0.0f);

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
			*velocity = gameObject->transform_forward() * TOP_BASE_SPEED_LATERAL * BOOST_POWER;
			chargeTimer = 0;
			boostTimer = 2;
			return true;
		}
		chargeTimer = 0;
		return false;
	};

	bool spring_jump(ColliderSphere *springBox, float strength) { // units per second
		if (springBox == lastSpring) return false;
		physicsObject->velocity.z += strength;
		airborne = true;
		lastSpring = springBox;
		jumping = true;
		return true;
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders, PlayMode *pm) {
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
			if (glm::vec2((*velocity).x, (*velocity).y) != glm::vec2(0.0f, 0.0f)) {
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
			}

			// Apply gravity
			*velocity += physicsObject->gravity * t;

			// Update position based on gravity
			transform->position += (*velocity * t);
		}

		float highestLanding = GROUND_LEVEL;

		/******************
		 * Collision logic
		 ******************/
		// player->building, player->tree, player->medal, player->ground, player->flame, player->meteor
		{
			for (size_t i = 0; i < otherColliders.size(); i++) {
				ColliderSphere *other = otherColliders[i];
				std::string otherTag = other->collider_tag;
				if (otherTag == "building" || otherTag == "tree") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							glm::vec3 motion = ((other->obj->transform->position + other->offset) -
												(gameObject->transform->position + collider->offset));
							float magnitude = std::sqrtf((motion.x * motion.x) + (motion.y * motion.y) + (motion.z * motion.z));
							float pushback = collider->radius + other->radius - magnitude;
							gameObject->transform->position -= glm::normalize(motion) * pushback;

							if (gameObject->transform->position.z > other->obj->transform->position.z + other->offset.z + other->radius) {
								airborne = true;
								lastSpring = nullptr;
							}
						}
					}
					glm::vec2 lateralAway = glm::vec2(other->obj->transform->position.x + other->offset.x - gameObject->transform->position.x,
													  other->obj->transform->position.y + other->offset.y - gameObject->transform->position.y);
					if (sqrtf((lateralAway.x * lateralAway.x) + (lateralAway.y + lateralAway.y)) < other->radius) {
						float potentialHigh = other->obj->transform->position.z + other->offset.z + other->radius;
						if (potentialHigh > highestLanding)
							highestLanding = potentialHigh;
					}
				}
				else if (otherTag == "medal") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							multiplier += MULTIPLIER_GAIN;
							comboTimer = COMBO_DURATION;
						}
					}
				}
				else if (otherTag == "meteor") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							health -= METEOR_DAMAGE;
							break;
						}
					}
				}
				else if (otherTag == "flame") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							health -= FLAME_DPS * t;
							break;
						}
					}
				}
				else if (otherTag == "spring") {
					for (ColliderSphere *collider : colliders) {
						if (collider->collider_test(other)) {
							spring_jump(other, 20.0f);
							break;
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
			
			if (!jumping && transform->position.z <= GROUND_LEVEL + 1) {
				if (transform->position.x >= four_corners[0].x && transform->position.y <= four_corners[0].y &&
					transform->position.x <= four_corners[1].x && transform->position.y <= four_corners[1].y &&
					transform->position.x >= four_corners[2].x && transform->position.y >= four_corners[2].y &&
					transform->position.x <= four_corners[3].x && transform->position.y >= four_corners[3].y) {
						transform->position = {transform->position.x, transform->position.y, GROUND_LEVEL + 1};
						(*velocity).z = 0.0f;
						airborne = false;
						lastSpring = nullptr;
				}
				else {
					airborne = true;
					if (transform->position.z < -20) {
						Mode::set_current(std::make_shared< PlayMode >());
					}
				}
			}
			jumping = false;
			(*drop_shadow).transform->position = gameObject->transform->position;
			(*drop_shadow).transform->position.z = highestLanding;

			/********************
			 * Animation Updates
			 ********************/
			// TODO SquetchearAnimator
			// (*transform).position = game_logic_position;
			// std::cout << "(" << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << ")" << std::endl;
			// (*transform).rotation = game_logic_rotation;
			// (*transform).scale = game_logic_scale;
		};
	};
};

struct Flame {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "flame", 1};

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
			std::list<Scene::Drawable>::iterator dr = pm->new_drawable(burnin_meshes->lookup("Flame"), tf, pm);
			
			Flame *childFlame = new Flame(new GameObject{tf, dr}, spawnLevel - 1, spawnDirection);
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
			// destroy self
			pm->scene.drawables.erase(gameObject->drawable);
			free(gameObject);
			free(collider);
		}
	};
};
std::list<Flame*> flames;

struct Meteor {
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "meteor", 1.8f};
	PhysicsObject *physicsObject = new PhysicsObject{{0, 0, -20.0f}};
	std::list<Scene::Drawable>::iterator drop_shadow;
	bool exploded = false;

	float SPEED = -20;

	Meteor(GameObject *obj = nullptr){
		gameObject = obj;
		collider->obj = obj;
	};

	void create_flames(PlayMode *pm) {
		Scene::Transform *tf0 = new Scene::Transform();
		tf0->name = "flame";
		tf0->position = gameObject->transform->position;
		std::list<Scene::Drawable>::iterator dr0 = pm->new_drawable(burnin_meshes->lookup("Flame"), tf0, pm);
		Flame *upFlame = new Flame(new GameObject{tf0, dr0}, 8, glm::vec3(0.0f, 1.0f, 0.0f));
		flames.emplace_back(upFlame);

		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "flame";
		tf1->position = gameObject->transform->position;
		std::list<Scene::Drawable>::iterator dr1 = pm->new_drawable(burnin_meshes->lookup("Flame"), tf1, pm);
		Flame *rightFlame = new Flame(new GameObject{tf1, dr1}, 8, glm::vec3(1.0f, 0.0f, 0.0f));
		flames.emplace_back(rightFlame);

		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "flame";
		tf2->position = gameObject->transform->position;
		std::list<Scene::Drawable>::iterator dr2 = pm->new_drawable(burnin_meshes->lookup("Flame"), tf2, pm);
		Flame *downFlame = new Flame(new GameObject{tf2, dr2}, 8, glm::vec3(0.0f, -1.0f, 0.0f));
		flames.emplace_back(downFlame);

		Scene::Transform *tf3 = new Scene::Transform();
		tf3->name = "flame";
		tf3->position = gameObject->transform->position;
		std::list<Scene::Drawable>::iterator dr3 = pm->new_drawable(burnin_meshes->lookup("Flame"), tf3, pm);
		Flame *leftFlame = new Flame(new GameObject{tf3, dr3}, 8, glm::vec3(-1.0f, 0.0f, 0.0f));
		flames.emplace_back(leftFlame);

		upFlame->spread(pm);
		rightFlame->spread(pm);
		leftFlame->spread(pm);
		downFlame->spread(pm);
	};

	bool update(float t, std::vector<ColliderSphere*> otherColliders, PlayMode *pm) {
		/******************
		 * Physics updates
		 ******************/
		glm::vec3 *velocity = &(physicsObject->velocity);
		gameObject->transform->position += (*velocity) * t;

		/******************
		 * Collision logic
		 ******************/
		if (gameObject->transform->position.z <= GROUND_LEVEL + collider->radius) {
			// create flames
			create_flames(pm);
			pm->scene.drawables.erase(drop_shadow);
			pm->scene.drawables.erase(gameObject->drawable);
			return true;

			// free(gameObject->transform);
			// free(gameObject);
			// free(physicsObject);
		}

		// meteor->building, meteor->tree, meteor->ground
		for (size_t i = 0; i < otherColliders.size(); i++) {
			ColliderSphere *other = otherColliders[i];
			std::string otherTag = other->collider_tag;
			if (otherTag == "building" || otherTag == "tree" || otherTag == "player") {
				if (collider->collider_test(other)) {
					// create flames, destroy self, remove from meteors
					create_flames(pm);
					pm->scene.drawables.erase(drop_shadow);
					pm->scene.drawables.erase(gameObject->drawable);
					// pm->scene.drawables.erase(gameObject->drawable);
					// free(gameObject->transform);
					// free(gameObject);
					// free(physicsObject);
					return true;
				}
			}
		}

		return false;
	};
};

std::array<glm::vec3, 6> medalSpawnPositions = {glm::vec3(0, 4, 1.4f), glm::vec3(0, -4, 1.4f),
												glm::vec3(-16, -12, 9.4f), glm::vec3(-16, -8, 17.4f),
												glm::vec3(8, -16, 1.4f), glm::vec3(24, 20, 1.4f)};

struct Medal {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "medal", 0.9f};
	int currentIdx = 0;
	std::list<Scene::Drawable>::iterator drop_shadow;

	/*********************
	 * Spinning Animation
	 *********************/
	float ROTATE_SPEED = std::numbers::pi_v<float> / 4; // radians per second

	Medal(GameObject *obj = nullptr) {
		gameObject = obj;
		collider->obj = obj;
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders) {
		/*************
		 * Collisions
		 *************/
		for (size_t i = 0; i < otherColliders.size(); i++) {
			ColliderSphere *other = otherColliders[i];
			std::string otherTag = other->collider_tag;
			if (otherTag == "player") {
				if (collider->collider_test(other)) {
					// move somewhere else
					// index generation courtesy of
					// https://www.w3schools.com/cpp/cpp_howto_random_number.asp
					int newIdx;
					do {
						std::srand((unsigned int)time(0));
						newIdx = std::rand() % 6;
					} while (newIdx == currentIdx);
					currentIdx = newIdx;
					// currentIdx = ++currentIdx % 6;
					gameObject->transform->position = medalSpawnPositions[currentIdx];
					(*drop_shadow).transform->position = gameObject->transform->position;
					(*drop_shadow).transform->position.z = gameObject->transform->position.z - 1.3f;
				}
			}
		}

		/************
		 * Animation
		 ************/
		gameObject->transform->rotation = glm::rotate(gameObject->transform->rotation, ROTATE_SPEED * t, {0.0f, 0.0f, 1.0f});
	};
};

struct Spring {
	/**********
	 * Structs
	 **********/
	GameObject *gameObject;
	ColliderSphere *collider = new ColliderSphere{{0, 0, 0}, "spring", 2};

	/************
	 * Animation
	 ************/
	enum SpringState {
		resting = 0b001,
		shooting = 0b010,
		sinking = 0b100
	} springState = resting;
	float SHOOT_TIME = 0.1f;
	float SINK_TIME = 0.9f;
	float shootTimer = 0.0f;
	float sinkTimer = 0.0f;

	Spring(GameObject *obj = nullptr) {
		gameObject = obj;
		collider->obj = obj;
	};

	void update(float t, std::vector<ColliderSphere*> otherColliders) {
		for (size_t i = 0; i < otherColliders.size(); i++) {
			ColliderSphere *other = otherColliders[i];
			std::string otherTag = other->collider_tag;
			if (otherTag == "player") {
				if (collider->collider_test(other) && springState == resting) {
					shootTimer = SHOOT_TIME;
					springState = shooting;
				}
			}
		}

		/**********************
		 * Logic and Animation
		 * TODO: Animation
		 **********************/
		switch (springState) {
			case shooting:
				if (shootTimer <= 0) {
					springState = sinking;
					sinkTimer = SINK_TIME;
				}
				else
					shootTimer = std::clamp(shootTimer - t, 0.0f, SHOOT_TIME);
				break;
			case sinking:
				if (sinkTimer <= 0) springState = resting;
				else sinkTimer = std::clamp(sinkTimer - t, 0.0f, SINK_TIME);
				break;
			default:
				break;
		}
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
					glm::vec3 sphere_offset = glm::vec3(((x-4) * 1.0f) + 0.5f,
														((y-4) * 1.0f) + 0.5f,
														((z-4) * 1.0f) + 0.5f);
					colliders[z][y].emplace_back(new ColliderSphere{sphere_offset, "building", 0.5f, obj});
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
	std::list<Scene::Drawable>::iterator drop_shadow;


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

struct MeteorSpawner {
	/************
	 * Timer Data
	 ************/
	float SPAWN_TIME = 30;
	float SPAWN_HEIGHT = 30;
	float spawnTimer = 0;

	void update(float t, std::list<Meteor*> &meteor_list, PlayMode *pm) {
		if (spawnTimer <= 0) {
			Scene::Transform *tf = new Scene::Transform();
			tf->name = "meteor";

			// 0-1 float generator from:
			// https://stackoverflow.com/questions/686353/random-float-number-generation

			tf->position = glm::vec3(-30.0f + (62.0f * ((float)std::rand()) / ((float)RAND_MAX)),
									 -30.0f + (62.0f * ((float)std::rand()) / ((float)RAND_MAX)), SPAWN_HEIGHT);
			// tf->rotation = glm::quat(1.0f, std::numbers::pi_v<float> / 4.0f, 0.0f, 0.0f);
			std::list<Scene::Drawable>::iterator dr = pm->new_drawable(burnin_meshes->lookup("Meteor"), tf, pm);
			Meteor *meteor = new Meteor(new GameObject{tf, dr});
			meteor->gameObject->drawable = dr;
			
			Scene::Transform *tf_ds = new Scene::Transform();
			tf_ds->name = "meteor_shadow";
			tf_ds->position = {tf->position.x, tf->position.y, 0.1f};
			tf_ds->scale = {4.0f, 4.0f, 1.0f};
			std::list<Scene::Drawable>::iterator dr_ds = pm->new_drawable(burnin_meshes->lookup("Shadow"), tf_ds, pm);
			meteor->drop_shadow = dr_ds;

			meteor_list.emplace_back(meteor);
			spawnTimer += SPAWN_TIME;
		}

		spawnTimer -= t;
	}
};
MeteorSpawner *meteorSpawner = new MeteorSpawner{};


/***************
 * Game Objects
 ***************/
Ground *ground = new Ground();

// Buildings, Trees, and Springs (fixed in the world)
std::array<Building*, 6> buildings;
std::array<Tree*, 3> trees;
std::array<Spring*, 7> springs;

// Medals and Meteors (procedurally generated)
Medal *theMedal;
std::list<Meteor*> meteors;

// Player
Player *player;

std::vector<ColliderSphere*> allColliders;

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

	/*********************************************
	 * Set locations for player and fixed objects
	 *********************************************/
	// Player
	{
		Scene::Transform *tf = new Scene::Transform();
		tf->name = "player";
		tf->position = glm::vec3(-2.0f, -2.0f, 2.0f);
		std::list<Scene::Drawable>::iterator dr = new_drawable(burnin_meshes->lookup("Tireler"), tf, this);

		Scene::Transform *tf_ds = new Scene::Transform();
		tf_ds->name = "medal_shadow";
		tf_ds->position = glm::vec3(-2.0f, -2.0f, -2.0f);
		tf_ds->scale = {2.0f, 2.0f, 1.0f};
		std::list<Scene::Drawable>::iterator dr_ds = new_drawable(burnin_meshes->lookup("Shadow"), tf_ds, this);

		player = new Player(new GameObject{tf, dr});
		player->drop_shadow = dr_ds;
	}

	// Medal
	{
		Scene::Transform *tf = new Scene::Transform();
		tf->name = "medal";
		tf->position = {0.0f, 0.0f, 1.4f};
		std::list<Scene::Drawable>::iterator dr = new_drawable(burnin_meshes->lookup("Medal"), tf, this);

		Scene::Transform *tf_ds = new Scene::Transform();
		tf_ds->name = "medal_shadow";
		tf_ds->position = {0.0f, 0.0f, 0.1f};
		tf_ds->scale = {2.0f, 2.0f, 1.0f};
		std::list<Scene::Drawable>::iterator dr_ds = new_drawable(burnin_meshes->lookup("Shadow"), tf_ds, this);

		theMedal = new Medal(new GameObject{tf, dr});
		theMedal->drop_shadow = dr_ds;
	}

	// Ground
	{
		Scene::Transform *tf = new Scene::Transform();
		tf->name = "ground";
		tf->position = {0.0f, 0.0f, -1.0f};
		std::list<Scene::Drawable>::iterator dr = new_drawable(burnin_meshes->lookup("Ground"), tf, this);
		ground->gameObject = new GameObject{tf, dr};
	}

	// Buildings
	{
		// see og design doc for reference

		// bottom right
		Scene::Transform *tf0 = new Scene::Transform();
		tf0->name = "building0";
		tf0->position = {-12.0f, -12.0f, 4.0f};
		std::list<Scene::Drawable>::iterator dr0 = new_drawable(burnin_meshes->lookup("Building"), tf0, this);
		buildings[0] = new Building{new GameObject{tf0, dr0}};

		// bottom left
		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "building1";
		tf1->position = {-20.0f, -12.0f, 4.0f};
		std::list<Scene::Drawable>::iterator dr1 = new_drawable(burnin_meshes->lookup("Building"), tf1, this);
		buildings[1] = new Building{new GameObject{tf1, dr1}};

		// middle left down
		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "building2";
		tf2->position = {-28.0f, 8.0f, 4.0f};
		std::list<Scene::Drawable>::iterator dr2 = new_drawable(burnin_meshes->lookup("Building"), tf2, this);
		buildings[2] = new Building{new GameObject{tf2, dr2}};

		// middle left up
		Scene::Transform *tf3 = new Scene::Transform();
		tf3->name = "building3";
		tf3->position = {-28.0f, 12.0f, 4.0f};
		std::list<Scene::Drawable>::iterator dr3 = new_drawable(burnin_meshes->lookup("Building"), tf3, this);
		buildings[3] = new Building{new GameObject{tf3, dr3}};

		// top right (ground floor)
		Scene::Transform *tf4 = new Scene::Transform();
		tf4->name = "building4";
		tf4->position = {-12.0f, 20.0f, 4.0f};
		std::list<Scene::Drawable>::iterator dr4 = new_drawable(burnin_meshes->lookup("Building"), tf4, this);
		buildings[4] = new Building{new GameObject{tf4, dr4}};

		// top right (upper floor)
		Scene::Transform *tf5 = new Scene::Transform();
		tf5->name = "building5";
		tf5->position = {-12.0f, 20.0f, 12.0f};
		std::list<Scene::Drawable>::iterator dr5 = new_drawable(burnin_meshes->lookup("Building"), tf5, this);
		buildings[5] = new Building{new GameObject{tf5, dr5}};
	}

	// Trees
	{
		Scene::Transform *tf0 = new Scene::Transform();
		tf0->name = "tree0";
		tf0->position = {12.0f, 20.0f, 4.5f};
		std::list<Scene::Drawable>::iterator dr0 = new_drawable(burnin_meshes->lookup("Tree"), tf0, this);

		Scene::Transform *tf0_ds = new Scene::Transform();
		tf0_ds->name = "tree_shadow";
		tf0_ds->position = {12.0f, 20.0f, 0.1f};
		tf0_ds->scale = {3.0f, 3.0f, 1.0f};
		std::list<Scene::Drawable>::iterator dr0_ds = new_drawable(burnin_meshes->lookup("Shadow"), tf0_ds, this);
		trees[0] = new Tree{new GameObject{tf0, dr0}};
		trees[0]->drop_shadow = dr0_ds;

		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "tree1";
		tf1->position = {20.0f, -20.0f, 4.5f};
		std::list<Scene::Drawable>::iterator dr1 = new_drawable(burnin_meshes->lookup("Tree"), tf1, this);

		Scene::Transform *tf1_ds = new Scene::Transform();
		tf1_ds->name = "tree_shadow";
		tf1_ds->position = {20.0f, -20.0f, 0.1f};
		tf1_ds->scale = {3.0f, 3.0f, 1.0f};
		std::list<Scene::Drawable>::iterator dr1_ds = new_drawable(burnin_meshes->lookup("Shadow"), tf1_ds, this);
		trees[1] = new Tree{new GameObject{tf1, dr1}};
		trees[1]->drop_shadow = dr1_ds;

		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "tree2";
		tf2->position = {8.0f, -8.0f, 4.5f};
		std::list<Scene::Drawable>::iterator dr2 = new_drawable(burnin_meshes->lookup("Tree"), tf2, this);

		Scene::Transform *tf2_ds = new Scene::Transform();
		tf2_ds->name = "tree_shadow";
		tf2_ds->position = {8.0f, -8.0f, 0.1f};
		tf2_ds->scale = {3.0f, 3.0f, 1.0f};
		std::list<Scene::Drawable>::iterator dr2_ds = new_drawable(burnin_meshes->lookup("Shadow"), tf2_ds, this);
		trees[2] = new Tree{new GameObject{tf2, dr2}};
		trees[2]->drop_shadow = dr2_ds;
	}

	// Springs
	{
		// City:
		// Bottom right
		Scene::Transform *tf0 = new Scene::Transform();
		tf0->name = "spring0";
		tf0->position = {-12.0f, -20.0f, -0.5f};
		std::list<Scene::Drawable>::iterator dr0 = new_drawable(burnin_meshes->lookup("Spring"), tf0, this);
		springs[0] = new Spring{new GameObject{tf0, dr0}};

		// Bottom left
		Scene::Transform *tf1 = new Scene::Transform();
		tf1->name = "spring1";
		tf1->position = {-20.0f, -12.0f, 7.5f};
		std::list<Scene::Drawable>::iterator dr1 = new_drawable(burnin_meshes->lookup("Spring"), tf1, this);
		springs[1] = new Spring{new GameObject{tf1, dr1}};

		// middle left
		Scene::Transform *tf2 = new Scene::Transform();
		tf2->name = "spring1";
		tf2->position = {-26.0f, 12.0f, 7.5f};
		std::list<Scene::Drawable>::iterator dr2 = new_drawable(burnin_meshes->lookup("Spring"), tf2, this);
		springs[2] = new Spring{new GameObject{tf2, dr2}};

		// middle right
		Scene::Transform *tf3 = new Scene::Transform();
		tf3->name = "spring1";
		tf3->position = {-12.0f, 18.0f, 15.5f};
		std::list<Scene::Drawable>::iterator dr3 = new_drawable(burnin_meshes->lookup("Spring"), tf3, this);
		springs[3] = new Spring{new GameObject{tf3, dr3}};

		// top
		Scene::Transform *tf4 = new Scene::Transform();
		tf4->name = "spring1";
		tf4->position = {-20.0f, 28.0f, -0.5f};
		std::list<Scene::Drawable>::iterator dr4 = new_drawable(burnin_meshes->lookup("Spring"), tf4, this);
		springs[4] = new Spring{new GameObject{tf4, dr4}};

		// Forest
		// Bottom
		Scene::Transform *tf5 = new Scene::Transform();
		tf5->name = "spring1";
		tf5->position = {12.0f, -20.0f, -0.5f};
		std::list<Scene::Drawable>::iterator dr5 = new_drawable(burnin_meshes->lookup("Spring"), tf5, this);
		springs[5] = new Spring{new GameObject{tf5, dr5}};

		// Bottom
		Scene::Transform *tf6 = new Scene::Transform();
		tf6->name = "spring1";
		tf6->position = {20.0f, 4.0f, -0.5f};
		std::list<Scene::Drawable>::iterator dr6 = new_drawable(burnin_meshes->lookup("Spring"), tf6, this);
		springs[6] = new Spring{new GameObject{tf6, dr6}};
	}

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
}

std::list<Scene::Drawable>::iterator PlayMode::new_drawable(Mesh const &mesh, Scene::Transform *tf, PlayMode *pm) {
	pm->scene.drawables.emplace_back(tf);
	Scene::Drawable &drawable = pm->scene.drawables.back();
	// Scene::Drawable &drawable = *(pm->scene.drawables.end());
	drawable.pipeline = lit_color_texture_program_pipeline;

	drawable.pipeline.vao = burning_meshes_for_lit_color_texture_program;
	drawable.pipeline.type = mesh.type;
	drawable.pipeline.start = mesh.start;
	drawable.pipeline.count = mesh.count;
	drawable.transform = tf;

	std::list<Scene::Drawable>::iterator ret = pm->scene.drawables.begin();
	for (int i = 0; i < pm->scene.drawables.size() - 1; i++) {
		ret++;
	}

	return ret;
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
		} else if (evt.key.key == SDLK_J) {
			jBtn.downs += 1;
			jBtn.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			space.downs += 1;
			space.pressed = true;
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
		} else if (evt.key.key == SDLK_J) {
			jBtn.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			space.pressed = false;
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
	// {
	// 	// combine inputs into a move:
	// 	constexpr float PlayerSpeed = 30.0f;
	// 	glm::vec2 move = glm::vec2(0.0f);
	// 	if (left.pressed && !right.pressed) move.x =-1.0f;
	// 	if (!left.pressed && right.pressed) move.x = 1.0f;
	// 	if (down.pressed && !up.pressed) move.y =-1.0f;
	// 	if (!down.pressed && up.pressed) move.y = 1.0f;

	// 	//make it so that moving diagonally doesn't go faster:
	// 	if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

	// 	glm::mat4x3 frame = camera->transform->make_parent_from_local();
	// 	glm::vec3 frame_right = frame[0];
	// 	//glm::vec3 up = frame[1];
	// 	glm::vec3 frame_forward = -frame[2];

	// 	camera->transform->position += move.x * frame_right + move.y * frame_forward;
	// }

	// meteor spawn manager
	{
		// meteorSpawner->update(elapsed, meteors, this);
	}

	// // player movement
	{
		if (space.pressed) player->jump();
		if (jBtn.pressed) player->charge_brake(elapsed);
		if (left.pressed && !right.pressed) player->turn(-1, elapsed);
		if (!left.pressed && right.pressed) player->turn(1, elapsed);
		if (up.pressed && !jBtn.pressed) player->accelerate(elapsed);
	}

	// entity updates
	{
		// add colliders
		allColliders = {};

		// player
		for (ColliderSphere* collider : player->colliders)
			allColliders.emplace_back(collider);
		// medal
		allColliders.emplace_back(theMedal->collider);

		for (Building *building : buildings) {
			for (int z = 0; z < 8; z++)
				for (int y = 0; y < 8; y++)
					for (int x = 0; x < 8; x++) {
						allColliders.emplace_back(building->colliders[z][y][x]);
					}
		}
		for (Tree *tree : trees) {
			for (ColliderSphere* collider : tree->colliders)
				allColliders.emplace_back(collider);
		}
		for (Spring *spring : springs) {
			allColliders.emplace_back(spring->collider);
		}
		for (Meteor *meteor : meteors) {
			allColliders.emplace_back(meteor->collider);
		}

		player->update(elapsed, allColliders, this);
		theMedal->update(elapsed, player->colliders);

		for (Spring *spring : springs) {
			spring->update(elapsed, player->colliders);
		}
		// for (auto iter = meteors.cbegin(); iter != meteors.cend(); iter++) {
		// 	if ((*iter)->update(elapsed, allColliders, this)) {
		// 		auto destroyed = iter;
		// 		iter--;
		// 		meteors.erase(destroyed);
		// 	}
		// }
		// for (Flame *flame : flames) {
		// 	flame->update(elapsed, allColliders, this);
		// }
	}


	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
	space.downs = 0;
	jBtn.downs = 0;
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

	glClearColor(0.31372549f, 0.784313725f, 1.0f, 1.0f);
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
		lines.draw_text("W to accelerate, A/D to turn, Space to Jump",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("W to accelerate, A/D to turn, Space to Jump",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
}
