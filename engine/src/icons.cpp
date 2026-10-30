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

// Home menu, one 9x9 frame per slot, drawn with an outline by draw_icon:
// status, feed, train, quest, clean (broom), lights, garden, bag, cryo.
constexpr const char* kMenu = R"(size 9 9
palette
w f4f4f4
g 94b0c2
s 566c86
r e04050
m b13e53
o ef7d57
y ffcd75
a d9a066
h 38b764
e 257179
c 73eff7
n 8b5a2b
l b8865b
d 5d3a1f
frame
.......hh
.......he
....yy.he
....ya.he
.oo.ya.he
.om.ya.he
.om.ya.he
.........
sssssssss
frame
....n.hh.
....nhh..
.rrr.rrr.
rrwrrrrrm
rwrrrrrrm
rrrrrrrrm
rrrrrrrmm
.rrrrrmm.
..mm.mm..
frame
.........
.g.....g.
gs.....gs
gs.....gs
gswwwwwgs
gs.....gs
gs.....gs
.s.....s.
.........
frame
.......wg
......wwg
.....wwg.
....wwg..
.y.wwg...
..ywg....
..ny.....
.n..y....
y........
frame
........n
.......n.
......n..
.....n...
..rrr....
.yyya....
yyyaa....
y.y.a....
.y.a.....
frame
...yyy...
..ywyyy..
.ywyyyya.
.yyyyyya.
.yyyyyaa.
..yyyaa..
...ggg...
...sss...
....s....
frame
......hh.
.hh..hhhh
hhhh.hhe.
.ehh.e...
...ee....
....e....
....e....
..lllll..
.nnnnnnn.
frame
...ddd...
..d...d..
.lllllll.
lllllllln
nnnnynnnd
lnnnnnnnd
lnnnnnnnd
lnnnnnnnd
.ddddddd.
frame
....c....
.c..c..c.
..c.c.c..
...cwc...
cccwwwccc
...cwc...
..c.c.c..
.c..c..c.
....c....
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

// Quest path cards, 9x9 icons drawn with draw_icon: fight, rest, forage, boss, home.
constexpr const char* kNodes = R"(size 9 9
palette
w f4f4f4
g 94b0c2
s 566c86
r e04050
m b13e53
o ef7d57
y ffcd75
a d9a066
h 38b764
e 257179
c 73eff7
n 8b5a2b
l b8865b
d 5d3a1f
frame
w.......w
.w.....w.
..w...w..
...w.w...
....g....
...w.w...
.yy...yy.
.yn...ny.
n.......n
frame
....o....
...oo....
...oyo...
..oyyo.o.
..oyyyoo.
...oyo...
.l.....n.
..ln.nl..
....n....
frame
......hh.
.hh..hhhh
hhhh.hhe.
.ehh.e...
...ee....
....e....
....e....
..lllll..
.nnnnnnn.
frame
.........
.........
y...y...y
yy.yyy.yy
yyyyyyyyy
yryywyyry
yyyyyyyya
aaaaaaaaa
.........
frame
....m....
...mrm...
..mrrrm..
.mrrrrrm.
mrrrrrrrm
.wwwwwwg.
.wwnnwwg.
.wwnnwwg.
.wwnnwwg.
)";

// Menu row icons, 8x8 solid shapes that read on dark panels: medicine, the
// battle menu's move kinds (timing, mash, sequence, dice) and items (a potion).
constexpr const char* kUi = R"(size 8 8
palette
k 1a1c2c
w f4f4f4
g 94b0c2
r e04050
m b13e53
y ffcd75
a d9a066
b 41a6f6
c 73eff7
frame
........
.rrrwww.
rwrrwwww
rrrrwwww
mrrrwwwg
.mmmggg.
........
........
frame
..rrrr..
.rrwwrr.
rrwwwwrr
rwwrrwwr
rwwrrwwr
rrwwwwrr
.rrwwrr.
..rrrr..
frame
...yyyy.
..yyyy..
.yyyy...
yyyyyyy.
...yyya.
..yyya..
.yya....
.ya.....
frame
........
bb..bb..
.bb..bb.
..cc..cc
..bb..bb
.bb..bb.
bb..bb..
........
frame
.wwwwww.
wkwwwwwg
wwwwwwwg
wwwkwwwg
wwwwwwwg
wwwwwkwg
.gggggg.
........
frame
..gggg..
...ww...
...ww...
..wbbw..
.wbbbbw.
.wbcbbw.
.wbbbbw.
..wwww..
)";

// Forever Partner badge: a small infinity sign.
constexpr const char* kForever = R"(size 9 5
palette
y ffcd75
frame
.yy...yy.
y..y.y..y
y...y...y
y..y.y..y
.yy...yy.
)";

}  // namespace

const Icons& icons() {
    static const Icons i{load(kPoop), load(kHeart),  load(kAttention), load(kSick),
                         load(kMenu), load(kKibble), load(kForever), load(kNodes), load(kUi)};
    return i;
}

}  // namespace vpet
