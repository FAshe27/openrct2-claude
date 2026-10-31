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
 * Builds paths that are several tiles wide in the pathfinding test park, leading to the entrance of the "PathWithGap" ride
 * at (1, 11), and walks guests along them from the far end.
 */
struct WidePathLayout
{
    std::vector<int32_t> rows; // From north to south; the last row leads to the ride entrance.
    int32_t firstX;
    int32_t lastX;
};

class WidePathLanesTestBase : public testing::Test
{
public:
    void SetUp() override
    {
        ScenarioRandSeed(0x12345678, 0x87654321);
    }

protected:
    static constexpr int32_t kPathZ = 14;

    struct LaneCounts
    {
        std::vector<int32_t> tilesPerRow;
        int32_t guestsArrived{};
    };

    static void LoadPark(const WidePathLayout& layout)
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        ASSERT_TRUE(_context->Initialise());

        GetContext()->LoadParkFromFile(TestData::GetParkPath("pathfinding-tests.sv6"));
        GameLoadInit();

        BuildWidePath(layout);
    }

    static void UnloadPark()
    {
        Config::Get().general.guestsSpreadAcrossWidePaths = true;
        _context = nullptr;
    }

    static void BuildWidePath(const WidePathLayout& layout)
    {
        const auto* templatePath = MapGetPathElementAt({ 1, 10, kPathZ });
        ASSERT_NE(templatePath, nullptr);

        for (int32_t x = layout.firstX; x <= layout.lastX; x++)
        {
            for (auto y : layout.rows)
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
        for (int32_t x = layout.firstX; x <= layout.lastX; x++)
        {
            for (auto y : layout.rows)
            {
                auto* pathElement = MapGetPathElementAt({ x, y, kPathZ });
                uint8_t edges = pathElement->getEdges();
                if (x > layout.firstX)
                    edges |= (1 << 0);
                if (x < layout.lastX)
                    edges |= (1 << 2);
                if (y != layout.rows.back())
                    edges |= (1 << 1);
                if (y != layout.rows.front())
                    edges |= (1 << 3);
                pathElement->setEdges(edges);
            }
        }

        // The wide flags are normally updated a few tiles at a time as the game runs.
        for (int32_t i = 0; i < 1000; i++)
            MapUpdatePathWideFlags();

        int32_t wideTiles = 0;
        for (int32_t x = layout.firstX; x <= layout.lastX; x++)
            for (auto y : layout.rows)
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

    static LaneCounts WalkGuests(const WidePathLayout& layout, int32_t numGuests, size_t startRow)
    {
        LaneCounts counts{};
        counts.tilesPerRow.resize(layout.rows.size());
        auto* ride = FindRideByName("PathWithGap");
        EXPECT_NE(ride, nullptr);
        if (ride == nullptr)
            return counts;

        const TileCoordsXYZ start{ layout.lastX, layout.rows[startRow], kPathZ };

        // Create all guests up front, so they all have different ids.
        std::vector<Guest*> guests;
        for (int32_t i = 0; i < numGuests; i++)
            guests.push_back(Guest::generate(start.toCoordsXYZ().toTileCentre()));

        for (auto* guest : guests)
        {
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
                    if (tile.x > layout.firstX && tile.x < layout.lastX)
                    {
                        for (size_t row = 0; row < layout.rows.size(); row++)
                            counts.tilesPerRow[row] += (tile.y == layout.rows[row]) ? 1 : 0;
                    }
                }

                if (tile.x == layout.firstX && tile.y == layout.rows.back())
                {
                    counts.guestsArrived++;
                    break;
                }
            }

            PeepEntityRemove(guest);
        }
        return counts;
    }

    static void PrintCounts(const char* name, const WidePathLayout& layout, const LaneCounts& counts)
    {
        std::printf("%s:", name);
        for (size_t row = 0; row < layout.rows.size(); row++)
            std::printf(" row %d = %d tiles", layout.rows[row], counts.tilesPerRow[row]);
        std::printf("\n");
    }

    static void ExpectEveryLaneUsed(const LaneCounts& counts)
    {
        int32_t total = 0;
        for (auto n : counts.tilesPerRow)
            total += n;
        const int32_t minimumPercent = 60 / static_cast<int32_t>(counts.tilesPerRow.size());
        for (auto n : counts.tilesPerRow)
            EXPECT_GT(n * 100, total * minimumPercent) << "Expected guests to use every lane";
    }

private:
    static std::shared_ptr<IContext> _context;
};

std::shared_ptr<IContext> WidePathLanesTestBase::_context;

// Two lanes: rows y = 9 and y = 10, from x = 1 to x = 10.
static const WidePathLayout kTwoLanes = { { 9, 10 }, 1, 10 };

class WidePathLanesTest : public WidePathLanesTestBase
{
public:
    static void SetUpTestSuite()
    {
        LoadPark(kTwoLanes);
    }

    static void TearDownTestSuite()
    {
        UnloadPark();
    }
};

TEST_F(WidePathLanesTest, GuestsUseOneLaneWhenDisabled)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = false;
    const auto counts = WalkGuests(kTwoLanes, 20, 1);
    PrintCounts("Disabled", kTwoLanes, counts);

    EXPECT_EQ(counts.guestsArrived, 20);
    const auto total = counts.tilesPerRow[0] + counts.tilesPerRow[1];
    const auto busiest = std::max(counts.tilesPerRow[0], counts.tilesPerRow[1]);
    EXPECT_GT(busiest * 10, total * 8) << "Expected nearly all guests to use the same lane";
}

TEST_F(WidePathLanesTest, GuestsSpreadAcrossBothLanesWhenEnabled)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;
    const auto counts = WalkGuests(kTwoLanes, 20, 1);
    PrintCounts("Enabled", kTwoLanes, counts);

    EXPECT_EQ(counts.guestsArrived, 20);
    ExpectEveryLaneUsed(counts);
}

TEST_F(WidePathLanesTest, AimlessGuestsStayOnPath)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;
    const auto& layout = kTwoLanes;

    std::vector<Guest*> guests;
    for (int32_t i = 0; i < 20; i++)
    {
        const TileCoordsXYZ start{ layout.firstX + (i % (layout.lastX - layout.firstX + 1)), layout.rows[i % 2], kPathZ };
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

// Three lanes: rows y = 8, 9 and 10, from x = 1 to x = 7.
static const WidePathLayout kThreeLanes = { { 8, 9, 10 }, 1, 7 };

class WidePathThreeLanesTest : public WidePathLanesTestBase
{
public:
    static void SetUpTestSuite()
    {
        LoadPark(kThreeLanes);
    }

    static void TearDownTestSuite()
    {
        UnloadPark();
    }
};

TEST_F(WidePathThreeLanesTest, GuestsSpreadAcrossAllLanesWhenEnabled)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;
    const auto counts = WalkGuests(kThreeLanes, 30, 2);
    PrintCounts("Enabled, three lanes", kThreeLanes, counts);

    EXPECT_EQ(counts.guestsArrived, 30);
    ExpectEveryLaneUsed(counts);
}

TEST_F(WidePathLanesTest, GuestsKeepClearOfPathAdditions)
{
    Config::Get().general.guestsSpreadAcrossWidePaths = true;
    const auto& layout = kTwoLanes;

    // Put path additions (e.g. benches) on part of the path.
    constexpr int32_t kFirstAdditionX = 4;
    constexpr int32_t kLastAdditionX = 6;
    for (int32_t x = kFirstAdditionX; x <= kLastAdditionX; x++)
        for (auto y : layout.rows)
            MapGetPathElementAt({ x, y, kPathZ })->setAddition(1);

    auto* ride = FindRideByName("PathWithGap");
    ASSERT_NE(ride, nullptr);

    std::vector<Guest*> guests;
    for (int32_t i = 0; i < 40; i++)
    {
        const TileCoordsXYZ start{ layout.lastX, layout.rows[i % 2], kPathZ };
        guests.push_back(Guest::generate(start.toCoordsXYZ().toTileCentre()));
    }

    int32_t maxOffsetNearAdditions = 0;
    int32_t maxOffsetElsewhere = 0;
    for (auto* guest : guests)
    {
        guest->outsideOfPark = false;
        guest->state = PeepState::walking;
        guest->guestHeadingToRideId = ride->id;
        guest->peepDirection = 0;
        guest->setDestination(CoordsXY{ guest->getLocation() } + CoordsDirectionDelta[0], 2);

        for (int32_t step = 0; step < 5000; step++)
        {
            guest->performNextAction();

            // Where the guest is heading within the next tile, across their walking direction (the value that is
            // clamped), for guests walking straight along a lane.
            const auto tile = TileCoordsXY{ TileCoordsXYZ{ guest->nextLoc } };
            const auto destination = guest->getDestination();
            const auto destinationTile = TileCoordsXY{ destination };
            if (tile.y == destinationTile.y && destinationTile.x > layout.firstX && destinationTile.x < layout.lastX)
            {
                const int32_t offset = std::abs((destination.y & 31) - 16);
                const bool nearAddition = (tile.x >= kFirstAdditionX && tile.x <= kLastAdditionX)
                    || (destinationTile.x >= kFirstAdditionX && destinationTile.x <= kLastAdditionX);
                if (nearAddition)
                    maxOffsetNearAdditions = std::max(maxOffsetNearAdditions, offset);
                else
                    maxOffsetElsewhere = std::max(maxOffsetElsewhere, offset);
            }

            if (tile.x == layout.firstX && tile.y == layout.rows.back())
                break;
        }
    }
    for (auto* guest : guests)
        PeepEntityRemove(guest);
    for (int32_t x = kFirstAdditionX; x <= kLastAdditionX; x++)
        for (auto y : layout.rows)
            MapGetPathElementAt({ x, y, kPathZ })->setAddition(0);

    std::printf(
        "Furthest from the centre line: %d next to path additions, %d elsewhere\n", maxOffsetNearAdditions, maxOffsetElsewhere);
    EXPECT_LE(maxOffsetNearAdditions, 6);
    EXPECT_LE(maxOffsetElsewhere, 7);
    EXPECT_GT(maxOffsetElsewhere, 3) << "Expected guests to spread out further than the stock 3";
}
