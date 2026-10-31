/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/String.hpp>
#include <openrct2/entity/Guest.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <vector>

using namespace OpenRCT2;

/*
 * Builds a path that is two tiles wide (rows y = 9 and y = 10, from x = 1 to x = 10) in the pathfinding test park, leading
 * to the entrance of the "PathWithGap" ride at (1, 11), and walks guests along it from the far end.
 */
class WidePathLanesTest : public testing::Test
{
public:
    static void SetUpTestSuite()
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        ASSERT_TRUE(_context->Initialise());

        GetContext()->LoadParkFromFile(TestData::GetParkPath("pathfinding-tests.sv6"));
        GameLoadInit();

        BuildWidePath();
    }

    static void TearDownTestSuite()
    {
        Config::Get().general.guestsSpreadAcrossWidePaths = true;
        _context = nullptr;
    }

    void SetUp() override
    {
        ScenarioRandSeed(0x12345678, 0x87654321);
    }

protected:
    static constexpr int32_t kPathZ = 14;
    static constexpr int32_t kFirstX = 1;
    static constexpr int32_t kLastX = 10;
    static constexpr std::array<int32_t, 2> kLaneRows = { 9, 10 };

    struct LaneCounts
    {
        int32_t tilesPerRow[2]{};
        int32_t guestsArrived{};
    };

    static void BuildWidePath()
    {
        const auto* templatePath = MapGetPathElementAt({ 1, 10, kPathZ });
        ASSERT_NE(templatePath, nullptr);

        for (int32_t x = kFirstX; x <= kLastX; x++)
        {
            for (auto y : kLaneRows)
            {
                if (MapGetPathElementAt({ x, y, kPathZ }) != nullptr)
                    continue;

                auto* pathElement = TileElementInsert<PathElement>(TileCoordsXYZ{ x, y, kPathZ }.toCoordsXYZ(), 0b1111);
                ASSERT_NE(pathElement, nullptr);
                pathElement->setClearanceZ(templatePath->getClearanceZ());
                if (templatePath->hasLegacyPathEntry())
                {
                    pathElement->setLegacyPathEntryIndex(templatePath->getLegacyPathEntryIndex());
                }
                else
                {
                    pathElement->setSurfaceEntryIndex(templatePath->getSurfaceEntryIndex());
                    pathElement->setRailingsEntryIndex(templatePath->getRailingsEntryIndex());
                }
                pathElement->setRideIndex(RideId::GetNull());
                pathElement->setAdditionStatus(255);
            }
        }

        // Connect every tile of the strip to its neighbours within the strip.
        for (int32_t x = kFirstX; x <= kLastX; x++)
        {
            for (auto y : kLaneRows)
            {
                auto* pathElement = MapGetPathElementAt({ x, y, kPathZ });
                uint8_t edges = pathElement->getEdges();
                if (x > kFirstX)
                    edges |= (1 << 0);
                if (x < kLastX)
                    edges |= (1 << 2);
                if (y == kLaneRows[0])
                    edges |= (1 << 1);
                else
                    edges |= (1 << 3);
                pathElement->setEdges(edges);
            }
        }

        // The wide flags are normally updated a few tiles at a time as the game runs.
        for (int32_t i = 0; i < 1000; i++)
            MapUpdatePathWideFlags();

        int32_t wideTiles = 0;
        for (int32_t x = kFirstX; x <= kLastX; x++)
            for (auto y : kLaneRows)
                wideTiles += MapGetPathElementAt({ x, y, kPathZ })->isWide() ? 1 : 0;
        ASSERT_GT(wideTiles, 0) << "Test layout should contain wide path tiles";
    }

    static Ride* FindRideByName(const char* name)
    {
        for (auto& ride : RideManager(getGameState()))
        {
            if (String::startsWith(ride.getName(), u8string{ name }, true))
                return &ride;
        }
        return nullptr;
    }

    static LaneCounts WalkGuests(int32_t numGuests, int32_t startRow)
    {
        LaneCounts counts{};
        auto* ride = FindRideByName("PathWithGap");
        EXPECT_NE(ride, nullptr);
        if (ride == nullptr)
            return counts;

        // Create all guests up front, so they all have different ids.
        std::vector<Guest*> guests;
        for (int32_t i = 0; i < numGuests; i++)
        {
            const TileCoordsXYZ start{ kLastX, kLaneRows[startRow], kPathZ };
            guests.push_back(Guest::generate(start.toCoordsXYZ().toTileCentre()));
        }

        for (int32_t i = 0; i < numGuests; i++)
        {
            const TileCoordsXYZ start{ kLastX, kLaneRows[startRow], kPathZ };
            auto* guest = guests[i];
            guest->outsideOfPark = false;
            guest->state = PeepState::walking;
            guest->guestHeadingToRideId = ride->id;
            guest->peepDirection = 0;
            guest->setDestination(CoordsXY{ guest->getLocation() } + CoordsDirectionDelta[0], 2);

            TileCoordsXY lastTile{ start };
            for (int32_t step = 0; step < 5000; step++)
            {
                guest->performNextAction();

                const auto tile = TileCoordsXY{ TileCoordsXYZ{ guest->nextLoc } };
                EXPECT_NE(MapGetFootpathElement(guest->nextLoc), nullptr)
                    << "Guest left the path at " << tile.x << "," << tile.y;

                if (tile != lastTile)
                {
                    lastTile = tile;
                    if (tile.x > kFirstX && tile.x < kLastX)
                    {
                        counts.tilesPerRow[0] += (tile.y == kLaneRows[0]) ? 1 : 0;
                        counts.tilesPerRow[1] += (tile.y == kLaneRows[1]) ? 1 : 0;
                    }
                }

                if (tile.x == kFirstX && tile.y == kLaneRows[1])
                {
                    counts.guestsArrived++;
                    break;
                }
            }

            PeepEntityRemove(guest);
        }
        return counts;
    }

private:
    static std::shared_ptr<IContext> _context;
};

std::shared_ptr<IContext> WidePathLanesTest::_context;

TEST_F(WidePathLanesTest, GuestsUseOneLaneWhenDisabled)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = false;
    const auto counts = WalkGuests(20, 1);

    EXPECT_EQ(counts.guestsArrived, 20);
    const auto total = counts.tilesPerRow[0] + counts.tilesPerRow[1];
    const auto busiest = std::max(counts.tilesPerRow[0], counts.tilesPerRow[1]);
    std::printf("Disabled: row 9 = %d tiles, row 10 = %d tiles\n", counts.tilesPerRow[0], counts.tilesPerRow[1]);
    EXPECT_GT(busiest * 10, total * 8) << "Expected nearly all guests to use the same lane";
}

TEST_F(WidePathLanesTest, GuestsSpreadAcrossBothLanesWhenEnabled)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;
    const auto counts = WalkGuests(20, 1);

    EXPECT_EQ(counts.guestsArrived, 20);
    const auto total = counts.tilesPerRow[0] + counts.tilesPerRow[1];
    std::printf("Enabled: row 9 = %d tiles, row 10 = %d tiles\n", counts.tilesPerRow[0], counts.tilesPerRow[1]);
    EXPECT_GT(counts.tilesPerRow[0] * 10, total * 3) << "Expected guests to use both lanes";
    EXPECT_GT(counts.tilesPerRow[1] * 10, total * 3) << "Expected guests to use both lanes";
}

TEST_F(WidePathLanesTest, AimlessGuestsStayOnPath)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;

    std::vector<Guest*> guests;
    for (int32_t i = 0; i < 20; i++)
    {
        const TileCoordsXYZ start{ kFirstX + (i % (kLastX - kFirstX + 1)), kLaneRows[i % 2], kPathZ };
        auto* guest = Guest::generate(start.toCoordsXYZ().toTileCentre());
        guest->outsideOfPark = false;
        guest->state = PeepState::walking;
        guest->peepDirection = (i % 2) ? 0 : 2;
        guest->nextLoc = start.toCoordsXYZ();
        guest->setNextFlags(0, false, false);
        guest->setDestination(guest->getLocation(), 2);
        guests.push_back(guest);
    }

    int32_t tilesEntered = 0;
    for (auto* guest : guests)
    {
        TileCoordsXY lastTile{ TileCoordsXYZ{ guest->nextLoc } };
        for (int32_t step = 0; step < 3000; step++)
        {
            guest->performNextAction();
            ASSERT_NE(MapGetFootpathElement(guest->nextLoc), nullptr)
                << "Guest " << guest->id.ToUnderlying() << " left the path at step " << step << ": " << guest->nextLoc.x / 32
                << "," << guest->nextLoc.y / 32 << "," << guest->nextLoc.z / 8 << " state " << static_cast<int>(guest->state);

            const auto tile = TileCoordsXY{ TileCoordsXYZ{ guest->nextLoc } };
            if (tile != lastTile)
            {
                lastTile = tile;
                tilesEntered++;
            }
        }
    }
    for (auto* guest : guests)
        PeepEntityRemove(guest);

    // Guests should keep walking around rather than getting stuck.
    EXPECT_GT(tilesEntered, 20 * 50);
}
