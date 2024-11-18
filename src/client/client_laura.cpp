#include "api.h"
#include "utils.h"
#include <string>
#include <iostream>
#include <random>
#include <spdlog/spdlog.h>
#include <climits>
#include <set>
#include <utility>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <SFML/System.hpp> // for sf::vector2i
#include <queue>

/*
My strategy was to make the bot move in a square pattern, in a sort of chain-like formation as it moves along the board. 
I aslo increase the size of the square after completing a full cycle in order to try avoiding collisions with myself, and to cover 
more space to hopefully kill more bots. 
I added a trail to keep track of the bot's most recent path, this way i tried to avoid moving to positions that are already part of the recent 
trail to prevent self-collisions. The goal was to tell the bot not to build the next square in the same direction/area of a recent square.
I also added some more logging to help me debug the bot's behavior.

Where it fails sometimes: 
It still sometimes collides with itself if it starts to build a square inside of itself. When this happens, it gets trapped and is doomed:(
I tried to prioritize moving inwards towards the board once its  near the edges of the grid to avoid getting trapped, but 
sometimes it still gets stuck in a loop near the edges. 
Works best in a less crowded board, so i would also try to improve the bot's ability to navigate in a more crowded board to make it a bit better. 

I think overall its an ok defensive strategy, but i could improve it by adding some offensive ones. 
*/

using namespace cycles;

struct VectorComparator {
    bool operator()(const sf::Vector2i& lhs, const sf::Vector2i& rhs) const {
        return (lhs.x < rhs.x) || (lhs.x == rhs.x && lhs.y < rhs.y);
    }
};

class BotClient {
  Connection connection; // manages communication with the game server
  std::string name; // name of the bot
  GameState state; // holds the current game state
  Player my_player; // the bot's player data
  std::mt19937 rng{std::random_device{}()}; // random number generator for fallback decisions
  Direction currentDirection = Direction::north; // starting direction
  std::set<sf::Vector2i, VectorComparator> trail; // stores trail positions
  std::queue<sf::Vector2i> trailQueue; // tracks trail order
  const int MAX_TRAIL_LENGTH = 200; // max trail length 
  int squareSize = 1; // initial square size
  int stepsOnCurrentSide = 0; // steps on the current side
  int sidesCompleted = 0; // sides completed in the square pattern

  // checks if a move in the given direction is valid
  bool is_valid_move(Direction direction) {
    auto new_pos = my_player.position + getDirectionVector(direction);

    // ensure move is inside grid boundaries
    if (!state.isInsideGrid(new_pos)) {
        spdlog::debug("{}: move out of bounds at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // ensure target cell is unoccupied
    if (state.getGridCell(new_pos) != 0) {
        spdlog::debug("{}: cell occupied at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // avoid collisions with other players
    for (const auto &player : state.players) {
        if (player.id != my_player.id && player.position == new_pos) {
            spdlog::debug("{}: collision detected with player {} at ({}, {})", name, player.name, new_pos.x, new_pos.y);
            return false;
        }
    }

    // prevent self-collision
    if (state.getGridCell(new_pos) == my_player.id) {
        spdlog::debug("{}: self-collision detected at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // avoid risky moves near grid edges
    if (new_pos.x < 2 || new_pos.x >= state.gridWidth - 2 || new_pos.y < 2 || new_pos.y >= state.gridHeight - 2) {
        spdlog::debug("{}: move near grid edge at ({}, {}) is risky", name, new_pos.x, new_pos.y);
        return false;
    }

    return true; // move is valid
  }

  // checks for self-collision at a position
  bool is_self_collision(const sf::Vector2i &pos) {
    return trail.count(pos) > 0;
  }

  // updates the trail with a new position
  void updateTrail(const sf::Vector2i &newPos) {
    trail.insert(newPos);
    trailQueue.push(newPos);

    // remove oldest position if trail exceeds max length
    if (trailQueue.size() > MAX_TRAIL_LENGTH) {      //keeps track of only recent trail positions, so the bot doesn't avoid already visited positions forever and ever
        auto oldPos = trailQueue.front();         
        trail.erase(oldPos);
        trailQueue.pop();
    }
  }

  // decides the next move for the bot
  Direction decideMove() {
    // detect proximity to grid edges
    int dangerZone = 2; // defines danger zone near grid borders
    bool nearLeft = my_player.position.x < dangerZone;
    bool nearRight = my_player.position.x >= state.gridWidth - dangerZone;
    bool nearTop = my_player.position.y < dangerZone;
    bool nearBottom = my_player.position.y >= state.gridHeight - dangerZone;

    // prioritize inward movement near edges
    if (nearLeft || nearRight || nearTop || nearBottom) {
        spdlog::info("{}: near border, adjusting movement", name);
        if (nearRight && is_valid_move(Direction::west)) return Direction::west;
        if (nearLeft && is_valid_move(Direction::east)) return Direction::east;
        if (nearTop && is_valid_move(Direction::south)) return Direction::south;
        if (nearBottom && is_valid_move(Direction::north)) return Direction::north;
    }

    // follow square-pattern logic
    if (stepsOnCurrentSide >= squareSize) {
        switch (currentDirection) {
            case Direction::north: currentDirection = Direction::east; break;
            case Direction::east: currentDirection = Direction::south; break;
            case Direction::south: currentDirection = Direction::west; break;
            case Direction::west: currentDirection = Direction::north; break;
        }
        stepsOnCurrentSide = 0;
        sidesCompleted++;
        if (sidesCompleted == 4) {
            squareSize++;
            sidesCompleted = 0;
        }
    }

    sf::Vector2i nextPos = my_player.position + getDirectionVector(currentDirection);
    if (is_valid_move(currentDirection) && !is_self_collision(nextPos)) {
        stepsOnCurrentSide++;
        updateTrail(nextPos);
        return currentDirection;
    }

    // fallback to valid directions
    for (int i = 0; i < 4; ++i) {
        auto direction = getDirectionFromValue(i);
        sf::Vector2i fallbackPos = my_player.position + getDirectionVector(direction);
        if (is_valid_move(direction) && !is_self_collision(fallbackPos)) {
            spdlog::warn("{}: fallback to direction {}", name, static_cast<int>(direction));
            updateTrail(fallbackPos);
            return direction;
        }
    }

    spdlog::error("{}: no valid moves available!", name);
    exit(1); // terminate if no valid move exists
  }

  // receives the current game state from the server
  void receiveGameState() {
    state = connection.receiveGameState();
    for (const auto &player : state.players) {
      if (player.name == name) {
        my_player = player;
        break;
      }
    }
  }

  // sends the bot's decided move to the server
  void sendMove() {
    spdlog::debug("{}: sending move", name);
    auto move = decideMove();
    connection.sendMove(move);
  }

public:
  // constructor: initializes the bot and establishes connection
BotClient(const std::string &botName) : name(botName) {
    try {
        std::random_device rd;
        rng.seed(rd());
        connection.connect(name); // attempt to connect
        if (!connection.isActive()) {
            spdlog::critical("{}: connection failed", name);
            throw std::runtime_error("connection to server failed");
        }
        spdlog::info("{}: connected to the server", name);
    } catch (const std::exception &e) {
        spdlog::critical("{}: exception during initialization: {}", name, e.what());
        exit(1); // terminate if connection cannot be established
    }
}


  // main loop: processes game state and makes moves
  void run() {
    while (connection.isActive()) {
      receiveGameState();
      sendMove();
    }
  }
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <bot_name>" << std::endl;
    return 1;
  }

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
  spdlog::set_level(spdlog::level::debug);
#endif

  std::string botName = argv[1];
  BotClient bot(botName);
  bot.run();
  return 0;
}




















