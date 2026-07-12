#pragma once

#include <chrono>
#include <random>
#include <stdexcept>


template <typename Dummy = void>
class RandomGeneratorImpl {
public:
    static std::mt19937 engine_;
    static int is_seed_set_;


    static void init_default_seed() {
        std::cout << "warning: init_default_seed" << std::endl;

        std::random_device rd;
        unsigned int seed = rd.entropy() > 0.0 ? rd() : 
            static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        engine_.seed(seed);
        is_seed_set_ = true;
    }
};


template <typename Dummy>
std::mt19937 RandomGeneratorImpl<Dummy>::engine_;

template <typename Dummy>
int RandomGeneratorImpl<Dummy>::is_seed_set_ = false;

class RandomGenerator {
private:
    using Impl = RandomGeneratorImpl<void>;
    RandomGenerator() = default;

public:

    static void set_seed(unsigned int seed) {
        Impl::engine_.seed(seed);
        Impl::is_seed_set_ = true;
    }


    static void reset_to_random_seed() {
        Impl::init_default_seed();
    }


    static int randomInt(int L, int R) {
        if (L > R) {
            throw std::invalid_argument("L must be <= R");
        }

        if (!Impl::is_seed_set_) {
            Impl::init_default_seed();
        }

        std::uniform_int_distribution<int> dist(L, R);
        return dist(Impl::engine_);
    }
};

namespace myrandom {

    inline int randomInt(int L, int R) {
        return RandomGenerator::randomInt(L, R);
    }


    inline void setSeed(unsigned int seed) {
        RandomGenerator::set_seed(seed);
    }
}
