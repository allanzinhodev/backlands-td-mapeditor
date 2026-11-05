#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace backlands {
	struct Sheet {
		uint32_t firstId = 0;
		int columns = 0, rows = 0;
		std::filesystem::path image;
	};
	struct Catalog {
		std::string name;
		uint32_t spriteOffset = 0;
		uint16_t outfitOffset = 0;
		std::vector<Sheet> sheets;
		nlohmann::json items, outfits, effects, missiles;
		const Sheet* sheet(uint32_t spriteId) const;
	};
	struct Assets {
		std::filesystem::path root, map;
		std::vector<Catalog> catalogs;
		nlohmann::json brushes = nlohmann::json::array();
		std::vector<std::filesystem::path> files;
		static Assets load(const std::filesystem::path& root);
	};
	// Paths in JSON are portable, relative PNG/JSON/OTBM paths below one assets root.
	std::filesystem::path assetPath(const std::filesystem::path& root, const std::string& relative);
} // namespace backlands
