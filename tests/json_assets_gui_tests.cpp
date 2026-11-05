#include "main.h"
#include "gui.h"
#include "items.h"
#include "backlands_assets.h"
#include "materials.h"
#include "brush.h"
#include "doodad_brush.h"
#include "copybuffer.h"
#include "editor.h"
#include "multiplayer_session.h"
#include "editor_tabs.h"
#include "settings.h"
#include "palette_brushlist.h"
#include <wx/imagpng.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using Json = nlohmann::json;
Editor::Editor(CopyBuffer& buffer, std::nullptr_t) :
	actionQueue(new ActionQueue(*this)), selection(*this), copybuffer(buffer), replace_brush(nullptr) {
	map.mapVersion = { MAP_OTBM_4, static_cast<ClientVersionID>(17) };
	map.setWidth(512);
	map.setHeight(512);
	map.unnamed = true;
}
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
	wxImage::AddHandler(new wxPNGHandler());
	g_settings.setDefaults();
	auto host = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Hidden JSON brush host");
	g_gui.tabbook = new MapTabbook(host.get(), wxID_ANY);
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
		thing["floorChange"] = "eastalt";
		auto outfit = thing;
		outfit["id"] = 1;
		outfit["name"] = "NPC";
		auto moving = group;
		moving["name"] = "moving";
		outfit["frameGroups"].push_back(moving);
		Json catalog = { { "format", "backlands.things" }, { "version", 1 }, { "spriteSize", 16 }, { "sheets", Json::array({ { { "firstId", 1 }, { "image", "tile.png" }, { "columns", 2 }, { "rows", 1 } } }) }, { "items", Json::array({ thing }) }, { "outfits", Json::array({ outfit }) }, { "effects", Json::array() }, { "missiles", Json::array() } };
		for (int season = 0; season < 4; ++season) {
			auto part = thing;
			part["id"] = 101 + season;
			part["flags"] = { { "season", season } };
			part["frameGroups"][0]["width"] = 1;
			part["frameGroups"][0]["phases"] = 1;
			part["frameGroups"][0]["durations"] = { 100 };
			part["frameGroups"][0]["sprites"] = { 1 };
			catalog["items"].push_back(part);
		}
		std::ofstream(root / "items.json") << catalog;
		const Json manifest = { { "format", "backlands.assets" }, { "version", 1 }, { "tileSize", 16 }, { "map", "world.otbm" }, { "configuration", "config.json" }, { "catalogs", Json::array({ { { "namespace", "terrain" }, { "file", "items.json" }, { "outfitIdOffset", 334 } } }) } };
		std::ofstream(root / "assets.json") << manifest;
		auto assets = backlands::Assets::load(root);
		assets.brushes = Json::array({ { { "id", "aseprite:test:ff000080" }, { "name", "Trees" }, { "color", "#ff000080" }, { "items", { 100 } } } });
		wxString error;
		for (int reload = 0; reload < 2; reload++) {
			requireJson(g_gui.gfx.loadJsonAssets(assets, error));
			requireJson(g_items.loadJsonAssets(assets, error));
			g_materials.loadJsonBrushes(assets);
			const auto* category = g_materials.tilesets.at("Aseprite: Trees (#ff000080)")->getCategory(TILESET_COLLECTION);
			requireJson(category && category->size() == 1 && category->brushlist[0] == g_items[100].raw_brush);
			g_materials.loadJsonBrushes(assets);
			requireJson(g_materials.tilesets.at("Aseprite: Trees (#ff000080)")->getCategory(TILESET_COLLECTION)->size() == 1);
			const auto& item = g_items[100];
			requireJson(item.name == "Authored ground" && item.clientID == 100 && item.unpassable && item.blockMissiles && item.group == ITEM_GROUP_GROUND);
			requireJson(item.isFloorChange() && item.floorChangeEast && !item.floorChangeDown);
			auto* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(100));
			requireJson(sprite && sprite->width == 2 && sprite->height == 1 && sprite->frames == 2 && sprite->spriteList.size() == 4);
			requireJson(item.sprite == sprite);
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
		g_materials.addToTileset("Manual", 100, TILESET_COLLECTION);
		Json structure = { { "id", "aseprite:structure:00880080" }, { "name", "House" }, { "color", "#00880080" }, { "items", { 100, 101, 102, 103, 104 } }, { "type", "structure" }, { "width", 4 }, { "height", 3 }, { "anchor", { { "x", 0 }, { "y", 0 } } } };
		structure["seasonTiles"] = Json::array();
		for (int season = 0; season < 4; ++season) {
			structure["seasonTiles"].push_back(Json::array({ { { "x", 1 }, { "y", 0 }, { "z", 0 }, { "items", { 100 } } }, { { "x", 1 }, { "y", 2 }, { "z", 0 }, { "items", { 101 + season } } }, { { "x", 3 }, { "y", 2 }, { "z", 0 }, { "items", { 101 + season } } } }));
		}
		structure["tiles"] = structure["seasonTiles"][0];
		assets.brushes = Json::array({ structure });
		g_materials.loadJsonBrushes(assets);
		const auto* structureCategory = g_materials.tilesets.at("Aseprite: House (#00880080)")->getCategory(TILESET_DOODAD);
		requireJson(structureCategory && structureCategory->size() == 1);
		auto* stamp = structureCategory->brushlist[0];
		requireJson(stamp->isDoodad() && !stamp->isRaw() && stamp->oneSizeFitsAll() && !stamp->canSmear() && stamp->getMaxVariation() == 4);
		{
			BrushListBox palette(host.get(), structureCategory);
			requireJson(palette.SelectBrush(stamp) && palette.GetSelectedBrush() == stamp);
		}
		requireJson(g_items[100].raw_brush && !g_items[100].doodad_brush);
		g_gui.SelectBrushInternal(stamp);
		g_gui.SetBrushVariation(0);
		g_gui.FillDoodadPreviewBuffer();
		const auto has = [](const Tile* tile, int id) {
			return tile && ((tile->ground && tile->ground->getID() == id) || std::any_of(tile->items.begin(), tile->items.end(), [=](const auto* item) { return item->getID() == id; }));
		};
		requireJson(has(g_gui.doodad_buffer_map->getTile({ 0x8001, 0x8000, 8 }), 100));
		requireJson(has(g_gui.doodad_buffer_map->getTile({ 0x8001, 0x8002, 8 }), 101));
		requireJson(has(g_gui.doodad_buffer_map->getTile({ 0x8003, 0x8002, 8 }), 101));
		{
			CopyBuffer copyBuffer;
			Editor editor(copyBuffer, nullptr);
			const Position at(20, 30, 7);
			editor.draw(at, false);
			requireJson(has(editor.map.getTile({ 21, 30, 7 }), 100) && has(editor.map.getTile({ 21, 32, 7 }), 101) && has(editor.map.getTile({ 23, 32, 7 }), 101));
			requireJson(editor.actionQueue->undo());
			requireJson(!has(editor.map.getTile({ 21, 30, 7 }), 100) && !has(editor.map.getTile({ 21, 32, 7 }), 101) && !has(editor.map.getTile({ 23, 32, 7 }), 101));
			requireJson(editor.actionQueue->redo());
			requireJson(has(editor.map.getTile({ 21, 30, 7 }), 100) && has(editor.map.getTile({ 21, 32, 7 }), 101) && has(editor.map.getTile({ 23, 32, 7 }), 101));
			// Refuse the whole stamp, including the tile that would fit.
			editor.draw({ 510, 40, 7 }, false);
			requireJson(!has(editor.map.getTile({ 511, 40, 7 }), 100));
			requireJson(!editor.applyDoodadPositions({ { 510, 50, 7 } }, false, true));
			requireJson(!has(editor.map.getTile({ 511, 50, 7 }), 100));
			for (int season = 1; season < 4; ++season) {
				g_gui.SetBrushVariation(season);
				const int y = 60 + season * 10;
				requireJson(editor.applyDoodadPositions({ { 20, y, 7 } }, false, true));
				requireJson(has(editor.map.getTile({ 21, y, 7 }), 100));
				for (int candidate = 0; candidate < 4; ++candidate) {
					requireJson(has(editor.map.getTile({ 21, y + 2, 7 }), 101 + candidate) == (candidate == season));
				}
			}
		}
		// Reload keeps selected pointers valid and doesn't seize manual doodad ownership.
		DoodadBrush manualOwner;
		g_items[100].doodad_brush = &manualOwner;
		const auto brushCount = g_brushes.getMap().size();
		g_materials.loadJsonBrushes(assets);
		requireJson(g_materials.tilesets.at("Aseprite: House (#00880080)")->getCategory(TILESET_DOODAD)->brushlist[0] == stamp);
		requireJson(g_brushes.getMap().size() == brushCount && g_items[100].doodad_brush == &manualOwner);
		g_items[100].doodad_brush = nullptr;
		assets.brushes = Json::array();
		g_materials.loadJsonBrushes(assets);
		requireJson(!g_materials.tilesets.count("Aseprite: Trees (#ff000080)") && !g_materials.tilesets.count("Aseprite: House (#00880080)"));
		requireJson(!stamp->visibleInPalette() && !stamp->canDraw(g_gui.doodad_buffer_map.get(), { 20, 20, 7 }));
		g_gui.SelectBrushInternal(nullptr);
		requireJson(g_materials.tilesets.at("Manual")->getCategory(TILESET_COLLECTION)->brushlist[0] == g_items[100].raw_brush);
		std::cout << "JSON PNG graphics, flags, alpha, groups, structure preview, placement, seasons, bounds, undo/redo and reload passed\n";
		if (argc > 1) {
			g_materials.clear();
			g_brushes.clear();
			g_items.clear();
			g_gui.gfx.clear();
			const auto actual = backlands::Assets::load(fs::path(argv[1]));
			requireJson(g_gui.gfx.loadJsonAssets(actual, error) && g_items.loadJsonAssets(actual, error));
			g_materials.loadJsonBrushes(actual);
			for (const auto& authored : actual.brushes) {
				const auto name = "Aseprite: " + authored.at("name").get<std::string>() + " (" + authored.at("color").get<std::string>() + ")";
				const auto* category = g_materials.tilesets.at(name)->getCategory(TILESET_DOODAD);
				requireJson(category && category->size() == 1 && category->brushlist[0]->isDoodad());
				auto* brush = category->brushlist[0];
				g_gui.SelectBrushInternal(brush);
				for (int season = 0; season < 4; ++season) {
					g_gui.SetBrushVariation(season);
					g_gui.FillDoodadPreviewBuffer();
					CopyBuffer copyBuffer;
					Editor editor(copyBuffer, nullptr);
					editor.draw({ 100, 100, 7 }, false);
					for (const auto& tile : authored.at("seasonTiles")[season]) {
						const Position position(100 + tile.at("x").get<int>(), 100 + tile.at("y").get<int>(), 7);
						for (const auto& id : tile.at("items")) {
							requireJson(has(editor.map.getTile(position), id.get<int>()));
						}
					}
					requireJson(editor.actionQueue->undo() && editor.actionQueue->redo());
				}
			}
			g_gui.SelectBrushInternal(nullptr);
			std::cout << "Real game structure palette, all four seasons, native placement and undo/redo validated: " << actual.brushes.size() << " brushes\n";
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	g_materials.clear();
	g_brushes.clear();
	g_items.clear();
	g_gui.gfx.clear();
	g_gui.tabbook = nullptr;
	host.reset();
	fs::remove_all(root);
	wxEntryCleanup();
	return result;
}
