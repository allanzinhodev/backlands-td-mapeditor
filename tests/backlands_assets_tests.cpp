#include "backlands_assets.h"
#include "server_workspace.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <source_location>
#include <algorithm>

using Json = nlohmann::json;
namespace fs = std::filesystem;
void check(bool ok, const std::source_location& location = std::source_location::current()) {
	if (!ok) {
		throw std::runtime_error("JSON asset contract failed at line " + std::to_string(location.line()));
	}
}
void write(const fs::path& path, const Json& value) {
	std::ofstream(path) << value;
}
int main(int argc, char** argv) {
	const auto root = fs::temp_directory_path() / ("backlands-json-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(root);
	try {
		std::ofstream(root / "world.otbm").put(0);
		write(root / "config.json", Json::object());
		// PNG IHDR signature/dimensions, which the independent parser checks.
		const unsigned char png[24] = { 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 16, 0, 0, 0, 16 };
		std::ofstream image(root / "tile.png", std::ios::binary);
		image.write(reinterpret_cast<const char*>(png), 24);
		image.close();
		const Json group = { { "name", "default" }, { "width", 1 }, { "height", 1 }, { "layers", 1 }, { "patternX", 1 }, { "patternY", 1 }, { "patternZ", 1 }, { "phases", 2 }, { "durations", { 100, 240 } }, { "sprites", { 1, 0 } } };
		Json catalog = { { "format", "backlands.things" }, { "version", 1 }, { "spriteSize", 16 }, { "sheets", Json::array({ { { "firstId", 1 }, { "image", "tile.png" }, { "columns", 1 }, { "rows", 1 } } }) }, { "items", Json::array({ { { "id", 100 }, { "name", "Hand authored ground" }, { "flags", { { "ground", true } } }, { "frameGroups", Json::array({ group }) } } }) }, { "outfits", Json::array() }, { "effects", Json::array() }, { "missiles", Json::array() } };
		write(root / "items.json", catalog);
		Json manifest = { { "format", "backlands.assets" }, { "version", 1 }, { "tileSize", 16 }, { "map", "world.otbm" }, { "configuration", "config.json" }, { "catalogs", Json::array({ { { "namespace", "terrain" }, { "file", "items.json" } } }) } };
		write(root / "assets.json", manifest);
		check(backlands::Assets::load(root).catalogs[0].items[0]["id"] == 100);
		catalog["items"][0]["flags"]["season"] = 0;
		catalog["items"][0]["seasonGroup"] = "test";
		write(root / "items.json", catalog);
		check(backlands::Assets::load(root).catalogs[0].items[0]["flags"]["season"] == 0);
		const auto detected = ServerResourceDetector::Detect(root);
		check(detected.validRoot && detected.workspace.hasRequiredResources());
		check(detected.workspace.itemIdMode == ItemIdMode::ServerId);
		check(!detected.workspace.hasItemsOtb() && !detected.workspace.trackedResourcesChanged());
		const Json brush = { { "id", "aseprite:fixture:ff000080" }, { "name", "Trees" }, { "color", "#ff000080" }, { "sourceId", "fixture" }, { "items", { 100 } } };
		Json brushes = { { "format", "backlands.brushes" }, { "version", 1 }, { "brushes", Json::array({ brush }) } };
		write(root / "brushes.json", brushes);
		manifest["brushes"] = "brushes.json";
		write(root / "assets.json", manifest);
		const auto withBrushes = backlands::Assets::load(root);
		check(withBrushes.brushes.size() == 1 && withBrushes.brushes[0]["items"][0] == 100);
		check(std::find(withBrushes.files.begin(), withBrushes.files.end(), fs::canonical(root / "brushes.json")) != withBrushes.files.end());
		const auto brushWorkspace = ServerResourceDetector::Detect(root);
		check(brushWorkspace.validRoot && !brushWorkspace.workspace.trackedResourcesChanged());
		for (int scenario = 0; scenario < 4; scenario++) {
			auto invalid = brushes;
			if (scenario == 0) {
				invalid["brushes"][0]["items"] = { 65535 };
			}
			if (scenario == 1) {
				invalid["brushes"][0]["items"] = { 100, 100 };
			}
			if (scenario == 2) {
				invalid["brushes"][0]["color"] = "#ff0000";
			}
			if (scenario == 3) {
				invalid["brushes"].push_back(brush);
			}
			write(root / "brushes.json", invalid);
			fs::last_write_time(root / "brushes.json", fs::file_time_type::clock::now() + std::chrono::seconds(scenario + 1));
			check(brushWorkspace.workspace.trackedResourcesChanged());
			bool rejected = false;
			try {
				backlands::Assets::load(root);
			} catch (const std::exception&) {
				rejected = true;
			}
			check(rejected);
		}
		write(root / "brushes.json", brushes);
		for (int scenario = 0; scenario < 6; scenario++) {
			auto invalid = catalog;
			if (scenario == 0) {
				invalid["items"][0]["frameGroups"][0]["sprites"][0] = 2;
			}
			if (scenario == 1) {
				invalid["items"][0]["frameGroups"][0]["width"] = 0;
			}
			if (scenario == 2) {
				invalid["items"][0]["frameGroups"][0]["durations"][0] = 0;
			}
			if (scenario == 3) {
				invalid["sheets"][0]["image"] = "../escape.png";
			}
			if (scenario == 4) {
				invalid["sheets"][0]["columns"] = 2;
			}
			if (scenario == 5) {
				invalid["items"][0]["flags"]["season"] = 4;
			}
			write(root / "items.json", invalid);
			// Deterministic metadata change even on filesystems that coalesce
			// several same-size writes into one timestamp tick.
			fs::last_write_time(root / "items.json", fs::file_time_type::clock::now() + std::chrono::seconds(scenario + 1));
			check(detected.workspace.trackedResourcesChanged());
			bool rejected = false;
			try {
				backlands::Assets::load(root);
			} catch (const std::exception&) {
				rejected = true;
			}
			check(rejected);
		}
		if (argc > 1) {
			const auto actual = backlands::Assets::load(fs::path(argv[1]));
			check(actual.catalogs.size() == 3);
			check(actual.catalogs[1].outfits.size() == 334);
			check(actual.catalogs[2].outfits.size() == 10);
			std::cout << "Real game JSON catalogs and PNG dimensions validated\n";
		}
		fs::remove_all(root);
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		fs::remove_all(root);
		return 1;
	}
	std::cout << "JSON asset contracts passed\n";
}
