#pragma once

#include <cstdint>
#include <random>

namespace Impacto::Saekano {
// Reuse the standard MT19937 engine. Only the game's range adapter is custom:
// one word per call, double scaling by2^-32, truncation, exclusive upper bound.
// std::uniform_int_distribution has a different draw/rejection contract.
class GameRandom {
 public:
  bool Known() const { return known_; }
  uint32_t SeedValue() const { return seed_; }
  uint64_t Draws() const { return draws_; }
  void Seed(uint32_t seed);
  uint32_t Word();
  int32_t Range(int32_t lower, int32_t upper);

 private:
  bool known_ = false;
  uint32_t seed_ = 0;
  uint64_t draws_ = 0;
  std::mt19937 engine_;
};
}  // namespace Impacto::Saekano
