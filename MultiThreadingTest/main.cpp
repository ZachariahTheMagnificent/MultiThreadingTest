#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include <random>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <condition_variable>
#include <future>
#include "Vector3.h"
#include "boost/lockfree/spsc_queue.hpp"
#include "Profiler.h"

using Vector = math::Vector3<float>;

//Use multi threaded version
#define MULTI_THREADED
//Use async to create tasks instead of using threads
#define USE_TASK_BASED_MULTITHREADING
//sleep worker thread only after consumption
#define SLEEP_ONLY_AFTER_CONSUMPTION

using namespace std::chrono_literals;

constexpr size_t num_planes = 1000u;
constexpr size_t num_missiles = 1000u;
constexpr size_t num_tests = 100000u;

struct Body
{
	Body ( ) = default;
	Body ( const Vector& position, const Vector& velocity ) : position { position }, velocity { velocity }
	{

	}

	Vector position;
	Vector velocity;
};

class Plane : public Body
{
public:
	Plane ( ) = default;
	Plane ( const Vector& position, const Vector& velocity ) : Body { position, velocity }
	{

	}

	void Update ( const float time )
	{
		position += velocity * time;
	}
};

class Missile : public Body
{
public:
	Missile ( const Body*const target ) : target { target }
	{

	}
	Missile ( const Vector& position, const Body*const target ) : Body { position, Vector { } }, target { target }
	{

	}

	void UpdateVelocity ( )
	{
		const auto target_position = target->position;
		const auto relative_position_to_target = target_position - position;
		const auto direction_to_target = relative_position_to_target.Normalized ( );
		velocity = direction_to_target * speed;
	}

	void UpdatePosition ( const float time )
	{
		position += velocity * time;
	}

private:
	static constexpr float speed = 12.8f;

	const Body* target;
};

class UpdatePositionThread
{
public:
	UpdatePositionThread ( )
	{
		thread_ = std::async ( std::launch::async, [ this ] ( )
		{
			while ( !is_dead_.load ( ) )
			{
				//Send a dummy missile to wake up the thread
				if ( !queue_.empty ( ) )
				{
					const float delta_time = delta_time_.load ( );
					queue_.pop ( );
					StartTask ( delta_time );
					//promised_task_.set_value ( );
				}
			}
		} );
	}
	~UpdatePositionThread ( )
	{
		is_dead_.store ( true );
		thread_.get ( );
	}

	void SetDeltaTime ( const float delta_time )
	{
		delta_time_.store ( delta_time );
		//queue_.push ( nullptr );
		if ( !queue_.push ( nullptr ) )
		{
			throw std::exception { "lel" };
		}
		//promised_task_ = std::promise<void> { };
		//finished_task_ = promised_task_.get_future ( );
	}

	void ProcessMissile ( Missile& missile )
	{
		queue_.push ( &missile );
		//if ( !queue_.push ( &missile ) )
		//{
		//	throw std::exception { "lel" };
		//}
	}

	void Consume ( )
	{
		//queue_.push ( nullptr );
		if ( !queue_.push ( nullptr ) )
		{
			throw std::exception { "lel" };
		}
	}

	void FinishTask ( )
	{
		while ( !queue_.empty ( ) )
		{
		}
		//finished_task_.get ( );
	}

private:
	void StartTask ( const float delta_time )
	{
		while ( true )
		{
			if ( !queue_.empty ( ) )
			{
				Missile*const missile_it = queue_.front ( );
				queue_.pop ( );
				if ( missile_it == nullptr )
				{
					break;
				}
				else
				{
					missile_it->UpdatePosition ( delta_time );
				}
			}
		}
	}

	std::future<void> thread_;
	//std::promise<void> promised_task_ { };
	//std::future<void> finished_task_ { };
	boost::lockfree::spsc_queue<Missile*, boost::lockfree::capacity<num_missiles + 2>> queue_;
	std::atomic<float> delta_time_ { };
	std::atomic<bool> is_dead_ { };
};

void main ( )
{
	Profiler profiler ( num_tests );
	profiler.MakeCurrent ( );

	constexpr float delta_time = 0.03f;

	std::vector<Plane> planes;
	std::vector<Missile> missiles;

#if defined MULTI_THREADED
	UpdatePositionThread update_position_thread;
#endif

	std::mt19937 rng_machine;
	const std::uniform_real_distribution<float> position_rng ( -1000.f, 1000.f );
	const std::uniform_real_distribution<float> speed_rng ( 0.f, 10.f );
	const std::uniform_int_distribution<size_t> target_rng ( 0, num_planes - 1 );

	planes.reserve ( num_planes );
	for ( size_t i = 0u; i < num_planes; ++i )
	{
		const auto x_pos = position_rng ( rng_machine );
		const auto y_pos = position_rng ( rng_machine );
		const auto z_pos = position_rng ( rng_machine );

		const auto speed = speed_rng ( rng_machine );

		const Vector position { x_pos, y_pos, z_pos };
		const Vector velocity = position.Normalized ( ) * speed;

		planes.emplace_back ( position, velocity );
	}

	missiles.reserve ( num_missiles );
	for ( size_t i = 0u; i < num_missiles; ++i )
	{
		const auto x_pos = position_rng ( rng_machine );
		const auto y_pos = position_rng ( rng_machine );
		const auto z_pos = position_rng ( rng_machine );

		const auto target_id = target_rng ( rng_machine );

		const Vector position { x_pos, y_pos, z_pos };
		const auto target = &planes [ target_id ];

		missiles.emplace_back ( position, target );
	}

	for ( size_t i = 0; i < num_tests; ++i )
	{
		Profiler::GetCurrent ( ).Start ( );

#if defined MULTI_THREADED
		update_position_thread.SetDeltaTime ( delta_time );
		for ( auto missile_it = missiles.begin ( ), end = missiles.end ( ); missile_it != end; ++missile_it )
		{
			missile_it->UpdateVelocity ( );
			update_position_thread.ProcessMissile ( *missile_it );
		}
		update_position_thread.Consume ( );
#else
		for ( auto missile_it = missiles.begin ( ), end = missiles.end ( ); missile_it != end; ++missile_it )
		{
			missile_it->UpdateVelocity ( );
		}
		for ( auto missile_it = missiles.begin ( ), end = missiles.end ( ); missile_it != end; ++missile_it )
		{
			missile_it->UpdatePosition ( delta_time );
		}
#endif

		Profiler::GetCurrent ( ).End ( );

		for ( auto plane_it = planes.begin ( ), end = planes.end ( ); plane_it != end; ++plane_it )
		{
			plane_it->Update ( delta_time );
		}

#if defined MULTI_THREADED
		update_position_thread.FinishTask ( );
#endif
	}

	auto profile = Profiler::GetCurrent ( ).Flush ( );

	std::cout << "lowest: " << profile.lowest << "ns\n";
	std::cout << "highest: " << profile.highest << "ns\n";
	std::cout << "median: " << profile.median << "ns\n";
	std::cout << "mean: " << profile.mean << "ns\n";
	std::cout << "standard deviation: " << profile.standard_deviation << "ns\n";

	system ( "pause" );
}