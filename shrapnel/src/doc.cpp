/*
 * doc.cpp - undo history.
 *
 * A map is only 23 KB, so every step keeps a whole copy of it: undo, redo and
 * jumping to any step are just picking a copy, and can never get out of step
 * with what is on screen.
 */
#include "shrapnel.h"

static const size_t MAX_STEPS = 2000;           /* about 46 MB of history */

void History::start(const uint16_t *map, const std::string &name)
{
    steps.clear();
    steps.push_back(Step{ name, std::vector<uint16_t>(map, map + ED_MAP_CELLS) });
    cur = saved = 0;
}

void History::commit(const uint16_t *map, const std::string &name)
{
    steps.resize(cur + 1);                      /* a new step drops what was undone */
    if (saved > cur) saved = -1;                /* the saved state can no longer be reached */
    steps.push_back(Step{ name, std::vector<uint16_t>(map, map + ED_MAP_CELLS) });
    cur++;
    if (steps.size() > MAX_STEPS) {             /* forget the oldest */
        steps.erase(steps.begin());
        cur--;
        saved = saved > 0 ? saved - 1 : -1;
    }
}

const uint16_t *History::go(int i)
{
    if (i < 0 || i >= (int)steps.size()) return NULL;
    cur = i;
    return steps[i].map.data();
}
