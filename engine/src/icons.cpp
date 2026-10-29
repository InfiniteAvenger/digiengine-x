#include "icons.h"

namespace vpet {
namespace {

// Same .spr format packs use, so these double as format examples.
constexpr const char* kPoop = R"(size 8 8
palette
k 1a1c2c
b 8b5a2b
l b8865b
frame
...k....
..kbk...
..kbbk..
.kblbbk.
.kbbbbk.
kbblbbbk
kbbbbbbk
.kkkkkk.
)";

constexpr const char* kHeart = R"(size 7 6
palette
g 94b0c2
r e04050
w f4f4f4
frame
.gg.gg.
grrgrrg
grwrrrg
.grrrg.
..grg..
...g...
frame
.gg.gg.
g..g..g
g.....g
.g...g.
..g.g..
...g...
frame
.gg.gg.
grrg..g
grwr..g
.grr.g.
..grg..
...g...
)";

constexpr const char* kAttention = R"(size 7 7
palette
k 1a1c2c
y ffcd75
frame
.kkkkk.
kyykyyk
kyykyyk
kyykyyk
kyyyyyk
kyykyyk
.kkkkk.
)";

constexpr const char* kSick = R"(size 7 7
palette
k 1a1c2c
w f4f4f4
r e04050
frame
.kkkkk.
kwwrwwk
kwwrwwk
krrrrrk
kwwrwwk
kwwrwwk
.kkkkk.
)";

Sprite load(const char* text) {
    Sprite s;
    std::string err;
    parse_sprite(text, s, err);  // built-in data; a failure just draws nothing
    return s;
}

// Home menu, one 9x9 frame per slot, drawn as a one-color mask:
// status, feed, train, quest, clean (broom), lights, garden, bag, cryo.
constexpr const char* kMenu = R"(size 9 9
palette
x ffffff
frame
.......x.
.......x.
....x..x.
....x..x.
.x..x..x.
.x..x..x.
.x..x..x.
.x..x..x.
xxxxxxxxx
frame
....xxxx.
...xxxxxx
...xxxxxx
...xxxxxx
....xxxx.
...xx....
..xx.....
xxx......
.x.......
frame
.........
x.......x
xx.....xx
xx.....xx
xxxxxxxxx
xx.....xx
xx.....xx
x.......x
.........
frame
.......xx
......xxx
.....xxx.
....xxx..
.x.xxx...
..xxx....
..xx.....
.x..x....
x........
frame
.......x.
......x..
.....x...
....x....
...xxx...
..xxxxx..
.xxxxxxx.
.x.x.x.x.
x.x.x.x.x
frame
...xxx...
..xxxxx..
.xxxxxxx.
.xxxxxxx.
..xxxxx..
...xxx...
.........
...xxx...
....x....
frame
.........
.xx...xx.
xxxx.xxxx
.xxx.xxx.
...x.x...
....x....
....x....
.xxxxxxx.
xxxxxxxxx
frame
...xxx...
..x...x..
.xxxxxxx.
xxxxxxxxx
xx.....xx
xx.xxx.xx
xx.....xx
xxxxxxxxx
.xxxxxxx.
frame
....x....
.x..x..x.
..x.x.x..
...xxx...
xxxxxxxxx
...xxx...
..x.x.x..
.x..x..x.
....x....
)";

constexpr const char* kKibble = R"(size 8 8
palette
k 1a1c2c
n 8b5a2b
l b86f50
b 41a6f6
frame
........
...n.n..
..nnnnn.
.nlnnlnn
kkkkkkkk
kbbbbbbk
.kbbbbk.
..kkkk..
)";

}  // namespace

const Icons& icons() {
    static const Icons i{load(kPoop), load(kHeart), load(kAttention), load(kSick), load(kMenu), load(kKibble)};
    return i;
}

}  // namespace vpet
