
#define tile_size 8

#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <float.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#define PHYS_EPSILON sqrt(FLT_EPSILON)

SDL_Window *win;
SDL_Renderer *ren;
SDL_Texture *pixelbuffer;
SDL_Joystick *joy;
SDL_Rect projection;
int running = 1;
int frame;

#define maxof(lcs) lcs##_max
#define countof(lcs) lcs##_count
#define dataof(lcs) lcs##s

#define tc_create(type, lcs, max) \
   const int maxof(lcs) = max; \
   type dataof(lcs)[max];\
   int countof(lcs) = 0;

#define tc_empty(lcs) (countof(lcs) == 0)
#define tc_full(lcs) (countof(lcs) == maxof(lcs))
#define tc_new(lcs) ((!tc_full(lcs))?(dataof(lcs)+(countof(lcs)++)):0)
#define tc_inarray(lcs, ind) ((ind) >= 0 && (ind) < countof(lcs))
#define tc_at(lcs, ind) (dataof(lcs) + (ind))
#define tc_at_safe(lcs, ind) ((tc_inarray(lcs, ind))?tc_at(lcs, ind):0)
#define tc_at_wrap(lcs, ind) (tc_at(lcs, (ind)%countof(lcs)))
#define tc_back(lcs) (tc_at(lcs, (countof(lcs) - 1)))
#define tc_erase(lcs, ind) {if (tc_inarray(lcs, ind)) { *tc_at(lcs, ind) = tc_back(lcs); countof(lcs)--;}}

#define field_w 320
#define field_h 240
#define field_w_tiles field_w/tile_size
#define field_h_tiles field_h/tile_size
#define start_w 640
#define start_h 480

Uint64 secondsToPCF(float seconds)
{
   return SDL_GetPerformanceFrequency() * seconds;
}

inline
float fapproach(float a, float t, float step)
{
   if (fabs(a - t) < step) {
      return t;
   } else {
      if (a > t) {
         return a - step;
      } else {
         return a + step;
      }
   }
}

union v2 {
   float m[2];
   struct {
      float 
         x, y;
   };
}; 

void printv2(v2 *v, const char * str)
{
   printf("%s: (%f, %f)\n", str, v->x, v->y);
}

v2 makev2(float x, float y)
{
   v2 res;
   res.x = x;
   res.y = y;
   return res;
}

v2 makeRotatedV2(float x, float y, float a)
{
   v2 res;
   float cosa = cos(a);
   float sina = sin(a);
   res.x = (cosa*x + sina*y);
   res.y = (cosa*y - sina*x);
   return res;
}

const v2 v2blank = {};
v2 v2null = {};

v2 operator+(const v2 &a, const v2 &b)
{
   v2 res = {a.x + b.x, a.y + b.y};
   return res;
}

v2 operator-(const v2 &a, const v2 &b)
{
   v2 res = {a.x - b.x, a.y - b.y};
   return res;
}

v2 operator-(const v2 &a)
{
   v2 res = {-a.x, -a.y};
   return res;
}

v2 operator*(const v2 &a, const float b)
{
   v2 res = {a.x * b, a.y * b};
   return res;
}

v2 operator*(const float a, const v2 &b)
{
   return b*a;
}

float operator*(const v2 &a, const v2 &b)
{
   return (a.x * b.x + b.y + a.y);
}

float operator^(const v2 &a, const v2 &b)
{
   return (a.x * b.y - a.y * b.x);
}

float sqlenv2(v2 *v)
{
   return ((v->x * v->x) + (v->y * v->y));
}

float lenv2(v2 *v)
{
   return sqrt((v->x * v->x) + (v->y * v->y));
}

int v2lequal(v2 *v, float d)
{
   return (sqlenv2(v) <= d*d);
}

int v2gequal(v2 *v, float d)
{
   return (sqlenv2(v) >= d*d);
}

v2 normalizev2(v2 *v)
{
   v2 res;
   float ilen = 1/lenv2(v);
   res.x = ilen * v->x;
   res.y = ilen * v->y;
   return res;
}

struct rect {
   float x, y, w, h;
};

struct {
   rect bounds;
   v2 position;
} camera;

inline
SDL_Rect rectToSDLRect(rect *r)
{
   SDL_Rect crect = {
      floor(r->x),
      floor(r->y),
      floor(r->w),
      floor(r->h)
   };
   return crect;
}

void drawRect(SDL_Renderer *ren, rect *r)
{
   SDL_Rect crect = rectToSDLRect(r);
   crect.x -= camera.position.x;
   crect.y -= camera.position.y;
   SDL_RenderDrawRect(ren, &crect);
}

void fillRect(SDL_Renderer *ren, rect *r)
{
   SDL_Rect crect = rectToSDLRect(r);
   crect.x -= camera.position.x;
   crect.y -= camera.position.y;
   SDL_RenderFillRect(ren, &crect);
}

#define ROOM_CONNECTION_MAX 9
#define RC_FILE_MAX 20
struct {
   rect bounds;
   rect connections[ROOM_CONNECTION_MAX];
   char roomname[RC_FILE_MAX];
   char filenames[ROOM_CONNECTION_MAX][RC_FILE_MAX];
   v2 transition_offset;
   int connection_count;
} room;

void setRoomName(const char* nname)
{
   int size = strlen(nname);
   size = (size < RC_FILE_MAX)?size:RC_FILE_MAX;
   strncpy(room.roomname, nname, size);
   room.roomname[size] = 0;
   //printf("filename is %s\n", room.roomname);
}

void resetConnections()
{
   rect rs = {};
   for (int i = 0; i < ROOM_CONNECTION_MAX; i++) {
      room.connections[i] = rs;
      room.filenames[i][0] = 0;
   }
}

void drawConnections()
{
   SDL_SetRenderDrawColor(ren, 0, 100, 0, 255);
   for (int i = 0; i < ROOM_CONNECTION_MAX; i++) {
      drawRect(ren, room.connections + i);
   }
}

rect makeRect(float x, float y, float w, float h)
{
   rect res = {x, y, w, h};
   return res;
}

rect makeTileAlignedRect(int x, int y, int w, int h)
{
   return makeRect(x * tile_size, y * tile_size, w * tile_size, h * tile_size);
}

rect expandRect(rect *r, float e)
{
   rect res = *r;
   res.x -= e;
   res.y -= e;
   res.w += 2*e;
   res.h += 2*e;
   return res;
}

int pointInRect(rect *r, v2 *p)
{
   return (p->x >= r->x && p->x <= r->x + r->w &&
         p->y >= r->y && p->y <= r->y + r->h);
}

//minkowski
rect operator*(const rect &a, const rect &b)
{
   rect res = {
      a.x - (b.x + b.w),
      a.y - (b.y + b.h),
      a.w + b.w, a.h + b.h
   };
   return res;
}

rect operator+(const rect &a, const rect &b)
{
   float planex = fmax(a.x + a.w, b.x + b.w);
   float planey = fmax(a.y + a.h, b.y + b.h);
   rect res;
   res.x = fmin(a.x, b.x);
   res.y = fmin(a.y, b.y);
   res.w = planex - res.x;
   res.h = planey - res.y;
   return res;
}

rect extendRect(rect src, v2 &d)
{
   src.w += fabs(d.x);
   src.h += fabs(d.y);
   if (d.x < 0) {
      src.x += d.x;
   }
   if (d.y < 0) {
      src.y += d.y;
   }
   return src;
}

int rectsOverlap(rect *a, rect *b)
{
   return (a->x <= b->x + b->w && a->y <= b->y + b->h && b->x <= a->x + a->w && b->y <= a->y + a->h);
}

int clipMovingRects(rect *a, v2 *da, rect *b, v2 *db, v2 *n, float *t)
{
   assert(a);
   assert(b);
   assert(da);
   assert(db);
   assert(n);
   assert(t);
#if 0
   if (rectsOverlap(a, b)) {
      *t = 0;
      *n = v2blank;
      return 1;
   }
#endif
   *t = 1;
   *n = v2blank;
   float tstart = 0.f;
   float tend = 1.f;
   rect mrect = (*a)*(*b);
   v2 vel = (*db)-(*da);
   v2 normal = {};

#if 0
   if (vel.y > 0) {
      if (mrect.y + mrect.h < 0) return 0;
      float teststart = mrect.y / vel.y;
      float testend = (mrect.y + mrect.h) / vel.y;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.x = 0;
         normal.y = 1;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else if (vel.y < 0) {
      if (mrect.y > 0) return 0;
      float teststart = (mrect.y + mrect.h) / vel.y;
      float testend = mrect.y / vel.y;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.x = 0;
         normal.y = -1;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else {
      if (!(mrect.y < 0 && mrect.y + mrect.w > 0)) {
         return 0;
      }
   }
#endif
   if (vel.y > 0) {
      if (mrect.y + mrect.h < 0) return 0;
      float teststart = mrect.y / vel.y;
      float testend = (mrect.y + mrect.h) / vel.y;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.y = 1;
         normal.x = 0;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else if (vel.y < 0) {
      if (mrect.y > 0) return 0;
      float teststart = (mrect.y + mrect.h) / vel.y;
      float testend = mrect.y / vel.y;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.y = -1;
         normal.x = 0;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else {
      if (!(mrect.y < 0 && mrect.y + mrect.h > 0)) {
         return 0;
      }
   }

   if (vel.x > 0) {
      if (mrect.x + mrect.w < 0) return 0;
      float teststart = mrect.x / vel.x;
      float testend = (mrect.x + mrect.w) / vel.x;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.x = 1;
         normal.y = 0;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else if (vel.x < 0) {
      if (mrect.x > 0) return 0;
      float teststart = (mrect.x + mrect.w) / vel.x;
      float testend = mrect.x / vel.x;
      if (teststart >= tstart) {
         tstart = teststart;
         normal.x = -1;
         normal.y = 0;
      }
      if (testend < tend) {
         tend = testend;
      }
   } else {
      if (!(mrect.x < 0 && mrect.x + mrect.w > 0)) {
         return 0;
      }
   }

   if (tstart < tend) {
      *t = fmax(tstart - PHYS_EPSILON, 0);
      *n = normal;
      return 1;
   }
   return 0;
}

void setCameraFocus(v2 * p)
{
   v2 tpos;
   tpos.x = fmin(fmax(camera.bounds.x, floor(p->x - field_w / 2)), camera.bounds.x + camera.bounds.w);
   tpos.y = fmin(fmax(camera.bounds.y, floor(p->y - field_h / 2)), camera.bounds.y + camera.bounds.h);
   camera.position = tpos;
}

void setCameraFocus(float x, float y)
{
   v2 r = makev2(x, y);
   setCameraFocus(&r);
}

struct ladder {
   rect bounds;
};

tc_create(ladder, ladder, 64);

void createTileAlignedLadder(int x, int y, int w, int h)
{
   ladder *l = tc_new(ladder);
   if (l) {
      l->bounds.x = x * tile_size;
      l->bounds.y = y * tile_size;
      l->bounds.w = w * tile_size;
      l->bounds.h = h * tile_size;
   }
}

ladder* getIntersectingLadder(rect *mr)
{
   for (int i = 0; i < countof(ladder); i++) {
      ladder *l = tc_at(ladder, i);
      if (rectsOverlap(mr, &l->bounds)) {
         return l;
      }
   }
   return 0;
}

int rectIntersectsLadders(rect *mr)
{
   for (int i = 0; i < countof(ladder); i++) {
      ladder *l = tc_at(ladder, i);
      if (rectsOverlap(mr, &l->bounds)) {
         return 1;
      }
   }
   return 0;
}

void drawLadders()
{
   SDL_SetRenderDrawColor(ren, 120, 80, 20, 255);
   for (int i = 0; i < countof(ladder); i++) {
      ladder *l = tc_at(ladder, i);
      drawRect(ren, &l->bounds);
   }
}

struct wall {
   rect bounds;
   int active;
};

tc_create(wall, wall, 512);

int rectIntersectsWalls(rect *mr)
{
   for (int i = 0; i < countof(wall); i++) {
      wall *w = tc_at(wall, i);
      if (w->active && rectsOverlap(mr, &w->bounds)) {
         return 1;
      }
   }
   return 0;
}

int rectOnGround(rect *mr)
{
   rect b = expandRect(mr, -0.5);
   b.y += 1;
   drawRect(ren, &b);
   return rectIntersectsWalls(&b);
}

int clipMovingRectWithWalls(rect *mr, v2 *mrv, v2 *n, float *t)
{
   int res = 0;
   v2 wallv = {};
   v2 bestn = {};
   float bestt = 1.f;
   for (int i = 0; i < countof(wall); i++) {
      wall *w = tc_at(wall, i);
      if (w->active) {
         float testt;
         v2 testn;
         if (i == 0) {
            int debug = -1;
         }
         int clipped = clipMovingRects(mr, mrv, &w->bounds, &wallv, &testn, &testt);
         res |= clipped;
         if (clipped != 0 && testt < bestt) {
            bestt = testt;
            bestn = testn;
         }
      }
   }
   *n = bestn;
   *t = bestt;
   return res;
}

void getMotionWalled(rect *r, v2 *v, v2 *out_velocity, v2 *out_displacement)
{
   rect bounds = *r;
   v2 clip_normal;
   float clip_time;
   v2 ovel = *v;
   v2 frame_vel = *v;
   v2 frame_displacement = {};
   for (int i = 0; i < 2; i++) {
      if (clipMovingRectWithWalls(&bounds, &frame_vel, &clip_normal, &clip_time)) {
         frame_displacement = frame_displacement + (frame_vel * clip_time);
         frame_vel = frame_vel * (1.f - clip_time);
         if (fabs(clip_normal.x) > PHYS_EPSILON) {
            ovel.x = 0;
            frame_vel.x = 0;
         }
         if (fabs(clip_normal.y) > PHYS_EPSILON) {
            ovel.y = 0;
            frame_vel.y = 0;
         }
      } else {
         frame_displacement = frame_displacement + frame_vel;
         break;
      }
   }

   if (out_velocity) {
      *out_velocity = ovel;
   }
   if (out_displacement) {
      *out_displacement = frame_displacement;
   }
}

void clearWalls()
{
   countof(wall) = 0;
   countof(ladder) = 0;
}

void createWall(float x, float y, float w, float h)
{
   wall *wn = tc_new(wall);
   if (wn) {
      //printf("new wall: %f, %f, %f, %f\n", x, y, w, h);
      wn->bounds = makeRect(x, y, w, h);
      wn->active = 1;
   }
}

void createTileAlignedWall(int x, int y, int w, int h)
{
   if (w > 0 && h > 0) {
      createWall(x * tile_size + 0.5, y * tile_size + 0.5, w * tile_size, h * tile_size);
   }
}

void debugDrawWalls(SDL_Renderer *ren)
{
   for (int i = 0; i < countof(wall); i++) {
      wall *w = tc_at(wall, i);
      if (w->active) {
         drawRect(ren, &w->bounds);
      }
   }
}

#if 0
struct asprite {
   SDL_Texture *tex;
   int framecount;
   int pitch;
   int w, h;
};

asprite createAsprite(SDL_Texture *tex, int frame_w, int frame_h)
{
}
#endif

struct testsprite {
   SDL_Rect bounds;
   char r, g, b;
};

testsprite createTestSprite(int w, int h, char r, char g, char b)
{
   testsprite res = {};
   res.bounds.w = w;
   res.bounds.h = h;
   res.r = r;
   res.g = g;
   res.b = b;
   return res;
}

void drawTestSprite(SDL_Renderer *ren, float x, float y, testsprite *tsr)
{
   tsr->bounds.x = x; tsr->bounds.y = y;
   SDL_SetRenderDrawColor(ren, tsr->r, tsr->g, tsr->b, 255);
   SDL_RenderFillRect(ren, &tsr->bounds);
}

enum control_type {
   ct_dummy,
   ct_axis,
   ct_button,
   ct_key
};

#define JOY_THRESHOLD 6553

struct control {
   int type;
   int pressed;
   int held;
   int released;
   int frames;
   union {
      struct {
         int axis;
         int side;
      } axis;
      struct {
         int button;
      } button;
      struct {
         int keysym;
      } key;
   };
};

tc_create(control, control, 16);

void startControlFrame()
{
   for (int i = 0; i < countof(control); i++) {
      control *c = tc_at(control, i);
      c->pressed = 0;
      c->released = 0;
      c->frames++;
   }
}

void fireControlEvent(SDL_Event *e)
{
   assert(e);
   for (int i = 0; i < countof(control); i++) {
      control *c = tc_at(control, i);
      switch (c->type) {
         case ct_axis:
            if (e->type == SDL_JOYAXISMOTION) {
               if (c->axis.axis == e->jaxis.axis) {
                  if (e->jaxis.value * c->axis.side > JOY_THRESHOLD) {
                     if (!c->held) {
                        c->held = 1;
                        c->pressed = 1;
                        c->frames = 0;
                     }
                  } else {
                     if (c->held) {
                        c->held = 0;
                        c->released = 1;
                        c->frames = 0;
                     }
                  }
               }
            }
            break;
         case ct_button:
            if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
               if (c->button.button == e->jbutton.button) {
                  if (e->type == SDL_JOYBUTTONDOWN) {
                     if (!c->held) {
                        c->held = 1;
                        c->pressed = 1;
                        c->frames = 0;
                     }
                  } else {
                     if (c->held) {
                        c->held = 0;
                        c->released = 1;
                        c->frames = 0;
                     }
                  }
               }
            }
            break;
         case ct_key:
            if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
               if (c->key.keysym == e->key.keysym.sym) {
                  if (e->type == SDL_KEYDOWN) {
                     if (!c->held) {
                        c->held = 1;
                        c->pressed = 1;
                        c->frames = 0;
                     }
                  } else {
                     if (c->held) {
                        c->held = 0;
                        c->released = 1;
                        c->frames = 0;
                     }
                  }
               }
            }
            break;
         default:
            break;
      };
   }
#if 0
   switch (e->type) {
      case SDL_JOYAXISMOTION:
         printf("axis %d: %d\n", e->jaxis.axis, e->jaxis.value);
         break;
      case SDL_JOYBUTTONDOWN:
         printf("button %d: down\n", e->jbutton.button);
         break;
      case SDL_JOYBUTTONUP:
         printf("button %d: up\n", e->jbutton.button);
         break;
      case SDL_KEYDOWN:
         printf("key %s: down\n", SDL_GetKeyName(e->key.keysym.sym));
         break;
      case SDL_KEYUP:
         printf("key %s: up\n", SDL_GetKeyName(e->key.keysym.sym));
         break;
      default:
         break;
   }
#endif
}

control * bindKey(int keysym)
{
   control *nc = tc_new(control);
   if (nc) {
      nc->type = ct_key;
      nc->key.keysym = keysym;
   }
   return nc;
}

control * bindAxis(int axis, int side)
{
   control *nc = tc_new(control);
   if (nc) {
      nc->type = ct_axis;
      nc->axis.axis = axis;
      nc->axis.side = (side > 0)?1:-1;
   }
   return nc;
}

control * bindButton(int button)
{
   control *nc = tc_new(control);
   if (nc) {
      nc->type = ct_button;
      nc->button.button = button;
   }
   return nc;
}

struct {
   control *left;
   control *up;
   control *down;
   control *right;
   control *jump;
} con;

struct p_shot {
   rect worldbounds;
   v2 position;
   v2 velocity;
   int shot_timer;
   int last_bounds_frame;
};

tc_create(p_shot, pshot, 3);

struct player {
   v2 position;
   v2 velocity;
   rect worldbounds;
   float w, h;
   int active;
   int alive;
   int jumping;
   int onladder;
   int last_bounds_frame;
} p1;

rect * getPlayerBounds(player *p)
{
   if (p->last_bounds_frame != frame) {
      p->worldbounds.x = p->position.x - 0.5*p->w;
      p->worldbounds.y = p->position.y - 0.5*p->h;
      p->worldbounds.w = p->w;
      p->worldbounds.h = p->h;
   }
   return &p->worldbounds;
}

player createPlayer(float x, float y)
{
   player res = {};
   //res.velocity.y = 100;
   res.velocity.x = 2;
   res.position = makev2(x, y);
   res.w = 14;
   res.h = 14;
   res.active = res.alive = 1;
   res.last_bounds_frame = frame-1;
   return res;
}

void tickPlayer(player *p)
{
   float player_accel = 0.1;
   float player_wspeed = 1.5;
   float player_gravity = 0.09;
   float player_jump = 4.5;
   int player_jump_grace = 20;
   if (p->onladder) {
      ladder *l = getIntersectingLadder(getPlayerBounds(p));
      if (l) {
         p->position.x = fapproach(p->position.x, l->bounds.x + l->bounds.w * 0.5, 1);
         if (con.up->held) {
            p->position.y -= 1;
         } else if (con.down->held) {
            p->position.y += 1;
         }
         if (con.jump->pressed) {
            if (!rectIntersectsWalls(getPlayerBounds(p))) {
               p->onladder = 0;
               p->velocity.x = 0;
               p->velocity.y = -player_jump * 0.5;
            }
         }
      } else {
         p->onladder = 0;
         p->velocity.x = 0;
         p->velocity.y = 0;
      }
   } else {
      rect bounds = *getPlayerBounds(p);
      if (con.left->held) {
         p->velocity.x = fapproach(p->velocity.x, -player_wspeed, player_accel);
      } else if (con.right->held) {
         p->velocity.x = fapproach(p->velocity.x, player_wspeed, player_accel);
      } else {
         p->velocity.x = fapproach(p->velocity.x, 0, player_accel);
      }

      if (con.jump->held && con.jump->frames < player_jump_grace) {
         if (rectOnGround(getPlayerBounds(p))) {
            p->velocity.y = -player_jump;
            p->jumping = 1;
         }
      }
      if (p->jumping) {
         if (con.jump->released && p->velocity.y < 0.f) {
            p->jumping = 0;
            p->velocity.y *= 0.3;
         } else if (p->velocity.y >= 0.f) {
            p->jumping = 0;
         }
      }
      p->velocity.y += player_gravity;
      v2 frame_displacement;
      getMotionWalled(getPlayerBounds(p), &p->velocity, &p->velocity, &frame_displacement);
      p->position = p->position + frame_displacement;
      if (con.up->pressed || con.down->pressed) {
         if (rectIntersectsLadders(getPlayerBounds(p))) {
            p->onladder = 1;
         }
      }
   }
   setCameraFocus(&p->position);
}

void drawPlayer(player *p)
{
   SDL_SetRenderDrawColor(ren, 255, 255, 100, 255);
   fillRect(ren, getPlayerBounds(p));
}

void loadLevel(const char * fname, int connection)
{
   SDL_RWops *rw = SDL_RWFromFile(fname, "r");
   if (connection != 0) {
      int debug = 5;
   }
   if (rw) {
      clearWalls();
      room.connection_count = 0;
      unsigned int size = SDL_RWsize(rw);
      char * fileblock = (char*)malloc(size);
      SDL_RWread(rw, fileblock, 1, size);
      SDL_RWclose(rw);
      int fp = 0;

      int tiles_w;
      int tiles_h;
      int screens_w;
      int screens_h;

      while(isspace(fileblock[fp])) {
         fp++;
      }

      tiles_w = atoi(fileblock + fp);
      while(!isspace(fileblock[fp])) {
         fp++;
      }
      while(isspace(fileblock[fp])) {
         fp++;
      }

      tiles_h = atoi(fileblock + fp);
      while(!isspace(fileblock[fp])) {
         fp++;
      }
      while(isspace(fileblock[fp])) {
         fp++;
      }

      screens_w = atoi(fileblock + fp);
      while(!isspace(fileblock[fp])) {
         fp++;
      }
      while(isspace(fileblock[fp])) {
         fp++;
      }

      screens_h = atoi(fileblock + fp);
      while(!isspace(fileblock[fp])) {
         fp++;
      }
      while(isspace(fileblock[fp])) {
         fp++;
      }

      resetConnections();
      while(fileblock[fp] == '+' && room.connection_count < ROOM_CONNECTION_MAX) {
         char *fstart = fileblock + fp + 1;
         int fcount = 0;
         while(!isspace(fileblock[fp])) {
            fp++;
            fcount++;
         }
         strncpy(room.filenames[room.connection_count], fstart, fcount);
         room.filenames[room.connection_count][fcount-1] = 0;
         room.connection_count++;
         if (connection) {
            if (strcmp(room.filenames[room.connection_count-1], room.roomname) == 0) {
               connection = room.connection_count;
            }
         }
         while(isspace(fileblock[fp])) {
            fp++;
         }
      }
      setRoomName(fname);
      while(fileblock[fp] == '+') {
         while(!isspace(fileblock[fp])) {
            fp++;
         }
         while(isspace(fileblock[fp])) {
            fp++;
         }
      }

      camera.bounds.x = 0;
      camera.bounds.y = 0;
      camera.bounds.w = (screens_w - 1) * field_w;
      camera.bounds.h = (screens_h - 1) * field_h;

      room.bounds.x = 0;
      room.bounds.y = 0;
      room.bounds.w = (screens_w) * field_w;
      room.bounds.h = (screens_h) * field_h;

      int tile_xc = field_w_tiles / tiles_w;
      int tile_yc = field_h_tiles / tiles_h;
      int rtw = tile_xc * tile_size;
      int rth = tile_yc * tile_size;
      int pitch = tiles_w * screens_w;
      int maxh = tiles_h * screens_h;
      int tilecount = pitch * maxh;
      int i = 0;
      char *block = (char*)malloc(tilecount);
      while (fp < size) {
         char c = fileblock[fp];
         if (!isspace(c)) {
            block[i++] = c;
         }
         fp++;
      }
      free(fileblock);
      i = 0;
      int reverse = 0;
      while (i < tilecount) {
         switch(block[i]) {
            case '@':
               {
                  if (!p1.active) {
                     int x = (i % pitch) * rtw + (0.5*rtw -7);
                     int y = (i / pitch) * rth + (0.5*rth -7);
                     p1 = createPlayer(x, y);
                  }
               } break;
            case 'l':
               {
                  int x = i % pitch;
                  int y = i / pitch;
                  int h = 1;
                  while (y + h < maxh) {
                     char ex = block[x + (y+h)*pitch];
                     if (ex == 'l') {
                        block[x + (y+h)*pitch] = '-';
                     } else if (ex == 'L') {
                        block[x + (y+h)*pitch] = '#';
                     } else {
                        break;
                     }
                     h++;
                  }
                  createTileAlignedLadder(x * tile_xc, y * tile_yc, tile_xc, h * tile_yc);
               } break;
            case 'L':
               reverse = 1;
            case '#':
               {
                  int rx = i % pitch;
                  int ry = i / pitch;
                  int rw = 1;
                  int rh = 1;
                  while (rx + rw < pitch) {
                     char ex = block[rx + rw + ry * pitch];
                     if (ex == '#' || ex == 'L') {
                        rw += 1;
                     } else {
                        break;
                     }
                  }
                  while (ry + rh < maxh) {
                     int expand = 1;
                     for (int x = rx; x < rx + rw; x++) {
                        char ex = block[x + (ry + rh)*pitch];
                        if (ex != '#' && ex != 'L') {
                           expand = 0;
                           break;
                        }
                     }
                     if (expand) {
                        rh += 1;
                     } else {
                        break;
                     }
                  }
                  for (int y = ry; y < ry + rh; y++) {
                     for (int x = rx; x < rx + rw; x++) {
                        if (block[x + y*pitch] == '#') {
                           block[x + y*pitch] = ' ';
                        } else {
                           block[x + y*pitch] = 'l';
                        }
                     }
                  }
                  createTileAlignedWall(rx * tile_xc, ry * tile_yc, rw * tile_xc, rh * tile_yc);
                  if (reverse) {
                     reverse = 0;
                     i--;
                  }
               } break;
            default:
               if (isdigit(block[i]) && block[i] != '0') {
                  char n = block[i];
                  block[i] = ' ';
                  int v = (n - '0') - 1;
                  int x = i % pitch;
                  int y = i / pitch;
                  if (x == 0) {
                     int h = 1;
                     int expand = 1;
                     while (expand && y + h < maxh) {
                        char c = block[(y+h)*pitch];
                        if (c == n) {
                           block[(y+h)*pitch] = ' ';
                           h += 1;
                        } else {
                           expand = 0;
                        }
                     }
                     rect res = makeTileAlignedRect(-1, y * tile_yc, 2, h * tile_yc);
                     room.connections[v] = res;
                     if (v + 1 == connection) {
                        p1.position.x = res.x + room.transition_offset.x;
                        p1.position.y = res.y + room.transition_offset.y;
                     }
                  } else if (y == 0) {
                     int w = 1;
                     int expand = 1;
                     while (expand && x + w < pitch) {
                        int offset = x + w + (y)*pitch;
                        char c = block[offset];
                        //printf("%c\n", c);
                        if (c == n) {
                           block[offset] = ' ';
                           w += 1;
                        } else {
                           expand = 0;
                        }
                     }
                     rect res = makeTileAlignedRect(x * tile_xc, -1, w * tile_xc, 2);
                     room.connections[v] = res;
                     if (v + 1 == connection) {
                        p1.position.x = res.x + room.transition_offset.x;
                        p1.position.y = res.y + room.transition_offset.y;
                     }
                  } else if (x == pitch-1) {
                     int h = 1;
                     int expand = 1;
                     while (expand && y + h < maxh) {
                        char c = block[pitch - 1 + (y+h)*pitch];
                        if (c == n) {
                           block[pitch - 1 + (y+h)*pitch] = ' ';
                           h += 1;
                        } else {
                           expand = 0;
                        }
                     }
                     rect res = makeTileAlignedRect((pitch-1) * tile_xc + 1, y * tile_yc, 2, h * tile_yc);
                     room.connections[v] = res;
                     if (v + 1 == connection) {
                        p1.position.x = res.x + room.transition_offset.x;
                        p1.position.y = res.y + room.transition_offset.y;
                     }
                  } else if (y == maxh-1) {
                     int w = 1;
                     int expand = 1;
                     while (expand && x + w < pitch) {
                        int offset = x + w + (y)*pitch;
                        char c = block[offset];
                        //printf("%c\n", c);
                        if (c == n) {
                           block[offset] = ' ';
                           w += 1;
                        } else {
                           expand = 0;
                        }
                     }
                     rect res = makeTileAlignedRect(x * tile_xc, (maxh - 1) * tile_yc + 1, w * tile_xc, 2);
                     room.connections[v] = res;
                     if (v + 1 == connection) {
                        p1.position.x = res.x + room.transition_offset.x;
                        p1.position.y = res.y + room.transition_offset.y;
                     }
                  }
               }
               break;
         }
         i++;
      }
      free(block);
   }
}

void reproject_screen(int w, int h)
{
   float scale = fmin((float)w / field_w, (float)h / field_h);
   projection.w = field_w * scale;
   projection.h = field_h * scale;
   projection.x = (w - projection.w)/2;
   projection.y = (h - projection.h)/2;
}

int main(int argc, char ** argv)
{
   Uint64 step_size = secondsToPCF(0.01);
   Uint64 next_step = SDL_GetPerformanceCounter() + step_size;
   SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);
   atexit(SDL_Quit);
   win = SDL_CreateWindow("figjam", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, start_w, start_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
   ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
   pixelbuffer = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, field_w, field_h);
   reproject_screen(start_w, start_h);

   testsprite st = createTestSprite(10, 10, 255, 255, 0);

#if 0
   p1 = createPlayer(100, field_h/2);

#if 0
   createTileAlignedWall(2, 2, 1, 1);
   createTileAlignedWall(2, 4, 1, 1);
#endif
   createTileAlignedWall(0, field_h_tiles - 1, field_w_tiles, 1);
   createTileAlignedWall(0, 0, field_w_tiles, 1);
   createTileAlignedWall(field_w_tiles - 1, 0, 1, field_h_tiles);
   createTileAlignedWall(0, 0, 1, field_h_tiles);
#endif
   loadLevel("startroom.txt", 0);

   float t;
   float angle = 0.f;
   rect test = makeRect(field_w/2, field_h/2, 16, 16);
   rect res = test;
   v2 testvel;
   
   joy = SDL_JoystickOpen(0);
   if (joy) {
      con.left = bindAxis(0, -1);
      con.right = bindAxis(0, 1);
      con.up = bindAxis(1, -1);
      con.down = bindAxis(1, 1);
      con.jump = bindButton(0);
   } else {
      con.left = bindKey(SDLK_LEFT);
      con.right = bindKey(SDLK_RIGHT);
      con.up = bindKey(SDLK_UP);
      con.down = bindKey(SDLK_DOWN);
      con.jump = bindKey(SDLK_d);
   }

   while (running) {
      startControlFrame();
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
         switch (e.type) {
            case SDL_QUIT:
               running = 0;
               break;
            case SDL_WINDOWEVENT:
               if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                  reproject_screen(e.window.data1, e.window.data2);
               }
               break;
            case SDL_KEYDOWN:
               if (e.key.keysym.sym == SDLK_ESCAPE) {
                  running = false;
               }
            case SDL_KEYUP:
            case SDL_JOYAXISMOTION:
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
               fireControlEvent(&e);
               break;
            default:
               break;
         }
      }
      SDL_SetRenderTarget(ren, pixelbuffer);
      SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
      SDL_RenderClear(ren);


      tickPlayer(&p1);


      SDL_SetRenderDrawColor(ren, 0, 255, 255, 255);
      debugDrawWalls(ren);
      drawLadders();
      drawPlayer(&p1);
      drawConnections();
      //drawing goes here
      SDL_SetRenderTarget(ren, 0);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, pixelbuffer, 0, &projection);
      SDL_RenderPresent(ren);

      int lload = 0;
      if (!pointInRect(&room.bounds, &p1.position)) {
         for (int i = 0; i < ROOM_CONNECTION_MAX; i++) {
            if (pointInRect(&room.connections[i], &p1.position)) {
               lload = i + 1;
               room.transition_offset.x = p1.position.x - room.connections[i].x;
               room.transition_offset.y = p1.position.y - room.connections[i].y;
               break;
            }
         }
      }
      if (lload > 0) {
         char buf[RC_FILE_MAX];
         strncpy(buf, room.filenames[lload - 1], RC_FILE_MAX);
         //printf("going to %s\n", buf);
         loadLevel(buf, 1);
      }

      while (SDL_GetPerformanceCounter() < next_step) {
         SDL_Delay(1);
      }
      next_step = SDL_GetPerformanceCounter() + step_size;
      frame++;
   }
}

