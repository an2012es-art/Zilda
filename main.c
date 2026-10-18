// City Drive DS v2 - مدينة كبيرة بمنظور علوي بأسلوب GTA 2 (مشروع أصلي)
// libnds / devkitPro. كل الرسومات تُولَّد بالكود، لا حاجة لملفات خارجية.
//
// التحكم:
//   يمين/يسار = توجيه | فوق أو A = تسارع | تحت أو B = فرامل/رجوع
//   Y = النزول من السيارة / ركوبها (عند الاقتراب منها)
// اللعبة:
//   اوصل بالسيارة إلى العلامة الصفراء لتكسب 100 نقطة.
//   دهس المشاة يرفع النجوم فتطاردك الشرطة. إن لمستك وأنت بطيء = BUSTED.
//   اهرب بعيدًا عن الشرطة فتنخفض النجوم تدريجيًا.

#include <nds.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// إذا دارت السيارة عكس اتجاه التوجيه، غيّر -1 إلى 1
#define ROT_SIGN (-1)

#define TILES        128
#define WORLD        1024
#define MAX_TRAFFIC  6
#define MAX_POLICE   4
#define MAX_CARS     (MAX_TRAFFIC + MAX_POLICE)
#define MAX_PEDS     10

#define OAM_PLAYER 0
#define OAM_FOOT   1
#define OAM_MARKER 2
#define OAM_CAR0   3
#define OAM_PED0   (OAM_CAR0 + MAX_CARS)

enum { T_GRASS, T_ROAD, T_VLR, T_VLL, T_HLB, T_HLT, T_SIDE,
       T_BUILD, T_BUILD2, T_BUILD3, T_WATER, T_LOT, T_COUNT };

enum { K_DOWNTOWN, K_PARK, K_HOUSES, K_INDUSTRIAL, K_LOT };

static u8  tileType[TILES][TILES];
static u8  tileGfx[T_COUNT][64] __attribute__((aligned(4)));
static u16 mapRam[TILES * TILES] __attribute__((aligned(4)));
static u8  miniStatic[128 * 128];
static u8  backbuf[256 * 192] __attribute__((aligned(4)));

static u8 gfxPlayer[256]     __attribute__((aligned(4)));
static u8 gfxTraffic[5][256] __attribute__((aligned(4)));
static u8 gfxPolice[2][256]  __attribute__((aligned(4)));
static u8 gfxPed[4][64]      __attribute__((aligned(4)));
static u8 gfxFoot[64]        __attribute__((aligned(4)));
static u8 gfxMarker[64]      __attribute__((aligned(4)));

static u16 *pPlayer, *pFoot, *pMarker;
static u16 *pTraffic[5], *pPolice[2], *pPed[4];

static const u8 font[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};
// حروف كلمة BUSTED بالترتيب: B U S T E D
static const u8 letters[6][5] = {
    {6,5,6,5,6},{5,5,5,5,7},{3,4,2,1,6},{7,2,2,2,2},{7,4,6,4,7},{6,5,5,5,6}
};

// أهداف المهمة: تقاطعات الطرق (عمود، صف) من 0 إلى 7
static const int targetIJ[][2] = {
    {5,1},{2,3},{6,6},{0,7},{7,2},{3,5},{1,0},{4,4},{7,7},{0,2}
};
#define NUM_TARGETS ((int)(sizeof(targetIJ) / sizeof(targetIJ[0])))
#define TARGET_X(k) (64 + 128 * targetIJ[k][0])
#define TARGET_Y(k) (64 + 128 * targetIJ[k][1])

static const int DX[4] = {0, 1, 0, -1};   // 0 شمال، 1 شرق، 2 جنوب، 3 غرب
static const int DY[4] = {-1, 0, 1, 0};

// ---------------------------------------------------------------- حالة اللعبة
static int carX, carY, angle, speed;       // السيارة: 24.8 ثابتة، زاوية 0..511
static int footX, footY, inCar;
static int score, target, wanted, heat, bustedTimer, frame;
static int camX, camY;

typedef struct {
    int x, y;                // 24.8
    int dir, cruise, cur;
    int type, color, alive;
    int turnDir, turnCoord;
    int inInter, block, ghost, stun;
} Car;
static Car cars[MAX_CARS];

typedef struct { int x, y, dx, dy, timer, alive, respawn, color; } Ped;
static Ped peds[MAX_PEDS];

static u32 rngState = 2463534242u;
static int rnd(int n) {
    rngState = rngState * 1664525u + 1013904223u;
    return (int)((rngState >> 16) % (u32)n);
}

static int isRoad(int t)  { return t >= T_ROAD && t <= T_HLT; }
static int isSolid(int t) { return t >= T_BUILD && t <= T_WATER; }

// ---------------------------------------------------------------- بناء العالم

static void buildTiles(void) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int i = y * 8 + x;
            int edge = (x == 0 || y == 0 || x == 7 || y == 7);
            tileGfx[T_GRASS][i]  = (((x * 7 + y * 13) % 5) == 0) ? 2 : 1;
            tileGfx[T_ROAD][i]   = 3;
            tileGfx[T_VLR][i]    = (x == 7 && y < 5) ? 4 : 3;
            tileGfx[T_VLL][i]    = (x == 0 && y < 5) ? 4 : 3;
            tileGfx[T_HLB][i]    = (y == 7 && x < 5) ? 4 : 3;
            tileGfx[T_HLT][i]    = (y == 0 && x < 5) ? 4 : 3;
            tileGfx[T_SIDE][i]   = (x == 7 || y == 7) ? 19 : 5;
            tileGfx[T_BUILD][i]  = edge ? 7 : ((((x & 1) == 0) && ((y & 1) == 0)) ? 8 : 6);
            tileGfx[T_BUILD2][i] = edge ? 11 : ((y == 3 || y == 4) ? 12 : 10);
            tileGfx[T_BUILD3][i] = edge ? 14 : ((y == 3) ? 14 : 13);
            tileGfx[T_WATER][i]  = (((x + y * 3) % 7) == 0) ? 16 : 15;
            tileGfx[T_LOT][i]    = (x == 0) ? 18 : 17;
        }
    }
}

static int blockKind(int bx, int by) {
    if (((bx * 3 + by * 5) % 7) == 0) return K_PARK;
    if (bx >= 6) return ((bx + by) & 1) ? K_INDUSTRIAL : K_LOT;
    if (bx <= 2) return K_HOUSES;
    return K_DOWNTOWN;
}

static void buildWorld(void) {
    for (int ty = 0; ty < TILES; ty++) {
        for (int tx = 0; tx < TILES; tx++) {
            int lx = (tx + 7) & 15, ly = (ty + 7) & 15;
            int bx = (tx + 7) >> 4, by = (ty + 7) >> 4;
            int vroad = (lx >= 14) && ty >= 7 && ty <= 120;
            int hroad = (ly >= 14) && tx >= 7 && tx <= 120;
            int t;
            if (ty >= 124)                 t = T_WATER;
            else if (vroad && hroad)       t = T_ROAD;
            else if (vroad)                t = (lx == 14) ? T_VLR : T_VLL;
            else if (hroad)                t = (ly == 14) ? T_HLB : T_HLT;
            else if (lx >= 14 || ly >= 14) t = T_BUILD;
            else if (lx == 0 || lx == 13 || ly == 0 || ly == 13) t = T_SIDE;
            else {
                switch (blockKind(bx, by)) {
                case K_PARK:       t = T_GRASS; break;
                case K_HOUSES:
                    t = ((((lx - 1) & 3) < 3) && (((ly - 1) & 3) < 3)) ? T_BUILD2 : T_GRASS;
                    break;
                case K_INDUSTRIAL: t = T_BUILD3; break;
                case K_LOT:        t = T_LOT; break;
                default:           t = T_BUILD; break;
                }
            }
            tileType[ty][tx] = (u8)t;
            mapRam[ty * TILES + tx] = (u16)t;
        }
    }
}

static void paintCar(u8 *g, u8 body, u8 barL, u8 barR) {
    memset(g, 0, 256);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            u8 c = 0;
            if (x >= 4 && x <= 11 && y >= 1 && y <= 14) {
                c = body;
                if ((y == 1 || y == 14) && (x == 4 || x == 11)) c = 0;
                if ((y == 4 || y == 5) && x >= 5 && x <= 10) c = 2;
                if (y == 11 && x >= 5 && x <= 10) c = 2;
                if (y == 1 && (x == 5 || x == 10)) c = 4;
                if (barL && y >= 7 && y <= 8) {
                    if (x >= 5 && x <= 7) c = barL;
                    else if (x >= 8 && x <= 10) c = barR;
                }
            }
            if ((x == 3 || x == 12) && ((y >= 3 && y <= 5) || (y >= 10 && y <= 12))) c = 3;
            g[y * 16 + x] = c;
        }
    }
}

static void paintPed(u8 *g, u8 shirt) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int r2 = (2 * x - 7) * (2 * x - 7) + (2 * y - 7) * (2 * y - 7);
            u8 c = 0;
            if (r2 <= 36) c = shirt;
            if (r2 <= 8) c = 6;
            g[y * 8 + x] = c;
        }
    }
}

static void buildSprites(void) {
    static const u8 bodies[5] = {8, 9, 10, 11, 12};
    paintCar(gfxPlayer, 1, 0, 0);
    for (int i = 0; i < 5; i++) paintCar(gfxTraffic[i], bodies[i], 0, 0);
    paintCar(gfxPolice[0], 14, 15, 13);
    paintCar(gfxPolice[1], 14, 13, 15);
    for (int i = 0; i < 4; i++) paintPed(gfxPed[i], (u8)(16 + i));
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int r2 = (2 * x - 7) * (2 * x - 7) + (2 * y - 7) * (2 * y - 7);
            u8 f = 0, m = 0;
            if (r2 <= 49) f = 5;
            if (r2 <= 10) f = 6;
            if (r2 <= 49 && r2 >= 16) m = 7;
            gfxFoot[y * 8 + x] = f;
            gfxMarker[y * 8 + x] = m;
        }
    }
}

static void buildMinimap(void) {
    for (int ty = 0; ty < TILES; ty++) {
        for (int tx = 0; tx < TILES; tx++) {
            int t = tileType[ty][tx];
            u8 c = 1;
            if (isRoad(t) || t == T_LOT) c = 2;
            else if (t == T_SIDE) c = 3;
            else if (t == T_WATER) c = 8;
            else if (isSolid(t)) c = 4;
            miniStatic[ty * 128 + tx] = c;
        }
    }
}

// ---------------------------------------------------------------- استعلامات العالم

static int solidAt(int px, int py) {
    if (px < 0 || py < 0 || px >= WORLD || py >= WORLD) return 1;
    return isSolid(tileType[py >> 3][px >> 3]);
}

static int onGrass(int px, int py) {
    if (px < 0 || py < 0 || px >= WORLD || py >= WORLD) return 0;
    return tileType[py >> 3][px >> 3] == T_GRASS;
}

static bool walkable(int px, int py) {
    if (px < 0 || py < 0 || px >= WORLD || py >= WORLD) return false;
    int t = tileType[py >> 3][px >> 3];
    return t == T_SIDE || t == T_GRASS;
}

static int playerPX(void) { return inCar ? (carX >> 8) : footX; }
static int playerPY(void) { return inCar ? (carY >> 8) : footY; }

static bool offscreen(int x, int y, int m) {
    return x < camX - m || x > camX + 256 + m || y < camY - m || y > camY + 192 + m;
}

// ---------------------------------------------------------------- سيارات المرور والشرطة

static int laneDir(int t) {
    switch (t) {
    case T_VLR: return 2;   // الحارة الغربية: جنوبًا
    case T_VLL: return 0;   // الحارة الشرقية: شمالًا
    case T_HLB: return 3;   // الحارة الشمالية: غربًا
    case T_HLT: return 1;   // الحارة الجنوبية: شرقًا
    default:    return -1;
    }
}

static bool placeCar(Car *c, int type, int maxD, bool anywhere) {
    int px = playerPX(), py = playerPY();
    for (int tries = 0; tries < 80; tries++) {
        int tx = rnd(TILES), ty = rnd(TILES);
        int d = laneDir(tileType[ty][tx]);
        if (d < 0) continue;
        int x = tx * 8 + 4, y = ty * 8 + 4;
        if (!anywhere && !offscreen(x, y, 24)) continue;
        int dx = x - px, dy = y - py;
        if (dx * dx + dy * dy > maxD * maxD) continue;
        if (anywhere && dx * dx + dy * dy < 40 * 40) continue;
        memset(c, 0, sizeof(*c));
        c->x = x << 8;
        c->y = y << 8;
        c->dir = d;
        c->type = type;
        c->alive = 1;
        c->turnDir = -1;
        c->cruise = type ? 540 : 230 + rnd(90);
        c->cur = c->cruise / 2;
        c->color = rnd(5);
        return true;
    }
    return false;
}

static bool inIntersection(int px, int py, int *ix, int *iy) {
    int i = (px + 8) >> 7, j = (py + 8) >> 7;
    if (i < 0 || i > 7 || j < 0 || j > 7) return false;
    int bx = 56 + 128 * i, by = 56 + 128 * j;
    if (px >= bx && px <= bx + 15 && py >= by && py <= by + 15) {
        *ix = i;
        *iy = j;
        return true;
    }
    return false;
}

static bool hasRoad(int nd, int ix, int iy) {
    switch (nd) {
    case 0:  return iy > 0;
    case 1:  return ix < 7;
    case 2:  return iy < 7;
    default: return ix > 0;
    }
}

static void chooseTurn(Car *c, int ix, int iy) {
    int d = c->dir;
    int cand[3] = { d, (d + 3) & 3, (d + 1) & 3 };
    int opts[3], n = 0;
    for (int k = 0; k < 3; k++)
        if (hasRoad(cand[k], ix, iy)) opts[n++] = cand[k];
    if (n == 0) { c->turnDir = -1; return; }

    int nd;
    if (c->type == 1) {
        // الشرطة: اختر الاتجاه الأقرب للاعب
        int px = playerPX(), py = playerPY();
        int best = 0x7fffffff;
        nd = opts[0];
        for (int k = 0; k < n; k++) {
            int nx = (64 + 128 * ix) + DX[opts[k]] * 128;
            int ny = (64 + 128 * iy) + DY[opts[k]] * 128;
            int dx = nx - px, dy = ny - py;
            int dist = dx * dx + dy * dy;
            if (dist < best) { best = dist; nd = opts[k]; }
        }
    } else {
        int k = rnd(n + 1);
        if (k >= n) k = 0;
        nd = opts[k];
    }

    if (nd == d) { c->turnDir = -1; return; }
    c->turnDir = nd;
    switch (nd) {
    case 1:  c->turnCoord = (68 + 128 * iy) << 8; break;  // شرق: مسار y
    case 3:  c->turnCoord = (60 + 128 * iy) << 8; break;  // غرب
    case 0:  c->turnCoord = (68 + 128 * ix) << 8; break;  // شمال: مسار x
    default: c->turnCoord = (60 + 128 * ix) << 8; break;  // جنوب
    }
}

static bool blockedBy(const Car *c, int cx, int cy, int ox, int oy) {
    int rx = ox - cx, ry = oy - cy;
    int fwd = rx * DX[c->dir] + ry * DY[c->dir];
    int lat = abs(rx * DY[c->dir] - ry * DX[c->dir]);
    return fwd > 0 && fwd < 22 && lat < 7;
}

static void stepCar(Car *c) {
    if (c->stun > 0) { c->stun--; c->cur = 0; return; }
    int cx = c->x >> 8, cy = c->y >> 8;
    int tgt = c->cruise;

    if (c->ghost > 0) {
        c->ghost--;
    } else {
        bool blocked = false;
        if (c->type == 0 && blockedBy(c, cx, cy, carX >> 8, carY >> 8)) blocked = true;
        for (int j = 0; j < MAX_CARS && !blocked; j++) {
            const Car *o = &cars[j];
            if (o == c || !o->alive) continue;
            if (blockedBy(c, cx, cy, o->x >> 8, o->y >> 8)) blocked = true;
        }
        if (blocked) {
            tgt = 0;
            if (++c->block > 100) { c->ghost = 70; c->block = 0; }
        } else {
            c->block = 0;
        }
    }

    if (c->cur < tgt) { c->cur += 14; if (c->cur > tgt) c->cur = tgt; }
    else if (c->cur > tgt) { c->cur -= 48; if (c->cur < tgt) c->cur = tgt; }

    int ox = c->x, oy = c->y;
    c->x += DX[c->dir] * c->cur;
    c->y += DY[c->dir] * c->cur;

    if (c->turnDir >= 0) {
        int T = c->turnCoord;
        int crossed = 0;
        switch (c->dir) {
        case 0:  if (oy > T && c->y <= T) { c->y = T; crossed = 1; } break;
        case 2:  if (oy < T && c->y >= T) { c->y = T; crossed = 1; } break;
        case 1:  if (ox < T && c->x >= T) { c->x = T; crossed = 1; } break;
        default: if (ox > T && c->x <= T) { c->x = T; crossed = 1; } break;
        }
        if (crossed) { c->dir = c->turnDir; c->turnDir = -1; }
    }

    int ix, iy;
    if (inIntersection(c->x >> 8, c->y >> 8, &ix, &iy)) {
        if (!c->inInter) { c->inInter = 1; chooseTurn(c, ix, iy); }
    } else {
        c->inInter = 0;
        c->turnDir = -1;
    }
}

static void updateCars(void) {
    int px = playerPX(), py = playerPY();
    int pc = 0;
    for (int i = MAX_TRAFFIC; i < MAX_CARS; i++) if (cars[i].alive) pc++;
    int wantPolice = (wanted > MAX_POLICE) ? MAX_POLICE : wanted;

    for (int i = 0; i < MAX_CARS; i++) {
        Car *c = &cars[i];
        bool police = (i >= MAX_TRAFFIC);
        if (c->alive) {
            int dx = (c->x >> 8) - px, dy = (c->y >> 8) - py;
            if (dx * dx + dy * dy > 420 * 420) c->alive = 0;
            else if (police && wanted == 0 && offscreen(c->x >> 8, c->y >> 8, 24)) c->alive = 0;
            if (!c->alive && police) pc--;
        }
        if (!c->alive) {
            if (!police) placeCar(c, 0, 330, false);
            else if (pc < wantPolice && placeCar(c, 1, 330, false)) pc++;
        }
        if (c->alive) stepCar(c);
    }
}

static void collideCars(void) {
    int px = playerPX(), py = playerPY();
    for (int i = 0; i < MAX_CARS; i++) {
        Car *c = &cars[i];
        if (!c->alive) continue;
        int dx = (c->x >> 8) - px, dy = (c->y >> 8) - py;
        int d2 = dx * dx + dy * dy;
        bool police = (i >= MAX_TRAFFIC);
        if (police && wanted > 0) {
            int r = inCar ? 14 : 10;
            if (d2 < r * r) {
                if (!inCar || abs(speed) < 220) {
                    bustedTimer = 120;
                    speed = 0;
                    return;
                }
                speed = -speed / 2;
                c->stun = 30;
            }
        } else if (inCar && d2 < 12 * 12) {
            speed = -speed / 2;
            c->stun = 25;
        }
    }
}

// ---------------------------------------------------------------- المشاة

static void pickPedDir(Ped *p) {
    static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    int k = rnd(5);
    if (k < 4) { p->dx = dirs[k][0]; p->dy = dirs[k][1]; }
    else { p->dx = 0; p->dy = 0; }
    p->timer = 30 + rnd(90);
}

static bool placePed(Ped *p, bool anywhere) {
    int px = playerPX(), py = playerPY();
    for (int tries = 0; tries < 60; tries++) {
        int tx = rnd(TILES), ty = rnd(TILES);
        int t = tileType[ty][tx];
        if (t != T_SIDE && t != T_GRASS) continue;
        int x = tx * 8 + 4, y = ty * 8 + 4;
        if (!anywhere && !offscreen(x, y, 16)) continue;
        int dx = x - px, dy = y - py;
        if (dx * dx + dy * dy > 320 * 320) continue;
        p->x = x;
        p->y = y;
        p->alive = 1;
        p->color = rnd(4);
        pickPedDir(p);
        return true;
    }
    return false;
}

static void updatePeds(void) {
    int px = playerPX(), py = playerPY();
    for (int i = 0; i < MAX_PEDS; i++) {
        Ped *p = &peds[i];
        if (!p->alive) {
            if (p->respawn > 0) p->respawn--;
            else placePed(p, false);
            continue;
        }
        int dx = p->x - px, dy = p->y - py;
        if (dx * dx + dy * dy > 400 * 400) { p->alive = 0; continue; }
        if (inCar && abs(speed) > 180) {
            int hx = p->x - (carX >> 8), hy = p->y - (carY >> 8);
            if (hx * hx + hy * hy < 10 * 10) {
                p->alive = 0;
                p->respawn = 180;
                if (wanted < 5) wanted++;
                heat = 0;
                continue;
            }
        }
        if ((frame & 1) == 0) {
            int nx = p->x + p->dx, ny = p->y + p->dy;
            if (walkable(nx, ny)) { p->x = nx; p->y = ny; }
            else p->timer = 0;
        }
        if (--p->timer <= 0) pickPedDir(p);
    }
}

// ---------------------------------------------------------------- اللاعب

static void resetPlayer(void) {
    carX = 64 << 8;
    carY = 120 << 8;
    angle = 0;
    speed = 0;
    footX = 64;
    footY = 120;
    inCar = 1;
}

static void resetAfterBusted(void) {
    resetPlayer();
    wanted = 0;
    heat = 0;
    score /= 2;
    for (int i = MAX_TRAFFIC; i < MAX_CARS; i++) cars[i].alive = 0;
}

static void updatePlayer(int held, int down) {
    s16 a16 = (s16)(angle << 6);
    int sn = sinLerp(a16), cs = cosLerp(a16);

    if (inCar) {
        int cap = onGrass(carX >> 8, carY >> 8) ? 350 : 768;
        if (held & (KEY_UP | KEY_A)) speed += 22;
        else if (held & (KEY_DOWN | KEY_B)) speed -= (speed > 0) ? 40 : 18;
        else speed -= speed / 28;
        if (speed > cap) speed -= 20;
        if (speed < -300) speed = -300;
        if (speed > -8 && speed < 8 && !(held & (KEY_UP | KEY_A | KEY_DOWN | KEY_B))) speed = 0;

        if (speed > 40 || speed < -40) {
            int turn = 0;
            if (held & KEY_LEFT)  turn = -3;
            if (held & KEY_RIGHT) turn = 3;
            if (speed < 0) turn = -turn;
            angle = (angle + turn) & 511;
            a16 = (s16)(angle << 6);
            sn = sinLerp(a16);
            cs = cosLerp(a16);
        }

        int dx = (sn * speed) >> 12;
        int dy = -((cs * speed) >> 12);
        int nx = carX + dx, ny = carY + dy;
        int dir = (speed >= 0) ? 1 : -1;
        int noseX = (nx >> 8) + dir * ((sn * 7) >> 12);
        int noseY = (ny >> 8) - dir * ((cs * 7) >> 12);
        if (solidAt(nx >> 8, ny >> 8) || solidAt(noseX, noseY)) {
            speed = -speed / 3;
        } else {
            carX = nx;
            carY = ny;
        }

        if ((down & KEY_Y) && speed < 64 && speed > -64) {
            int fx = (carX >> 8) + ((cs * 12) >> 12);
            int fy = (carY >> 8) + ((sn * 12) >> 12);
            if (!walkable(fx, fy) && solidAt(fx, fy)) { fx = carX >> 8; fy = carY >> 8; }
            footX = fx;
            footY = fy;
            inCar = 0;
            speed = 0;
        }
    } else {
        int mx = ((held & KEY_RIGHT) ? 1 : 0) - ((held & KEY_LEFT) ? 1 : 0);
        int my = ((held & KEY_DOWN) ? 1 : 0) - ((held & KEY_UP) ? 1 : 0);
        if (!solidAt(footX + mx * 2, footY)) footX += mx * 2;
        if (!solidAt(footX, footY + my * 2)) footY += my * 2;

        int ddx = footX - (carX >> 8), ddy = footY - (carY >> 8);
        if ((down & KEY_Y) && ddx * ddx + ddy * ddy < 24 * 24) inCar = 1;
    }
}

// ---------------------------------------------------------------- رسم الشاشة السفلية

static void putPix(int x, int y, u8 c) {
    if (x < 0 || y < 0 || x >= 256 || y >= 192) return;
    backbuf[y * 256 + x] = c;
}

static void fillRect(int x, int y, int w, int h, u8 c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            putPix(x + i, y + j, c);
}

static void dot(int x, int y, u8 c) { fillRect(x - 1, y - 1, 3, 3, c); }

static void drawGlyph(const u8 *rows, int x, int y, u8 c) {
    for (int r = 0; r < 5; r++)
        for (int b = 0; b < 3; b++)
            if (rows[r] & (4 >> b))
                fillRect(x + b * 4, y + r * 4, 4, 4, c);
}

static void drawNumber(int n, int x, int y, u8 c) {
    int digits[10], k = 0;
    if (n < 0) n = 0;
    if (n == 0) digits[k++] = 0;
    while (n > 0 && k < 10) { digits[k++] = n % 10; n /= 10; }
    for (int i = 0; i < k; i++) drawGlyph(font[digits[k - 1 - i]], x + i * 16, y, c);
}

static void drawHud(void) {
    memset(backbuf, 0, sizeof(backbuf));
    for (int y = 0; y < 128; y++)
        memcpy(&backbuf[(32 + y) * 256 + 8], &miniStatic[y * 128], 128);

    if ((frame >> 4) & 1)
        dot(8 + TARGET_X(target) / 8, 32 + TARGET_Y(target) / 8, 6);
    for (int i = MAX_TRAFFIC; i < MAX_CARS; i++)
        if (cars[i].alive) dot(8 + (cars[i].x >> 8) / 8, 32 + (cars[i].y >> 8) / 8, 9);
    dot(8 + playerPX() / 8, 32 + playerPY() / 8, 5);

    drawNumber(score, 150, 40, 7);
    for (int k = 0; k < wanted; k++) fillRect(150 + k * 14, 80, 10, 10, 6);

    if (bustedTimer > 0)
        for (int k = 0; k < 6; k++) drawGlyph(letters[k], 40 + k * 16, 6, 5);
}

// ---------------------------------------------------------------- السبرايت

static void hideSprite(int id) {
    oamSet(&oamMain, id, 0, 0, 0, 0, SpriteSize_8x8, SpriteColorFormat_256Color,
           pFoot, -1, false, true, false, false, false);
}

static void drawSprites(void) {
    int csx = (carX >> 8) - camX, csy = (carY >> 8) - camY;
    oamRotateScale(&oamMain, 0, ROT_SIGN * (angle << 6), 256, 256);
    oamSet(&oamMain, OAM_PLAYER, csx - 16, csy - 16, 0, 0,
           SpriteSize_16x16, SpriteColorFormat_256Color, pPlayer,
           0, true, (csx < -24 || csx > 280 || csy < -24 || csy > 216),
           false, false, false);

    oamSet(&oamMain, OAM_FOOT, footX - camX - 4, footY - camY - 4, 0, 0,
           SpriteSize_8x8, SpriteColorFormat_256Color, pFoot,
           -1, false, inCar, false, false, false);

    int msx = TARGET_X(target) - camX, msy = TARGET_Y(target) - camY;
    bool mh = ((frame >> 4) & 1) || msx < -8 || msx > 264 || msy < -8 || msy > 200;
    oamSet(&oamMain, OAM_MARKER, msx - 4, msy - 4, 0, 0,
           SpriteSize_8x8, SpriteColorFormat_256Color, pMarker,
           -1, false, mh, false, false, false);

    for (int i = 0; i < MAX_CARS; i++) {
        const Car *c = &cars[i];
        int id = OAM_CAR0 + i;
        if (!c->alive) { hideSprite(id); continue; }
        int sx = (c->x >> 8) - camX, sy = (c->y >> 8) - camY;
        if (sx < -24 || sx > 280 || sy < -24 || sy > 216) { hideSprite(id); continue; }
        u16 *g = (i >= MAX_TRAFFIC) ? pPolice[(frame >> 3) & 1] : pTraffic[c->color];
        oamSet(&oamMain, id, sx - 16, sy - 16, 0, 0,
               SpriteSize_16x16, SpriteColorFormat_256Color, g,
               1 + c->dir, true, false, false, false, false);
    }

    for (int i = 0; i < MAX_PEDS; i++) {
        const Ped *p = &peds[i];
        int id = OAM_PED0 + i;
        if (!p->alive) { hideSprite(id); continue; }
        int sx = p->x - camX, sy = p->y - camY;
        if (sx < -8 || sx > 264 || sy < -8 || sy > 200) { hideSprite(id); continue; }
        oamSet(&oamMain, id, sx - 4, sy - 4, 0, 0,
               SpriteSize_8x8, SpriteColorFormat_256Color, pPed[p->color],
               -1, false, false, false, false, false);
    }
}

// ---------------------------------------------------------------- البرنامج الرئيسي

int main(void) {
    powerOn(POWER_ALL_2D);
    lcdMainOnTop();

    // الشاشة العليا: المدينة (خلفية دوّارة موسّعة 1024x1024) + السبرايت
    videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | DISPLAY_SPR_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    // الشاشة السفلى: الخريطة المصغرة
    videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    vramSetBankC(VRAM_C_SUB_BG);

    buildTiles();
    buildWorld();
    buildSprites();
    buildMinimap();

    // ألوان الخلفية
    BG_PALETTE[0]  = RGB15(0, 0, 0);
    BG_PALETTE[1]  = RGB15(6, 18, 6);
    BG_PALETTE[2]  = RGB15(4, 14, 4);
    BG_PALETTE[3]  = RGB15(9, 9, 10);
    BG_PALETTE[4]  = RGB15(28, 28, 20);
    BG_PALETTE[5]  = RGB15(20, 20, 19);
    BG_PALETTE[6]  = RGB15(12, 12, 15);
    BG_PALETTE[7]  = RGB15(6, 6, 9);
    BG_PALETTE[8]  = RGB15(22, 22, 24);
    BG_PALETTE[10] = RGB15(19, 7, 5);
    BG_PALETTE[11] = RGB15(10, 4, 3);
    BG_PALETTE[12] = RGB15(24, 11, 8);
    BG_PALETTE[13] = RGB15(14, 15, 13);
    BG_PALETTE[14] = RGB15(9, 10, 8);
    BG_PALETTE[15] = RGB15(4, 9, 22);
    BG_PALETTE[16] = RGB15(8, 15, 28);
    BG_PALETTE[17] = RGB15(7, 7, 8);
    BG_PALETTE[18] = RGB15(24, 24, 24);
    BG_PALETTE[19] = RGB15(17, 17, 16);

    // ألوان السبرايت
    SPRITE_PALETTE[0]  = RGB15(0, 0, 0);
    SPRITE_PALETTE[1]  = RGB15(28, 3, 3);
    SPRITE_PALETTE[2]  = RGB15(6, 10, 20);
    SPRITE_PALETTE[3]  = RGB15(2, 2, 2);
    SPRITE_PALETTE[4]  = RGB15(31, 30, 10);
    SPRITE_PALETTE[5]  = RGB15(5, 12, 28);
    SPRITE_PALETTE[6]  = RGB15(28, 22, 16);
    SPRITE_PALETTE[7]  = RGB15(31, 24, 0);
    SPRITE_PALETTE[8]  = RGB15(5, 8, 26);
    SPRITE_PALETTE[9]  = RGB15(4, 20, 6);
    SPRITE_PALETTE[10] = RGB15(28, 28, 28);
    SPRITE_PALETTE[11] = RGB15(31, 26, 2);
    SPRITE_PALETTE[12] = RGB15(18, 6, 22);
    SPRITE_PALETTE[13] = RGB15(4, 6, 28);
    SPRITE_PALETTE[14] = RGB15(29, 29, 30);
    SPRITE_PALETTE[15] = RGB15(31, 2, 2);
    SPRITE_PALETTE[16] = RGB15(28, 8, 8);
    SPRITE_PALETTE[17] = RGB15(8, 24, 8);
    SPRITE_PALETTE[18] = RGB15(28, 24, 4);
    SPRITE_PALETTE[19] = RGB15(20, 8, 26);

    // ألوان الخريطة المصغرة
    BG_PALETTE_SUB[0] = RGB15(1, 1, 4);
    BG_PALETTE_SUB[1] = RGB15(5, 16, 5);
    BG_PALETTE_SUB[2] = RGB15(12, 12, 13);
    BG_PALETTE_SUB[3] = RGB15(18, 18, 17);
    BG_PALETTE_SUB[4] = RGB15(9, 6, 6);
    BG_PALETTE_SUB[5] = RGB15(31, 4, 4);
    BG_PALETTE_SUB[6] = RGB15(31, 28, 0);
    BG_PALETTE_SUB[7] = RGB15(31, 31, 31);
    BG_PALETTE_SUB[8] = RGB15(4, 9, 22);
    BG_PALETTE_SUB[9] = RGB15(8, 12, 31);

    // خلفية المدينة: الخريطة 32KB تبدأ من 0، والبلاطات بعدها (16KB × 2)
    int bg = bgInit(2, BgType_ExRotation, BgSize_ER_1024x1024, 0, 2);
    DC_FlushRange(tileGfx, sizeof(tileGfx));
    DC_FlushRange(mapRam, sizeof(mapRam));
    dmaCopy(tileGfx, bgGetGfxPtr(bg), sizeof(tileGfx));
    dmaCopy(mapRam, bgGetMapPtr(bg), sizeof(mapRam));

    // الخلفية السفلية (bitmap)
    int sbg = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    u16 *subGfx = bgGetGfxPtr(sbg);

    // السبرايت
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    pPlayer = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    for (int i = 0; i < 5; i++)
        pTraffic[i] = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    for (int i = 0; i < 2; i++)
        pPolice[i] = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    for (int i = 0; i < 4; i++)
        pPed[i] = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_256Color);
    pFoot   = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_256Color);
    pMarker = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_256Color);

    DC_FlushRange(gfxPlayer, sizeof(gfxPlayer));
    DC_FlushRange(gfxTraffic, sizeof(gfxTraffic));
    DC_FlushRange(gfxPolice, sizeof(gfxPolice));
    DC_FlushRange(gfxPed, sizeof(gfxPed));
    DC_FlushRange(gfxFoot, sizeof(gfxFoot));
    DC_FlushRange(gfxMarker, sizeof(gfxMarker));
    dmaCopy(gfxPlayer, pPlayer, 256);
    for (int i = 0; i < 5; i++) dmaCopy(gfxTraffic[i], pTraffic[i], 256);
    for (int i = 0; i < 2; i++) dmaCopy(gfxPolice[i], pPolice[i], 256);
    for (int i = 0; i < 4; i++) dmaCopy(gfxPed[i], pPed[i], 64);
    dmaCopy(gfxFoot, pFoot, 64);
    dmaCopy(gfxMarker, pMarker, 64);

    // مصفوفات الدوران لسيارات المرور (4 اتجاهات ثابتة)
    for (int d = 0; d < 4; d++)
        oamRotateScale(&oamMain, 1 + d, ROT_SIGN * ((d * 128) << 6), 256, 256);

    // بداية اللعبة
    resetPlayer();
    score = 0;
    target = 0;
    wanted = 0;
    heat = 0;
    bustedTimer = 0;
    frame = 0;
    camX = carX / 256 - 128;
    camY = carY / 256 - 96;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    for (int i = 0; i < MAX_TRAFFIC; i++) placeCar(&cars[i], 0, 260, true);
    for (int i = 0; i < MAX_PEDS; i++) placePed(&peds[i], true);

    while (1) {
        scanKeys();
        int held = keysHeld();
        int down = keysDown();
        frame++;

        if (bustedTimer > 0) {
            bustedTimer--;
            if (bustedTimer == 0) resetAfterBusted();
        } else {
            updatePlayer(held, down);
        }

        // الكاميرا
        int px = playerPX(), py = playerPY();
        camX = px - 128;
        camY = py - 96;
        if (camX < 0) camX = 0;
        if (camX > WORLD - 256) camX = WORLD - 256;
        if (camY < 0) camY = 0;
        if (camY > WORLD - 192) camY = WORLD - 192;
        bgSetScroll(bg, camX, camY);

        // المهمة
        {
            int tdx = px - TARGET_X(target), tdy = py - TARGET_Y(target);
            if (inCar && bustedTimer == 0 && tdx * tdx + tdy * tdy < 14 * 14) {
                score += 100;
                target = (target + 1) % NUM_TARGETS;
            }
        }

        updateCars();
        updatePeds();
        if (bustedTimer == 0) collideCars();

        // انخفاض مستوى المطلوبية عند الابتعاد عن الشرطة
        if (wanted > 0 && bustedTimer == 0) {
            bool near = false;
            for (int i = MAX_TRAFFIC; i < MAX_CARS; i++) {
                if (!cars[i].alive) continue;
                int dx = (cars[i].x >> 8) - px, dy = (cars[i].y >> 8) - py;
                if (dx * dx + dy * dy < 160 * 160) near = true;
            }
            if (near) heat = 0;
            else if (++heat >= 600) { wanted--; heat = 0; }
        }

        drawSprites();
        drawHud();
        DC_FlushRange(backbuf, sizeof(backbuf));

        swiWaitForVBlank();
        bgUpdate();
        oamUpdate(&oamMain);
        dmaCopy(backbuf, subGfx, sizeof(backbuf));
    }
    return 0;
}
