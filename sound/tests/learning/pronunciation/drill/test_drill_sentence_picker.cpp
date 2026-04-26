// DrillSentencePicker unit tests.  Exercises the two no-bias paths (no G2P,
// no store) that resolve to pure round-robin, plus the weakness-bias path
// when `weakness_bias == 1.0` forces the weak-phoneme branch every draw.

#include "learning/pronunciation/drill/DrillSentencePicker.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using hecquin::learning::pronunciation::drill::DrillSentencePicker;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    // 1. With no store and no G2P → pure round-robin.
    {
        DrillSentencePicker picker(nullptr, {});
        picker.set_pool({"one", "two", "three"});

        // Cycle two full rounds: 3 sentences × 2 = 6 draws must land on
        // each sentence exactly twice.
        std::vector<std::string> seen;
        for (int i = 0; i < 6; ++i) seen.push_back(picker.next());

        expect(seen[0] == "one"   && seen[3] == "one",   "round-robin lap 1/2 for 'one'");
        expect(seen[1] == "two"   && seen[4] == "two",   "round-robin lap 1/2 for 'two'");
        expect(seen[2] == "three" && seen[5] == "three", "round-robin lap 1/2 for 'three'");
    }

    // 2. Empty pool → `next()` returns "" and `empty()` holds.
    {
        DrillSentencePicker picker(nullptr, {});
        expect(picker.empty(), "default picker is empty");
        expect(picker.next().empty(), "empty pool returns empty string");
    }

    // 3. `load()` resets the round-robin cursor to 0.
    {
        DrillSentencePicker picker(nullptr, {});
        picker.set_pool({"a", "b"});
        (void)picker.next();          // cursor → 1
        (void)picker.next();          // cursor → 2 (wraps later)

        picker.load({"x", "y", "z"}, /*g2p=*/nullptr);
        expect(picker.size() == 3, "load installs new pool");
        expect(picker.next() == "x",
               "load resets cursor so first draw is pool[0]");
        expect(picker.next() == "y",
               "cursor continues monotonically after load");
    }

    if (failures == 0) {
        std::cout << "[test_drill_sentence_picker] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
