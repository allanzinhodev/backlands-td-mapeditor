#include "main.h"
#include "gui.h"
#include "items.h"
#include "backlands_assets.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using Json = nlohmann::json;
void requireJson(bool value) {
	if (!value) {
		throw std::runtime_error("JSON graphics integration failed");
	}
}
int RunMultiplayerSessionTests(int argc, char** argv) {
	if (!wxEntryStart(argc, argv)) {
		return 1;
	}
	wxLog::SetActiveTarget(new wxLogStderr());
	wxInitAllImageHandlers();
	const auto root = fs::temp_directory_path() / ("backlands-json-gui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(root);
	int result = 0;
	try {
		wxImage png(32, 16);
		png.InitAlpha();
		for (int y = 0; y < 16; y++) {
			for (int x = 0; x < 32; x++) {
				png.SetRGB(x, y, x < 16 ? 240 : 10, 30, x < 16 ? 10 : 240);
				png.SetAlpha(x, y, x == 0 ? 0 : 128);
			}
		}
		requireJson(png.SaveFile(wxString(root.wstring()) + "/tile.png", wxBITMAP_TYPE_PNG));
		std::ofstream(root / "world.otbm").put(0);
		std::ofstream(root / "config.json") << "{}";
		const Json group = { { "name", "default" }, { "width", 2 }, { "height", 1 }, { "layers", 1 }, { "patternX", 1 }, { "patternY", 1 }, { "patternZ", 1 }, { "phases", 2 }, { "durations", { 100, 240 } }, { "sprites", { 2, 1, 1, 2 } } };
		Json thing = { { "id", 100 }, { "name", "Authored ground" }, { "flags", { { "ground", true }, { "blocked", true }, { "blockProjectiles", true } } }, { "frameGroups", Json::array({ group }) } };
		auto outfit = thing;
		outfit["id"] = 1;
		outfit["name"] = "NPC";
		auto moving = group;
		moving["name"] = "moving";
		outfit["frameGroups"].push_back(moving);
		const Json catalog = { { "format", "backlands.things" }, { "version", 1 }, { "spriteSize", 16 }, { "sheets", Json::array({ { { "firstId", 1 }, { "image", "tile.png" }, { "columns", 2 }, { "rows", 1 } } }) }, { "items", Json::array({ thing }) }, { "outfits", Json::array({ outfit }) }, { "effects", Json::array() }, { "missiles", Json::array() } };
		std::ofstream(root / "items.json") << catalog;
		const Json manifest = { { "format", "backlands.assets" }, { "version", 1 }, { "tileSize", 16 }, { "map", "world.otbm" }, { "configuration", "config.json" }, { "catalogs", Json::array({ { { "namespace", "terrain" }, { "file", "items.json" }, { "outfitIdOffset", 334 } } }) } };
		std::ofstream(root / "assets.json") << manifest;
		const auto assets = backlands::Assets::load(root);
		wxString error;
		for (int reload = 0; reload < 2; reload++) {
			requireJson(g_gui.gfx.loadJsonAssets(assets, error));
			requireJson(g_items.loadJsonAssets(assets, error));
			const auto& item = g_items[100];
			requireJson(item.name == "Authored ground" && item.clientID == 100 && item.unpassable && item.blockMissiles && item.group == ITEM_GROUP_GROUND);
			auto* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(100));
			requireJson(sprite && sprite->width == 2 && sprite->height == 1 && sprite->frames == 2 && sprite->spriteList.size() == 4);
			requireJson(sprite->animator->getFrameDuration(0)->min == 100 && sprite->animator->getFrameDuration(1)->min == 240);
			std::unique_ptr<uint8_t[]> pixels(sprite->spriteList[0]->getRGBAData());
			requireJson(pixels[0] == 10 && pixels[2] == 240 && pixels[3] == 128);
			pixels.reset(sprite->spriteList[1]->getRGBAData());
			requireJson(pixels[0] == 0 && pixels[3] == 0 && pixels[7] == 128);
			requireJson(g_gui.gfx.getCreatureSprite(335) != nullptr && g_gui.gfx.getCreatureSprite(335, FRAME_GROUP_MOVING) != nullptr);
			wxBitmap icon(32, 32);
			wxMemoryDC target(icon);
			sprite->DrawTo(&target, SPRITE_SIZE_32x32, 0, 0);
			target.SelectObject(wxNullBitmap);
			requireJson(icon.ConvertToImage().GetBlue(24, 8) == 240);
		}
		std::cout << "JSON PNG graphics, flags, alpha, groups, icons and reload passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	g_items.clear();
	g_gui.gfx.clear();
	fs::remove_all(root);
	wxEntryCleanup();
	return result;
}
