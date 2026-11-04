#include "backlands_assets.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace backlands {
	namespace {
		using Json = nlohmann::json;
		uint32_t integer(const Json& value, uint32_t min, uint32_t max, const char* label) {
			if (!value.is_number_integer() || value.get<int64_t>() < min || value.get<uint64_t>() > max) {
				throw std::runtime_error(std::string("Invalid ") + label);
			}
			return value.get<uint32_t>();
		}
		Json read(const std::filesystem::path& path) {
			std::ifstream file(path);
			if (!file) {
				throw std::runtime_error("Cannot open JSON assets file");
			}
			Json result;
			file >> result;
			return result;
		}
		void png(const Sheet& sheet) {
			std::ifstream file(sheet.image, std::ios::binary);
			std::array<unsigned char, 24> bytes {};
			const std::array<unsigned char, 8> signature { 137, 80, 78, 71, 13, 10, 26, 10 };
			if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) || !std::equal(signature.begin(), signature.end(), bytes.begin())) {
				throw std::runtime_error("Invalid PNG sprite sheet");
			}
			const auto big = [&](int at) { return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3]; };
			if (big(16) != uint32_t(sheet.columns * 16) || big(20) != uint32_t(sheet.rows * 16)) {
				throw std::runtime_error("PNG dimensions differ from declared sprite sheet grid");
			}
		}
		void things(const Catalog& catalog, const Json& entries, bool items) {
			if (!entries.is_array()) {
				throw std::runtime_error("Missing thing category array");
			}
			std::set<uint32_t> ids;
			for (const auto& thing : entries) {
				const auto id = integer(thing.at("id"), items ? 100 : 1, 65535, "thing ID");
				if (!ids.insert(id).second || !thing.at("name").is_string()) {
					throw std::runtime_error("Duplicate ID or invalid thing name");
				}
				if (thing.contains("flags")) {
					if (!thing["flags"].is_object()) {
						throw std::runtime_error("Invalid flags object");
					}
					for (const auto& flag : thing["flags"]) {
						if (!flag.is_boolean()) {
							throw std::runtime_error("Invalid boolean flag");
						}
					}
				}
				for (const auto* key : { "collision", "walkable" }) {
					if (thing.contains(key)) {
						if (!thing[key].is_array() || thing[key].size() != 16) {
							throw std::runtime_error("Invalid collision mask");
						}
						for (const auto& cell : thing[key]) {
							integer(cell, 0, 1, "collision cell");
						}
					}
				}
				if (thing.contains("properties")) {
					const auto& properties = thing["properties"];
					if (!properties.is_object()) {
						throw std::runtime_error("Invalid properties");
					}
					for (const auto* key : { "groundSpeed", "minimapColor", "elevation", "alwaysOnTopOrder" }) {
						if (properties.contains(key)) {
							integer(properties[key], 0, std::string(key) == "minimapColor" ? 255 : 65535, key);
						}
					}
					for (const auto* key : { "displacement", "light" }) {
						if (properties.contains(key)) {
							if (!properties[key].is_object()) {
								throw std::runtime_error("Invalid property object");
							}
							for (const auto& value : properties[key]) {
								integer(value, 0, 65535, key);
							}
						}
					}
				}
				const auto& groups = thing.at("frameGroups");
				if (!groups.is_array() || groups.empty()) {
					throw std::runtime_error("Thing requires frame groups");
				}
				std::set<std::string> names;
				for (const auto& group : groups) {
					if (!group.at("name").is_string() || !names.insert(group["name"].get<std::string>()).second) {
						throw std::runtime_error("Duplicate frame group");
					}
					uint64_t count = 1;
					for (const auto* field : { "width", "height", "layers", "patternX", "patternY", "patternZ", "phases" }) {
						count *= integer(group.at(field), 1, std::string(field) == "phases" ? 255 : 32, field);
					}
					if (count > 1048576 || !group.at("sprites").is_array() || group["sprites"].size() != count) {
						throw std::runtime_error("Sprite count differs from frame group dimensions");
					}
					const auto& durations = group.at("durations");
					if (!durations.is_array() || durations.size() != group["phases"].get<size_t>()) {
						throw std::runtime_error("Invalid frame durations");
					}
					for (const auto& ms : durations) {
						integer(ms, 1, 60000, "frame duration");
					}
					for (const auto& value : group["sprites"]) {
						const auto sprite = integer(value, 0, std::numeric_limits<uint32_t>::max(), "sprite ID");
						if (sprite && !catalog.sheet(sprite)) {
							throw std::runtime_error("Missing sprite reference");
						}
					}
				}
			}
		}
	} // namespace

	std::filesystem::path assetPath(const std::filesystem::path& root, const std::string& relative) {
		const auto input = std::filesystem::u8path(relative);
		if (relative.empty() || input.is_absolute() || input.has_root_name() || relative.find('\\') != std::string::npos) {
			throw std::runtime_error("Asset path must be relative");
		}
		for (const auto& part : input) {
			if (part == "..") {
				throw std::runtime_error("Asset path escapes assets root");
			}
		}
		const auto base = std::filesystem::canonical(root), target = std::filesystem::canonical(base / input);
		auto a = base.begin(), b = target.begin();
		for (; a != base.end() && b != target.end() && *a == *b; ++a, ++b) { }
		if (a != base.end() || b == target.end()) {
			throw std::runtime_error("Asset path escapes assets root");
		}
		return target;
	}
	const Sheet* Catalog::sheet(uint32_t id) const {
		for (const auto& value : sheets) {
			if (id >= value.firstId && uint64_t(id) < uint64_t(value.firstId) + value.columns * value.rows) {
				return &value;
			}
		}
		return nullptr;
	}
	Assets Assets::load(const std::filesystem::path& directory) {
		Assets result;
		result.root = std::filesystem::canonical(directory);
		const auto manifest = read(result.root / "assets.json");
		result.files.push_back(result.root / "assets.json");
		if (manifest.at("format") != "backlands.assets" || manifest.at("version") != 1 || manifest.at("tileSize") != 16 || !manifest.at("catalogs").is_array()) {
			throw std::runtime_error("Expected Backlands JSON assets v1, tile size 16");
		}
		result.map = assetPath(result.root, manifest.at("map").get<std::string>());
		if (manifest.contains("configuration")) {
			result.files.push_back(assetPath(result.root, manifest.at("configuration").get<std::string>()));
		}
		std::set<std::string> namespaces;
		std::set<uint32_t> itemIds, outfitIds;
		uint64_t spriteOffset = 0;
		for (const auto& entry : manifest["catalogs"]) {
			Catalog catalog;
			catalog.name = entry.at("namespace").get<std::string>();
			if (catalog.name.empty() || !namespaces.insert(catalog.name).second) {
				throw std::runtime_error("Duplicate asset namespace");
			}
			catalog.outfitOffset = integer(entry.value("outfitIdOffset", Json(0)), 0, 65535, "outfit offset");
			catalog.spriteOffset = static_cast<uint32_t>(spriteOffset);
			const auto file = assetPath(result.root, entry.at("file").get<std::string>());
			result.files.push_back(file);
			const auto data = read(file);
			if (data.at("format") != "backlands.things" || data.at("version") != 1 || data.at("spriteSize") != 16 || !data.at("sheets").is_array()) {
				throw std::runtime_error("Expected Backlands JSON things v1");
			}
			uint64_t end = 0;
			for (const auto& value : data["sheets"]) {
				Sheet sheet;
				sheet.firstId = integer(value.at("firstId"), 1, 0xffffffff, "sheet first ID");
				sheet.columns = integer(value.at("columns"), 1, 512, "sheet columns");
				sheet.rows = integer(value.at("rows"), 1, 512, "sheet rows");
				if (sheet.firstId <= end || uint64_t(sheet.firstId) + sheet.columns * sheet.rows > 0x100000000ull) {
					throw std::runtime_error("Overlapping sprite ranges");
				}
				end = uint64_t(sheet.firstId) + sheet.columns * sheet.rows - 1;
				const auto image = value.at("image").get<std::string>();
				auto extension = std::filesystem::u8path(image).extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
				if (extension != ".png") {
					throw std::runtime_error("Sheets must be PNG");
				}
				sheet.image = assetPath(result.root, image);
				result.files.push_back(sheet.image);
				png(sheet);
				catalog.sheets.push_back(std::move(sheet));
			}
			for (const auto* kind : { "items", "outfits", "effects", "missiles" }) {
				things(catalog, data.at(kind), std::string(kind) == "items");
			}
			catalog.items = data["items"];
			catalog.outfits = data["outfits"];
			catalog.effects = data["effects"];
			catalog.missiles = data["missiles"];
			for (const auto& item : catalog.items) {
				if (!itemIds.insert(item.at("id").get<uint32_t>()).second) {
					throw std::runtime_error("Duplicate item across catalogs");
				}
			}
			for (const auto& outfit : catalog.outfits) {
				const auto id = outfit.at("id").get<uint32_t>() + catalog.outfitOffset;
				if (id > 65535 || !outfitIds.insert(id).second) {
					throw std::runtime_error("Duplicate or excessive outfit ID");
				}
			}
			spriteOffset += end;
			if (spriteOffset > 0x7fffffffull) {
				throw std::runtime_error("Too many JSON sprites");
			}
			result.catalogs.push_back(std::move(catalog));
		}
		return result;
	}
} // namespace backlands
