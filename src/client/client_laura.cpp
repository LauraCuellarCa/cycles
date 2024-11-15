#include "api.h"
#include "utils.h"
#include <string>
#include <iostream>
#include <random>
#include <spdlog/spdlog.h>
#include <string>
#include <climits>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace cycles;

class BotClient {
  Connection connection; // Manages communication with the game server
  std::string name;      // Name of the bot
  GameState state;       // Holds the current game state
  Player my_player;      // The bot's player data
  std::mt19937 rng{std::random_device{}()}; // Random number generator for fallback decisions
  Direction currentDirection = Direction::north; // Starting direction
  int squareSize = 1;                            // Initial square side length
  int stepsOnCurrentSide = 0;                    // Steps taken on the current side
  int sidesCompleted = 0;                        // Number of sides completed in the square

  // Check whether a move in the given direction is valid
  bool is_valid_move(Direction direction) {
    auto new_pos = my_player.position + getDirectionVector(direction);

    // Ensure the move stays inside the grid boundaries
    if (!state.isInsideGrid(new_pos)) {
        spdlog::debug("{}: Move out of bounds at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // Ensure the target cell is unoccupied
    if (state.getGridCell(new_pos) != 0) {
        spdlog::debug("{}: Cell occupied at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // Avoid collisions with other players
    for (const auto &player : state.players) {
        if (player.id != my_player.id && player.position == new_pos) {
            spdlog::debug("{}: Collision detected with player {} at ({}, {})", 
                          name, player.name, new_pos.x, new_pos.y);
            return false;
        }
    }

    // Prevent the bot from running into its own trail
    if (state.getGridCell(new_pos) == my_player.id) {
        spdlog::debug("{}: Self-collision detected at ({}, {})", name, new_pos.x, new_pos.y);
        return false;
    }

    // Avoid risky areas near the grid edges
    if (new_pos.x < 2 || new_pos.x >= state.gridWidth - 2 || new_pos.y < 2 || new_pos.y >= state.gridHeight - 2) {
        spdlog::debug("{}: Move near grid edge at ({}, {}) is risky", name, new_pos.x, new_pos.y);
        return false;
    }

    return true; // Move is valid if all checks pass
  }

  // Decide the bot's next move
  Direction decideMove() {
    // If the bot completes the current side of the square
    if (stepsOnCurrentSide >= squareSize) {
      // Switch to the next direction (clockwise movement)
      switch (currentDirection) {
        case Direction::north: currentDirection = Direction::east; break;
        case Direction::east: currentDirection = Direction::south; break;
        case Direction::south: currentDirection = Direction::west; break;
        case Direction::west: currentDirection = Direction::north; break;
      }
      stepsOnCurrentSide = 0; // Reset step counter for the new side
      sidesCompleted++;

      // Expand the square after completing all four sides
      if (sidesCompleted == 4) {
        squareSize++; // Increase the square size
        sidesCompleted = 0; // Reset the side completion count
        spdlog::info("{}: Expanding square to size {}", name, squareSize);
      }
    }

    // Attempt to move in the current direction
    if (is_valid_move(currentDirection)) {
      stepsOnCurrentSide++; // Increment step counter for the current side
      return currentDirection;
    }

    // If the current direction is invalid, fallback to any valid direction
    for (int i = 0; i < 4; ++i) {
      auto direction = getDirectionFromValue(i);
      if (is_valid_move(direction)) {
        spdlog::warn("{}: Fallback to direction {}", name, static_cast<int>(direction));
        return direction;
      }
    }

    // If no valid move exists, terminate the bot with an error
    spdlog::error("{}: No valid moves available!", name);
    exit(1);
  }

  // Receive the current game state from the server
  void receiveGameState() {
    state = connection.receiveGameState();
    for (const auto &player : state.players) {
      if (player.name == name) {
        my_player = player; // Update the bot's player data
        break;
      }
    }
  }

  // Send the bot's decided move to the server
  void sendMove() {
    spdlog::debug("{}: Sending move", name);
    auto move = decideMove();
    connection.sendMove(move); // Communicate the move to the server
  }

public:
  // Constructor: Initialize the bot and establish a connection with the server
  BotClient(const std::string &botName) : name(botName) {
    std::random_device rd;
    rng.seed(rd());
    connection.connect(name);
    if (!connection.isActive()) {
      spdlog::critical("{}: Connection failed", name);
      exit(1); // Exit if the connection fails
    }
    spdlog::info("{}: Connected to the server", name);
  }

  // Main loop: Continuously process game state and make moves
  void run() {
    while (connection.isActive()) {
      receiveGameState(); // Update the game state
      sendMove();         // Decide and send the next move
    }
  }
};

int main(int argc, char *argv[]) {
  // Ensure the bot name is provided as a command-line argument
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
    return 1;
  }

  // Set logging level for debugging
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
  spdlog::set_level(spdlog::level::debug);
#endif

  std::string botName = argv[1];
  BotClient bot(botName); // Create a new bot with the provided name
  bot.run();              // Start the bot's main loop
  return 0;
}



















