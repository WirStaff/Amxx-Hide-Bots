#pragma once

#include <cstddef>

namespace hide_bots {

void BuildSyntheticPlayerName(int playerIndex, char* buffer, std::size_t capacity);
float GetSyntheticPlayerDuration(int playerIndex);

}
