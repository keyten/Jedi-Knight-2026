// Self-authored test fixtures, never linked into a renderer.
#pragma once
#include "lightrecon/offline.h"
namespace OfflineFixtures
{
struct Fixture
{
	Rend2LightRecon::Core::Offline::Sources sources;
	std::string answer;
};
std::vector<std::string> Names();
Fixture Make(const std::string &);
} // namespace OfflineFixtures
