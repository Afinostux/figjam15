
#define tile_size 8

#ifdef _WIN32
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <cfloat>
#include <cstring>
#include <ctime>
#include "SDL/SDL.h"
#include "SDL/SDL_mixer.h"
#include "SDL/SDL_image.h"
#undef main
#else
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <float.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#endif

// NOTE(afox): what the hell man
#define PHYS_EPSILON sqrt(FLT_EPSILON) * 100

SDL_Window *win;
SDL_Renderer *ren;
SDL_Texture *pixelbuffer;
SDL_Joystick *joy;
SDL_Rect projection;
enum songstates {
   ss_silent,
   ss_leveltheme,
   ss_bosstheme,
   ss_victory
};
int songstate;
int running = 1;
int frame;
void loadLevel(const char * fname, int connection);

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
#define tc_erase(lcs, ind) {if (tc_inarray(lcs, ind)) { *tc_at(lcs, ind) = *tc_back(lcs); countof(lcs)--;}}

#define field_w 320
#define field_h 240
#define field_w_tiles field_w/tile_size
#define field_h_tiles field_h/tile_size
#define start_w 640
#define start_h 480

#if 0
int min(int a, int b)
{
   return (a < b)?a:b;
}

int max(int a, int b)
{
   return (a > b)?a:b;
}
#endif

struct {
   SDL_Texture *saber;
   SDL_Texture *robots;
   SDL_Texture *wall;
   SDL_Texture *stone;
   SDL_Texture *effect;
   SDL_Texture *mirv;
   SDL_Texture *ladder;
} tex;

struct {
   Mix_Chunk *hit;
   Mix_Chunk *mirv_engine;
   Mix_Chunk *mirv_hit;
   Mix_Chunk *mirv_die;
   Mix_Chunk *mirv_shotgun;
   Mix_Chunk *reflect;
   Mix_Chunk *rock_break;
   Mix_Chunk *saber_die;
   Mix_Chunk *saber_hit;
   Mix_Chunk *saber_jump;
   Mix_Chunk *saber_heal;
   Mix_Chunk *saber_shoot;
   Mix_Chunk *spider_hit;
   Mix_Chunk *spider_shoot;
} sound;

void play(Mix_Chunk *ch)
{
   Mix_PlayChannel(-1, ch, 0);
}

struct {
   Mix_Music *mirv_theme;
   Mix_Music *level_theme;
} music;

SDL_Texture* loadTexture(const char* file)
{
   SDL_Surface *lsrf = IMG_Load(file);
   SDL_Texture *tex;
   if (lsrf) {
      tex = SDL_CreateTextureFromSurface(ren, lsrf);
      SDL_FreeSurface(lsrf);
   }
   return tex;
}

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

int rectInRoom(rect *r)
{
   return (r->x <= room.bounds.w && r->y <= room.bounds.h && r->x + r->w >= 0 && r->y + r->h >= 0);
}

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

   if (tstart < tend + PHYS_EPSILON) {
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

int rectOnScreen(rect *r)
{
   return (r->x + r->w > camera.position.x && r->y + r->h > camera.position.y &&
         r->x < camera.position.x + field_w && r->y < camera.position.y + field_h);
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
   SDL_Rect lrect;
   lrect.w = lrect.h = 16;
   for (int i = 0; i < countof(ladder); i++) {
      ladder *l = tc_at(ladder, i);
      if (rectOnScreen(&l->bounds)) {
         lrect.x = l->bounds.x - camera.position.x;
         int max = l->bounds.y + l->bounds.h;
         for (int y = l->bounds.y; y < max; y += 16) {
            lrect.y = y - camera.position.y;
            SDL_RenderCopy(ren, tex.ladder, 0, &lrect);
         }
      }
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

int rectAgainstWall(rect *mr)
{
   rect b = expandRect(mr, -0.5);
   b.x -= 1;
   b.w += 2;
   return rectIntersectsWalls(&b);
}

int rectOnGround(rect *mr)
{
   rect b = expandRect(mr, -0.5);
   b.y += 1;
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
         v2 clip_velocity = frame_vel * clip_time;
         frame_displacement = frame_displacement + clip_velocity;
         frame_vel = frame_vel * (1.f - (clip_time));
         bounds.x += clip_velocity.x;
         bounds.y += clip_velocity.y;
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

struct asprite {
   SDL_Texture *tex;
   int framecount;
   int pitch;
   int w, h;
};

asprite createAsprite(SDL_Texture *tex, int frame_w, int frame_h)
{
   asprite res;
   res.tex = tex;
   res.w = frame_w;
   res.h = frame_h;
   int texw, texh;
   SDL_QueryTexture(tex, 0, 0, &texw, &texh);
   res.pitch = texw / frame_w;
   res.framecount = res.pitch * (texh / frame_h);
   return res;
}

void drawAnimatingAsprite(asprite *sp, float x, float y, int frame_start, int frame_count, float *time, int flip)
{
   int barrier;
   if (frame_start + frame_count > sp->framecount) {
      barrier = sp->framecount;
   } else  {
      barrier = frame_start + frame_count;
   }
   if (frame_start + ceil(*time) > barrier) {
      *time = 0;
   }
   if (*time < 0.f) {
      *time = (barrier - frame_start) - 0.01;
   }
   int frame = frame_start + floor(*time);
   if (frame > barrier) {
      frame = frame_start;
   }
   SDL_Rect src;
   SDL_Rect dest;
   src.x = (frame % sp->pitch) * sp->w;
   src.y = (frame / sp->pitch) * sp->h;
   dest.x = floor(x - camera.position.x);
   dest.y = floor(y - camera.position.y);
   src.w = dest.w = sp->w;
   src.h = dest.h = sp->h;
   SDL_Point ofs = {sp->w * 0.5, sp->h * 0.5};
   if (flip) { 
      flip = SDL_FLIP_HORIZONTAL;
   }
   SDL_RenderCopyEx(ren, sp->tex, &src, &dest, 0, &ofs, (SDL_RendererFlip)flip);
}

void drawAspriteFrame(asprite *sp, float x, float y, int frame, int flip)
{
   frame = frame % sp->framecount;
   SDL_Rect src;
   SDL_Rect dest;
   src.x = (frame % sp->pitch) * sp->w;
   src.y = (frame / sp->pitch) * sp->h;
   dest.x = floor(x - camera.position.x);
   dest.y = floor(y - camera.position.y);
   src.w = dest.w = sp->w;
   src.h = dest.h = sp->h;
   SDL_Point ofs = {sp->w * 0.5, sp->h * 0.5};
   if (flip) { 
      flip = SDL_FLIP_HORIZONTAL;
   }
   SDL_RenderCopyEx(ren, sp->tex, &src, &dest, 0, &ofs, (SDL_RendererFlip)flip);
}

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
   control *fire;
   control *reset;
} con;

struct effect {
   asprite spr;
   v2 offset;
   v2 position;
   v2 velocity;
   int timer;
   int timer_start;
   int frame_start;
   int frame_end;
};

tc_create(effect, effect, 32);

void createEffect(SDL_Texture *t, v2 position, v2 velocity, int w, int h, int framestart, int frameend, int time)
{
   effect *e = tc_new(effect);
   if (e) {
      e->spr = createAsprite(t, w, h);
      e->offset = makev2(-w/2, -h/2);
      e->position = position;
      e->velocity = velocity;
      e->timer_start = e->timer = time;
      e->frame_start = framestart;
      e->frame_end = frameend;
   }
}

void effect_smalldie(v2 position)
{
   createEffect(tex.effect, position, makev2(0,0), 16, 16, 4, 6, 10);
}

void effect_explode(v2 position)
{
   float step = M_PI / 4;
   for (int i = 0; i < 8; i++) {
      v2 vel = makeRotatedV2(0, 1.3, step * i);
      v2 pos = position + makeRotatedV2(4, 0, step * i);
      createEffect(tex.effect, pos, vel, 16, 16, 4, 6, 20);
   }
}

void effect_explode_large(v2 position)
{
   float step = M_PI / 4;
   for (int i = 0; i < 8; i++) {
      v2 vel = makeRotatedV2(0, 0.1, step * i);
      v2 pos = position + makeRotatedV2(4, 0, step * i);
      createEffect(tex.effect, pos, vel, 16, 16, 12, 15, 100);
   }
   for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 8; i++) {
         v2 vel = makeRotatedV2(0, 0.3 + 0.3 * j, step * i);
         v2 pos = position + makeRotatedV2(4, 0, step * i);
         createEffect(tex.effect, pos, vel, 16, 16, 4, 6, 60 - 20 * j);
      }
   }
}

void drawEffects()
{
   for (int i = 0; i < countof(effect); i++) {
      effect *e = tc_at(effect, i);
      if (e->timer == 0) {
         tc_erase(effect, i);
         i--;
         continue;
      }
      e->timer -= 1;
      float t = (float)(e->timer_start - e->timer)/e->timer_start;
      int frame = floor(((float)e->frame_start + 0.5)*(1 - t) + ((float)e->frame_end + 0.5)*(t));
      e->position = e->position + e->velocity;
      v2 drawpos = e->position + e->offset;
      drawAspriteFrame(&e->spr, drawpos.x, drawpos.y, frame, 0);
   }
}

struct p_shot {
   rect worldbounds;
   asprite spr;
   v2 position;
   v2 velocity;
   int last_bounds_frame;
};

tc_create(p_shot, pshot, 3);

rect* getPshotBounds(p_shot *p)
{
   if (p->last_bounds_frame != frame) {
      float width = 4;
      float height = 4;
      p->worldbounds.x = p->position.x - 0.5*width;
      p->worldbounds.y = p->position.y - 0.5*height;
      p->worldbounds.w = width;
      p->worldbounds.h = height;
   }
   return &p->worldbounds;
}

void firePshot(float x, float y, float hspeed)
{
   p_shot *shot = tc_new(pshot);
   if (shot) {
      play(sound.saber_shoot);
      shot->spr = createAsprite(tex.saber, 16, 16);
      shot->position.x = x;
      shot->position.y = y;
      shot->velocity.y = 0;
      shot->velocity.x = hspeed;
      shot->last_bounds_frame = frame - 1;
   }
}

void stepPshots()
{
   for (int i = 0; i < countof(pshot); i++) {
      p_shot *shot = tc_at(pshot, i); 
      shot->position = shot->velocity + shot->position;
   }
   for (int i = 0; i < countof(pshot);) {
      p_shot *shot = tc_at(pshot, i); 
      if (shot->position.x < camera.position.x || shot->position.x > camera.position.x + field_w) {
         tc_erase(pshot, i);
         continue;
      }
      i++;
   }
}

void drawPshots()
{
   float ofs_x = -8;
   float ofs_y = -8;
   for (int i = 0; i < countof(pshot); i++) {
      p_shot *shot = tc_at(pshot, i); 
      shot->position = shot->velocity + shot->position;
      drawAspriteFrame(&shot->spr, shot->position.x + ofs_x, shot->position.y + ofs_y, 3, (shot->velocity.x < 0.f));
   }
}

p_shot* clipWithPshots(rect *r)
{
   for (int i = 0; i < countof(pshot); i++) {
      p_shot *shot = tc_at(pshot, i); 
      if (rectsOverlap(r, getPshotBounds(shot))) {
         return shot;
      }
   }
   return 0;
}

struct player {
   v2 position;
   v2 velocity;
   rect worldbounds;
   float w, h;
   float frame;
   asprite spr;
   int active;
   int flip;
   int alive;
   int jumping;
   int onladder;
   int accept_ladder;
   int last_bounds_frame;
   int hitpoints;
   int hurt_timer;
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
   res.spr = createAsprite(tex.saber, 16, 16);
   res.hitpoints = 100;
   return res;
}

void hurtPlayer(float vx, float vy, int amount)
{
   if (p1.hurt_timer == 0) {
      play(sound.saber_hit);
      p1.hitpoints = max(p1.hitpoints - amount, 0);
      p1.velocity.x = vx;
      p1.velocity.y = vy;
      p1.hurt_timer = 200;
      if (!rectIntersectsWalls(getPlayerBounds(&p1))) {
         p1.onladder = 0;
      }
   }
}

void healPlayer(int amount)
{
   play(sound.saber_heal);
   p1.hitpoints = min(p1.hitpoints + amount, 100);
}

int player_hurt_threshold = 180;
void tickPlayer(player *p)
{
   float player_accel = 0.1;
   float player_wspeed = 1.5;
   float player_gravity = 0.09;
   float player_jump = 4.5;
   float player_shot_speed = 2.5;
   int player_jump_grace = 20;
   if (p->alive) {
      if (p->hurt_timer > 0) {
         p->hurt_timer -= 1;
      }
      if (p->hurt_timer > player_hurt_threshold) {
         if (rectOnGround(getPlayerBounds(p))) {
            p->velocity.x = fapproach(p->velocity.x, 0, player_accel);
         }
         if (!p->onladder) {
            p->velocity.y += player_gravity;
            v2 frame_displacement;
            getMotionWalled(getPlayerBounds(p), &p->velocity, &p->velocity, &frame_displacement);
            p->position = p->position + frame_displacement;
         }
      } else {
         if (p->hitpoints == 0) {
            p->alive = 0;
            play(sound.saber_die);
            effect_explode_large(p->position);
            p->hurt_timer = 300;
         }
         if (con.fire->pressed) {
            if (con.left->held) {
               firePshot(p->position.x, p->position.y, -player_shot_speed);
            } else if (con.right->held) {
               firePshot(p->position.x, p->position.y, player_shot_speed);
            } else {
               if (p->flip) {
                  firePshot(p->position.x, p->position.y, -player_shot_speed);
               } else {
                  firePshot(p->position.x, p->position.y, player_shot_speed);
               }
            }
         }
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
                  play(sound.saber_jump);
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
            if (!p->accept_ladder) {
               p->accept_ladder = (con.up->pressed || con.down->pressed) || (con.up->held && con.jump->pressed);
            } else {
               if (con.up->released || con.down->released) {
                  p->accept_ladder = 0;
               }
            }
            if (p->accept_ladder) {
               if (rectIntersectsLadders(getPlayerBounds(p))) {
                  p->accept_ladder = 0;
                  p->onladder = 1;
               }
            }
            if (p->velocity.x > 0) {
               p->flip = 0;
            } else if (p->velocity.x < 0) {
               p->flip = 1;
            }
         }
      }
   } else {
      if (p->hurt_timer > 0) {
         p->hurt_timer -= 1;
      } else {
         loadLevel("startroom.txt", 0);
      }
   }
   setCameraFocus(&p->position);
}

void drawPlayer(player *p)
{
   if (!p->alive) {
      return;
   }
   SDL_SetRenderDrawColor(ren, 255, 255, 100, 255);
   float ofs_x = -8;
   float ofs_y = -9;
   if (p->hurt_timer > player_hurt_threshold) {
      p->frame += 0.6;
      drawAnimatingAsprite(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 1, 2, &p->frame, p->flip);
   } else {
      if (!((p->hurt_timer / 2)%2)) {
         if (!p->onladder) {
            if (fabs(p->velocity.x) > 0.1) {
               p->frame += 0.2;
               drawAnimatingAsprite(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 4, 4, &p->frame, p->flip);
            } else {
               drawAspriteFrame(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 0, p->flip);
            }
         } else {
            if (con.up->held) {
               p->frame += 0.1;
               drawAnimatingAsprite(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 8, 4, &p->frame, p->flip);
            } else if (con.down->held) {
               p->frame -= 0.1;
               drawAnimatingAsprite(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 8, 4, &p->frame, p->flip);
            } else {
               drawAspriteFrame(&p->spr, p->position.x + ofs_x, p->position.y + ofs_y, 8, p->flip);
            }
         }
      }
   }

   SDL_Rect healthrect;
   SDL_Rect healthbar;
   healthrect.x = healthbar.x = 4;
   healthrect.y = healthbar.y = 4;
   healthrect.w = healthbar.w = 4;
   healthrect.h = 100;
   healthbar.h = p->hitpoints;
   healthbar.y += healthrect.h - healthbar.h;
   SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
   SDL_RenderFillRect(ren, &healthrect);
   SDL_SetRenderDrawColor(ren, 100, 255, 100, 255);
   SDL_RenderFillRect(ren, &healthbar);
   SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
   SDL_RenderDrawRect(ren, &healthrect);
}

struct boulderboss {
   wall *blocker;
   asprite spr;
   int hitpoints;
};

tc_create(boulderboss, boulder, 1);

rect* getBoulderBounds(boulderboss *bb)
{
   return (&bb->blocker->bounds);
}

void createBoulder(float x, float y)
{
   boulderboss *bb = tc_new(boulder);
   if (bb) {
      createWall(x, y + 32, 64, 32);
      bb->blocker = tc_back(wall);
      bb->hitpoints = 20;
      bb->spr = createAsprite(tex.stone, 64, 64);
   }
}

struct dozermob {
   asprite spr;
   int hitpoints;
   v2 position;
   v2 velocity;
   float frame;
   int flip;
   int flipping;
   int state_timer;
   int active;
};

tc_create(dozermob, dozer, 16);

void createDozer(float x, float y, int flip)
{
   dozermob *dz = tc_new(dozer);
   if (dz) {
      dozermob blank = {};
      *dz = blank;
      dz->spr = createAsprite(tex.robots, 16, 16);
      dz->position = makev2(x, y);
      dz->velocity = makev2(0, 0);
      dz->flip = flip; 
      dz->hitpoints = 2;
      dz->flipping = 0;
   }
}

struct bulletmob {
   asprite spr;
   int hitpoints;
   v2 position;
   float altitude;
   float frame;
   v2 velocity;
   int flip;
   int flipping;
   int active;
};

tc_create(bulletmob, bullet, 16);

void createBulletMob(float x, float y, int flip)
{
   bulletmob * b = tc_new(bullet);
   if (b) {
      bulletmob blank = {};
      *b = blank;
      b->spr = createAsprite(tex.robots, 16, 16);
      b->hitpoints = 2;
      b->position = makev2(x, y);
      b->velocity = makev2(0, 0);
      b->flip = flip;
      b->altitude = y;
      b->velocity.y = 0.6;
   }
};

struct saucermob {
   asprite spr;
   v2 position;
   v2 seek_vel;
   int state_timer;
   int active;
   int hitpoints;
   float frame;
};

tc_create(saucermob, saucer, 16);

void createSaucerMob(float x, float y)
{
   saucermob *s = tc_new(saucer);
   if (s) {
      saucermob blank = {};
      *s = blank;
      s->spr = createAsprite(tex.robots, 16, 16);
      s->position = makev2(x, y);
      s->hitpoints = 4;
   }
}

struct spidermob {
   asprite spr;
   v2 position;
   int flip;
   int shot_timer;
   int active;
   int hitpoints;
   float frame;
};

tc_create(spidermob, spider, 16);

struct slaser {
   asprite spr;
   v2 position;
   float hspeed;
};

tc_create(slaser, slaser, 32);

void fireSmallLaser(float x, float y, float hspeed)
{
   slaser *sl = tc_new(slaser);
   if (sl) {
      play(sound.spider_shoot);
      sl->spr = createAsprite(tex.robots, 16, 16);
      sl->position = makev2(x, y);
      sl->hspeed = hspeed;
   }
}

void createSpiderMob(float x, float y, int flip)
{
   spidermob *sp = tc_new(spider);
   if (sp) {
      spidermob blank = {};
      *sp = blank;
      sp->spr = createAsprite(tex.robots, 16, 16);
      sp->position = makev2(x, y);
      sp->flip = flip;
      sp->hitpoints = 3;
   }
}

struct item {
   asprite spr;
   v2 position;
   int healamt;
   int frame[2];
   int timer;
};

tc_create(item, item, 8);

void createItem(float x, float y, int islarge, int infinite)
{
   item *it = tc_new(item);
   if (it) {
      it->spr = createAsprite(tex.saber, 16, 16);
      it->position = makev2(x, y);
      if (infinite) {
         it->timer = -1;
      } else {
         it->timer = 600;
      }
      if (islarge) {
         it->frame[0] = 14;
         it->frame[1] = 15;
         it->healamt = 40;
      } else {
         it->frame[0] = 12;
         it->frame[1] = 13;
         it->healamt = 10;
      }
   }
}

void randomDrop(v2 position)
{
   int dice = rand() % 64;
   if (dice < 8) {
      if (dice%4) {
         createItem(position.x, position.y, 0, 0);
      } else {
         createItem(position.x, position.y, 1, 0);
      }
   }
}

struct mirvrocket {
   asprite spr;
   v2 position;
   int direction;
   v2 velocity;
};

tc_create(mirvrocket, mirvr, 32);

void fireMirvRocket(float x, float y, float hs, float vs, int dir)
{
   mirvrocket *mr = tc_new(mirvr);
   if (mr) {
      mr->spr = createAsprite(tex.effect, 16, 16);
      mr->position = makev2(x, y);
      mr->velocity = makev2(hs, vs);
      mr->direction = dir;
   }
}

void tickEnemies()
{
   for (int i = 0; i < countof(boulder); ) {
      boulderboss *bb = tc_at(boulder, i);
      p_shot *s = clipWithPshots(getBoulderBounds(bb));
      if (s) {
         s->position.x = -1000;
         play(sound.hit);
         bb->hitpoints--;
         if (bb->hitpoints < 1) {
            bb->blocker->active = 0;
            v2 p = makev2(bb->blocker->bounds.x, bb->blocker->bounds.y) + makev2(32, 0);
            effect_explode_large(p);
            play(sound.rock_break);
            randomDrop(p);
            tc_erase(boulder, i);
            continue;
         }
      }
      i++;
   }
   for (int i = 0; i < countof(dozer); ) {
      dozermob *dz = tc_at(dozer, i);
      if (dz->hitpoints < 1) {
         effect_smalldie(dz->position);
         randomDrop(dz->position);
         tc_erase(dozer, i);
         continue;
      }
      rect dozerbounds = makeRect(dz->position.x - 4, dz->position.y - 6, 8, 12);
      dz->active = rectOnScreen(&dozerbounds);
      if (dz->active) {
         if (dz->flipping) {
            dz->state_timer -= 1;
            if (dz->state_timer < 1) {
               dz->flipping = 0;
               dz->flip = !dz->flip;
            }
            dz->velocity.x = fapproach(dz->velocity.x, 0, 0.1);
            v2 displacement;
            getMotionWalled(&dozerbounds, &dz->velocity, &dz->velocity, &displacement);
            dz->position = dz->position + displacement;
         } else {
            rect edgesensor;
            if (dz->flip) {
               dz->velocity.x = fapproach(dz->velocity.x, -1, 0.1);
               edgesensor = makeRect(dz->position.x - 2 - 16, dz->position.y, 4, 9);
            } else {
               dz->velocity.x = fapproach(dz->velocity.x, 1, 0.1);
               edgesensor = makeRect(dz->position.x - 2 + 16, dz->position.y, 4, 9);
            }
            v2 displacement;
            getMotionWalled(&dozerbounds, &dz->velocity, &dz->velocity, &displacement);
            dz->position = dz->position + displacement;
            if (fabs(displacement.x) <= PHYS_EPSILON || !rectIntersectsWalls(&edgesensor)) {
               dz->state_timer = 50;
               dz->flipping = 1;
            }
         }
         p_shot *bullet = clipWithPshots(&dozerbounds);
         if (bullet) {
            bullet->position.x = -1000;
            if (dz->flip) {
               if (bullet->velocity.x < 0) {
                  dz->hitpoints -= 1;
                  play(sound.hit);
               } else {
                  play(sound.reflect);
               }
            } else {
               if (bullet->velocity.x > 0) {
                  dz->hitpoints -= 1;
                  play(sound.hit);
               } else {
                  play(sound.reflect);
               }
            }
         }
         if (rectsOverlap(&dozerbounds, getPlayerBounds(&p1))) {
            if (p1.position.x > dz->position.x) {
               if (dz->flip) {
                  hurtPlayer(1, -1, 10);
               } else {
                  hurtPlayer(2, -1, 10);
               }
            } else {
               if (!dz->flip) {
                  hurtPlayer(-1, -1, 10);
               } else {
                  hurtPlayer(-2, -1, 10);
               }
            }
         }
      }
      i++;
   }
   for (int i = 0; i < countof(bullet); ) {
      bulletmob *b = tc_at(bullet, i);
      if (b->hitpoints < 1) {
         effect_smalldie(b->position);
         randomDrop(b->position);
         tc_erase(bullet, i);
         continue;
      }
      rect bulletbounds = makeRect(b->position.x - 4, b->position.y - 4, 8, 8);
      b->active = rectOnScreen(&bulletbounds);
      if (b->active) {
         if (b->flipping) {
            b->velocity.x = fapproach(b->velocity.x, 0, 0.04);
            if (b->velocity.x == 0.f) {
               b->flipping = 0;
               b->flip = !b->flip;
            }
         } else {
            rect bulletsensor = bulletbounds;
            if (b->flip) {
               b->velocity.x = fapproach(b->velocity.x, -2, 0.01);
               bulletsensor.x -= 64;
            } else {
               b->velocity.x = fapproach(b->velocity.x, 2, 0.01);
               bulletsensor.x += 64;
            }
            if (rectIntersectsWalls(&bulletsensor)) {
               b->flipping = 1;
            }
         }
         if (b->position.y < b->altitude) {
            b->velocity.y += 0.01;
         } else {
            b->velocity.y -= 0.01;
         }
         v2 displacement;
         getMotionWalled(&bulletbounds, &b->velocity, &b->velocity, &displacement);
         b->position = b->position + displacement;
         p_shot *bullet = clipWithPshots(&bulletbounds);
         if (bullet) {
            bullet->position.x = -1000;
            play(sound.hit);
            b->hitpoints -= 1;
         }
         if (rectsOverlap(&bulletbounds, getPlayerBounds(&p1))) {
            if (p1.position.x > b->position.x) {
               if (b->flip) {
                  hurtPlayer(1, -1, 10);
               } else {
                  hurtPlayer(2, -1, 10);
               }
            } else {
               if (!b->flip) {
                  hurtPlayer(-1, -1, 10);
               } else {
                  hurtPlayer(-2, -1, 10);
               }
            }
         }
      }
      i++;
   }
   for (int i = 0; i < countof(saucer); ) {
      saucermob *s = tc_at(saucer, i);
      if (s->hitpoints < 1) {
         effect_smalldie(s->position);
         randomDrop(s->position);
         tc_erase(saucer, i);
         continue;
      }
      rect saucerbounds = makeRect(s->position.x - 6, s->position.y - 4, 12, 8);
      s->active = rectOnScreen(&saucerbounds);
      if (s->active) {
         s->state_timer += 1;
         if (s->state_timer < 75) {
            s->seek_vel = p1.position - s->position;
            s->seek_vel = normalizev2(&s->seek_vel);
         } else if (s->state_timer < 150) {
            s->position = s->position + s->seek_vel;
         } else {
            s->state_timer = 0;
         }
         p_shot *bullet = clipWithPshots(&saucerbounds);
         if (bullet) {
            bullet->position.x = -1000;
            play(sound.hit);
            s->hitpoints -= 1;
         }
         if (rectsOverlap(&saucerbounds, getPlayerBounds(&p1))) {
            if (p1.position.x > s->position.x) {
               hurtPlayer(1, -1, 10);
            } else {
               hurtPlayer(-1, -1, 10);
            }
         }
      }
      i++;
   }
   for (int i = 0; i < countof(slaser); ) {
      slaser *sl = tc_at(slaser, i);
      rect laserbounds = makeRect(sl->position.x - 6, sl->position.y - 2, 12, 4);
      if (rectsOverlap(&laserbounds, getPlayerBounds(&p1))) {
         hurtPlayer(sl->hspeed, -1, 20);
         play(sound.spider_hit);
         effect_explode(sl->position);
         tc_erase(slaser, i);
         continue;
      }
      sl->position.x += sl->hspeed;
      if (!rectInRoom(&laserbounds)) {
         tc_erase(slaser, i);
         continue;
      }
      i++;
   }
   for (int i = 0; i < countof(mirvr); ) {
      mirvrocket *mr = tc_at(mirvr, i);
      rect rocketbounds = makeRect(mr->position.x - 4, mr->position.y - 4, 8, 8);
      if (rectsOverlap(&rocketbounds, getPlayerBounds(&p1))) {
         hurtPlayer(mr->velocity.x, -2, 20);
         effect_explode(mr->position);
         tc_erase(mirvr, i);
         continue;
      }
      mr->position = mr->position + mr->velocity;
      if (!rectInRoom(&rocketbounds)) {
         tc_erase(mirvr, i);
         continue;
      }
      i++;
   }
   for (int i = 0; i < countof(spider); ) {
      spidermob *sp = tc_at(spider, i);
      if (sp->hitpoints < 1) {
         effect_smalldie(sp->position);
         randomDrop(sp->position);
         tc_erase(spider, i);
         continue;
      }
      rect spiderbounds = makeRect(sp->position.x - 6, sp->position.y - 6, 12, 12);
      sp->active = rectOnScreen(&spiderbounds);
      if (sp->active) {
         int range = 100;
         if (sp->shot_timer > 0) {
            sp->shot_timer -= 1;
         } else {
            rect edgesensor;
            rect playersensor = makeRect(sp->position.x, sp->position.y, range, 4);
            v2 fakevelocity;
            fakevelocity.y = 0;
            if (sp->flip) {
               edgesensor = makeRect(sp->position.x - 2 - 16, sp->position.y, 4, 12);
               playersensor.x -= range;
               fakevelocity.x = -0.15;
            } else {
               edgesensor = makeRect(sp->position.x - 2 + 16, sp->position.y, 4, 12);
               fakevelocity.x = 0.15;
            }
            v2 displacement;
            getMotionWalled(&spiderbounds, &fakevelocity, 0, &displacement);
            sp->position = sp->position + displacement;
            if (rectsOverlap(&playersensor, getPlayerBounds(&p1))) {
               sp->shot_timer = 50;
               if (sp->flip) {
                  fireSmallLaser(sp->position.x, sp->position.y, -4);
               } else {
                  fireSmallLaser(sp->position.x, sp->position.y,  4);
               }
            } else {
               if (fabs(displacement.x) <= PHYS_EPSILON || !rectIntersectsWalls(&edgesensor)) {
                  sp->flip = !sp->flip;
               }
            }
         }
         p_shot *bullet = clipWithPshots(&spiderbounds);
         if (bullet) {
            bullet->position.x = -1000;
            play(sound.hit);
            sp->hitpoints -= 1;
         }
         if (rectsOverlap(&spiderbounds, getPlayerBounds(&p1))) {
            if (p1.position.x > sp->position.x) {
               hurtPlayer(1, -1, 10);
            } else {
               hurtPlayer(-1, -1, 10);
            }
         }
      }
      i++;
   }
   for (int i = 0; i < countof(item); i++) {
      item *it = tc_at(item, i);
      rect itembounds = makeRect(it->position.x - 4, it->position.y - 8, 8, 16);
      if (rectsOverlap(&itembounds, getPlayerBounds(&p1))) {
         if (p1.hitpoints < 100) {
            healPlayer(it->healamt);
            tc_erase(item, i);
            i--;
            continue;
         }
      }
      if (it->timer >= 0) {
         if (it->timer == 0 || !rectOnScreen(&itembounds)) {
            tc_erase(item, i);
            i--;
            continue;
         }
         it->timer--;
      }
      v2 fakevelocity = makev2(0, 4);
      v2 displacement;
      getMotionWalled(&itembounds, &fakevelocity, 0, &displacement);
      it->position = it->position + displacement;
   }
}

void drawEnemies()
{
   for (int i = 0; i < countof(boulder); i++) {
      boulderboss *bb = tc_at(boulder, i);
      drawAspriteFrame(&bb->spr, bb->blocker->bounds.x, bb->blocker->bounds.y - 32, 0, 0);
   }
   for (int i = 0; i < countof(dozer); i++) {
      dozermob *dz = tc_at(dozer, i);
      if (!dz->active) {
         continue;
      }
      if (!dz->flipping) {
         dz->frame += 0.1;
         drawAnimatingAsprite(&dz->spr, dz->position.x - 8, dz->position.y - 8, 8, 2, &dz->frame, dz->flip);
      } else {
         drawAspriteFrame(&dz->spr, dz->position.x - 8, dz->position.y - 8, 10, dz->flip);
      }
   }
   for (int i = 0; i < countof(bullet); i++) {
      bulletmob *b = tc_at(bullet, i);
      if (!b->active) {
         continue;
      }
      if (!b->flipping) {
         b->frame += 0.20;
         drawAnimatingAsprite(&b->spr, b->position.x - 8, b->position.y - 8, 4, 3, &b->frame, b->flip);
      } else {
         drawAspriteFrame(&b->spr, b->position.x - 8, b->position.y - 8, 7, b->flip);
      }
   }
   for (int i = 0; i < countof(saucer); i++) {
      saucermob *s = tc_at(saucer, i);
      if (!s->active) {
         continue;
      }
      s->frame += 0.10;
      drawAnimatingAsprite(&s->spr, s->position.x - 8, s->position.y - 8, 0, 4, &s->frame, 0);
   }
   for (int i = 0; i < countof(slaser); i++) {
      slaser *sl = tc_at(slaser, i);
      drawAspriteFrame(&sl->spr, sl->position.x - 8, sl->position.y - 8, 11, sl->hspeed < 0.f);
   }
   for (int i = 0; i < countof(spider); i++) {
      spidermob *sp = tc_at(spider, i);
      if (!sp->active) {
         continue;
      }
      if (sp->shot_timer) {
         if (sp->shot_timer > 40) {
            drawAspriteFrame(&sp->spr, sp->position.x - 8, sp->position.y - 8, 15, sp->flip);
         } else {
            drawAspriteFrame(&sp->spr, sp->position.x - 8, sp->position.y - 8, 12, sp->flip);
         }
      } else {
         sp->frame += 0.05;
         drawAnimatingAsprite(&sp->spr, sp->position.x - 8, sp->position.y - 8, 12, 3, &sp->frame, sp->flip);
      }
   }
   for (int i = 0; i < countof(item); i++) {
      item *it = tc_at(item, i);
      if (it->timer > 100 || it->timer < 0) {
         drawAspriteFrame(&it->spr, it->position.x - 8, it->position.y - 8, it->frame[(frame/8)%2], 0);
      } else {
         if ((frame/8)%2) {
            drawAspriteFrame(&it->spr, it->position.x - 8, it->position.y - 8, it->frame[(frame/8)%2], 0);
         }
      }
   }
   for (int i = 0; i < countof(mirvr); i++) {
      mirvrocket *mr = tc_at(mirvr, i);
      drawAspriteFrame(&mr->spr, mr->position.x - 8, mr->position.y - 8, mr->direction, 0);
   }
}

enum mirv_actions {
   ma_entry,
   ma_taunt,
   ma_fly,
   ma_dive,
   ma_findland,
   ma_shotgun,
   ma_takeoff,
   ma_rise,
   ma_bomb,
   ma_count
};

struct mirv_s{
   asprite spr;
   v2 position;
   int active;
   int state; 
   int timer;
   int hurttimer;
   int hitpoints;
   int flip;
   float frame;
   float orbit;
   v2 velocity;
} mirv;

void startMirv(float x, float y)
{
   memset(&mirv, 0, sizeof(mirv_s));
   mirv.spr = createAsprite(tex.mirv, 32, 32);
   mirv.active = 1;
   mirv.position = makev2(x, y);
   mirv.hitpoints = 100;
}

void doMirv()
{
   if (mirv.active) {
      v2 drawpos = makev2(mirv.position.x - 16, mirv.position.y - 16);
      rect mirvbounds = makeRect(mirv.position.x - 8, mirv.position.y - 8, 16, 24);

      if (p1.position.x < mirv.position.x) {
         mirv.flip = 1;
      } else {
         mirv.flip = 0;
      }

      if (rectsOverlap(getPlayerBounds(&p1), &mirvbounds)) {
         if (mirv.flip) {
            hurtPlayer(-2, -4, 30);
         } else {
            hurtPlayer(2, -4, 30);
         }
      }


      if (!mirv.hurttimer) {
         p_shot *shot = clipWithPshots(&mirvbounds);
            if (shot) {
               shot->position.x = -1000;
               mirv.hitpoints = max(0, mirv.hitpoints - 5);
               mirv.hurttimer = 20;
            }
            if (!mirv.hitpoints) {
               play(sound.mirv_die);
               effect_explode_large(mirv.position);
               mirv.active = 0;
            }
      } else {
         mirv.hurttimer -= 1;
      }

      if (mirv.timer > 0) {
         mirv.timer--;
      }

      float gravity = 0.05;
      float hover = 150;

      switch (mirv.state) {
         case ma_entry:
            {
               mirv.velocity.y += gravity;
               if (mirv.hitpoints < 100) {
                  mirv.state = ma_taunt;
                  mirv.timer = 30;
               }
               drawAspriteFrame(&mirv.spr, drawpos.x, drawpos.y, 0, mirv.flip);
            }break;
         case ma_taunt:
            {
               mirv.velocity.y += gravity;
               if (!mirv.timer) {
                  play(sound.mirv_engine);
                  mirv.state = ma_takeoff;
                  mirv.orbit = mirv.position.x - 4;
               }
               drawAspriteFrame(&mirv.spr, drawpos.x, drawpos.y, 1, mirv.flip);
            }break;
         case ma_fly:
            {
               if ((frame/1000)%2) {
                  mirv.orbit = p1.position.x - 100;
               } else {
                  mirv.orbit = p1.position.x + 100;
               }
               if (mirv.position.x > mirv.orbit) {
                  mirv.velocity.x = fapproach(mirv.velocity.x, -1, 0.01);
               } else {
                  mirv.velocity.x = fapproach(mirv.velocity.x, 1, 0.01);
               }
               if (mirv.position.y > p1.position.y - hover) {
                  mirv.velocity.y = fapproach(mirv.velocity.y, -1, 0.01);
               } else {
                  mirv.velocity.y = fapproach(mirv.velocity.y, 1, 0.01);
               }
               if (!mirv.timer) {
                  if ((rand() % 100) < 30) {
                     mirv.state = ma_rise;
                     play(sound.mirv_engine);
                  } else {
                     mirv.state = ma_findland;
                  }
               }
               mirv.frame += 0.2;
               drawAnimatingAsprite(&mirv.spr, drawpos.x, drawpos.y, 4, 4, &mirv.frame, mirv.flip);
            }break;
         case ma_dive:
            {
               mirv.frame += 0.1;
               drawAnimatingAsprite(&mirv.spr, drawpos.x, drawpos.y, 4, 4, &mirv.frame, mirv.flip);
            }break;
         case ma_findland:
            {
               mirv.velocity.y += gravity;
               if (rectOnGround(&mirvbounds)) {
                  mirv.velocity.x = 0;
                  mirv.state = ma_shotgun;
                  mirv.timer = 20;
                  play(sound.mirv_shotgun);
                  if (mirv.flip) {
                     fireMirvRocket(mirv.position.x - 16, mirv.position.y, -3, -1, 2);
                     fireMirvRocket(mirv.position.x - 16, mirv.position.y, -3, 0, 2);
                     fireMirvRocket(mirv.position.x - 16, mirv.position.y, -3, 1, 2);
                  } else {
                     fireMirvRocket(mirv.position.x + 16, mirv.position.y,  3, -1, 0);
                     fireMirvRocket(mirv.position.x + 16, mirv.position.y,  3, 0, 0);
                     fireMirvRocket(mirv.position.x + 16, mirv.position.y,  3, 1, 0);
                  }
               }
               drawAspriteFrame(&mirv.spr, drawpos.x, drawpos.y, 4, mirv.flip);
            }break;
         case ma_shotgun:
            {
               if (!mirv.timer) {
                  mirv.timer = 50;
                  mirv.state = ma_taunt;
               }
               drawAspriteFrame(&mirv.spr, drawpos.x, drawpos.y, 3, mirv.flip);
            }break;
         case ma_takeoff:
            {
               if (mirv.position.y < p1.position.y - hover) {
                  mirv.state = ma_fly;
                  mirv.timer = 500 + (rand() % 1000);
               }
               if (mirv.position.x > mirv.orbit) {
                  mirv.velocity.x = fapproach(mirv.velocity.x, -1, 0.01);
               } else {
                  mirv.velocity.x = fapproach(mirv.velocity.x, 1, 0.01);
               }
               mirv.velocity.y = fapproach(mirv.velocity.y, -1, 0.01);
               mirv.frame += 0.6;
               drawAnimatingAsprite(&mirv.spr, drawpos.x, drawpos.y, 4, 4, &mirv.frame, mirv.flip);
            }break;
         case ma_rise:
            {
               if (mirv.position.y < p1.position.y - 2*hover) {
                  mirv.state = ma_bomb;
                  mirv.timer = 20;
                  float startx = p1.position.x - 300;
                  float maxx = p1.position.x + 300;
                  float launchy = camera.position.y - 16;
                  for (float lx = startx; lx < maxx; lx += 32) {
                     switch (rand() % 3) {
                        case 0:
                           fireMirvRocket(lx, launchy, -0.1, 2, 3);
                           break;
                        case 1:
                           fireMirvRocket(lx, launchy, 0, 2, 3);
                           break;
                        case 2:
                           fireMirvRocket(lx, launchy, 0.1, 2, 3);
                           break;
                     }
                  }
               }
               mirv.velocity.x = 0;
               mirv.velocity.y = fapproach(mirv.velocity.y, -1, 0.01);
               mirv.frame += 0.6;
               drawAnimatingAsprite(&mirv.spr, drawpos.x, drawpos.y, 4, 4, &mirv.frame, mirv.flip);
            }break;
         case ma_bomb:
            {
               if (!mirv.timer) {
                  mirv.state = ma_fly;
                  mirv.timer = 500 + (rand() % 1000);
               }
            }break;
         default:
            break;
      }
      v2 displacement;
      getMotionWalled(&mirvbounds, &mirv.velocity, &mirv.velocity, &displacement);
      mirv.position = mirv.position + displacement;

      SDL_Rect healthrect;
      SDL_Rect healthbar;
      healthrect.x = healthbar.x = field_w - 8;
      healthrect.y = healthbar.y = 4;
      healthrect.w = healthbar.w = 4;
      healthrect.h = 100;
      healthbar.h = mirv.hitpoints;
      healthbar.y += healthrect.h - healthbar.h;
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderFillRect(ren, &healthrect);
      SDL_SetRenderDrawColor(ren, 255, 255, 100, 255);
      SDL_RenderFillRect(ren, &healthbar);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderDrawRect(ren, &healthrect);
   }
}

void clearEnemies()
{
   countof(boulder) = 0;
   countof(dozer) = 0;
   countof(bullet) = 0;
   countof(saucer) = 0;
   countof(slaser) = 0;
   countof(spider) = 0;
   countof(item) = 0;
   mirv.active = 0;
}

struct {
   char *data;
   SDL_Texture *tex;
   int width, height;
   int size;
   int tex_pitch;
   int tex_samplecount;
} tilemap;

void initTilemap(int screens_w, int screens_h, SDL_Texture *tex)
{
   free(tilemap.data);

   tilemap.width  = screens_w * field_w_tiles;
   tilemap.height = screens_h * field_h_tiles;
   tilemap.size = tilemap.width * tilemap.height;
   tilemap.data = (char*)calloc(tilemap.size, sizeof(char));

   srand(screens_w + screens_h);
   int tw, th;
   tilemap.tex = tex;
   SDL_QueryTexture(tex, 0, 0, &tw, &th);
   tilemap.tex_pitch = tw / tile_size;
   tilemap.tex_samplecount = tilemap.tex_pitch * (th / tile_size);
}

void setRandomTile(int x, int y)
{
   assert(x >= 0 && x < tilemap.width && y >= 0 && y < tilemap.height);
   tilemap.data[x + y * tilemap.width] = (rand() % tilemap.tex_samplecount) + 1;
}

void setRandomRectangle(int x, int y, int w, int h)
{
   int xs = max(x, 0);
   int ys = max(y, 0);
   int xm = min(tilemap.width,  x + w);
   int ym = min(tilemap.height, y + h);
   for (y = ys; y < ym; y++) {
      for (x = xs; x < xm; x++) {
         setRandomTile(x, y);
      }
   }
}

void drawTilemap()
{
   if (tilemap.data) {
      int ofsx = floor(camera.position.x);
      int ofsy = floor(camera.position.y);
      int tstartx = ofsx / tile_size;
      int tstarty = ofsy / tile_size;
      int xs = max(tstartx, 0);
      int ys = max(tstarty, 0);
      int xm = min(tilemap.width,  tstartx + field_w);
      int ym = min(tilemap.height, tstarty + field_h);

      SDL_Rect src;
      SDL_Rect dst;
      src.w = dst.w = src.h = dst.h = tile_size;

      for (int y = ys; y < ym; y++) {
         for (int x = xs; x < xm; x++) {
            int ind = tilemap.data[x + y * tilemap.width];
            if (ind) {
               ind -= 1;
               dst.x = x * tile_size - ofsx;
               dst.y = y * tile_size - ofsy;
               src.x = (ind % tilemap.tex_pitch) * tile_size;
               src.y = (ind / tilemap.tex_pitch) * tile_size;
               SDL_RenderCopy(ren, tilemap.tex, &src, &dst);
            }
         }
      }
   }
}

void loadLevel(const char * fname, int connection)
{
   SDL_RWops *rw = SDL_RWFromFile(fname, "r");
   if (connection != 0) {
      int debug = 5;
   }
   if (rw) {
      clearEnemies();
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

      initTilemap(screens_w, screens_h, tex.wall);

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
                  if (!connection) {
                     int x = (i % pitch) * rtw + (0.5*rtw -7);
                     int y = (i / pitch) * rth + (0.5*rth -7);
                     p1 = createPlayer(x, y);
                  }
               } break;
            case 's':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createSaucerMob((x + 1) * tile_size, (y + 1) * tile_size);
               }break;
            case 'I':
            case 'i':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createItem((x + 1) * tile_size, (y + 1) * tile_size, block[i] == 'I', 1);
               }break;
            case 'B':
            case 'b':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createBulletMob((x + 1) * tile_size, (y + 1) * tile_size, block[i] == 'b');
               }break;
            case 'D':
            case 'd':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createDozer((x + 1) * tile_size, (y + 1) * tile_size, block[i] == 'd');
               }break;
            case 'P':
            case 'p':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createSpiderMob((x + 1) * tile_size, (y + 1) * tile_size, block[i] == 'p');
               }break;
            case 'M':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  startMirv((x) * tile_size, (y) * tile_size);
               }break;
            case 'O':
               {
                  int x = (i % pitch) * tile_xc;
                  int y = (i / pitch) * tile_yc;
                  createBoulder((x) * tile_size, (y) * tile_size);
               }break;
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
                  setRandomRectangle(rx * tile_xc, ry * tile_yc, rw * tile_xc, rh * tile_yc);
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
      srand(time(0));
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

   tex.saber = loadTexture("saber.gif");
   tex.robots = loadTexture("robots.gif");
   tex.wall = loadTexture("wall.gif");
   tex.stone = loadTexture("boulder.gif");
   tex.effect = loadTexture("mirvattack.gif");
   tex.mirv = loadTexture("mirv.gif");
   tex.ladder = loadTexture("ladder.gif");

   Mix_Init(0);
   assert(!Mix_OpenAudio(22050, AUDIO_U16SYS, 1, 256));
   Mix_AllocateChannels(16);
   sound.hit               = Mix_LoadWAV("sound/hit.wav");
   sound.mirv_engine       = Mix_LoadWAV("sound/mirv_engine.wav");
   sound.mirv_hit          = Mix_LoadWAV("sound/mirv_hit.wav");
   sound.mirv_die          = Mix_LoadWAV("sound/mirv_die.wav");
   sound.mirv_shotgun      = Mix_LoadWAV("sound/mirv_shotgun.wav");
   sound.reflect           = Mix_LoadWAV("sound/reflect.wav");
   sound.rock_break        = Mix_LoadWAV("sound/rock_break.wav");
   sound.saber_die         = Mix_LoadWAV("sound/saber_die.wav");
   sound.saber_hit         = Mix_LoadWAV("sound/saber_hit.wav");
   sound.saber_jump        = Mix_LoadWAV("sound/saber_jump.wav");
   sound.saber_shoot       = Mix_LoadWAV("sound/saber_shoot.wav");
   sound.saber_heal        = Mix_LoadWAV("sound/saber_heal.wav");
   sound.spider_hit        = Mix_LoadWAV("sound/spider_hit.wav");
   sound.spider_shoot      = Mix_LoadWAV("sound/spider_shoot.wav");
   music.mirv_theme        = Mix_LoadMUS("sound/mirv_theme_scott.wav");
   music.level_theme       = Mix_LoadMUS("sound/saber_level_theme.wav");

   testsprite st = createTestSprite(10, 10, 255, 255, 0);

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
      // NOTE(afox): xbox controller buttons are different on windows for some reason?
#ifdef _WIN32
      con.jump = bindButton(10);
      con.fire = bindButton(12);
      con.reset = bindButton(5);
#else
      con.jump = bindButton(0);
      con.fire = bindButton(2);
      con.reset = bindButton(8);
#endif
   } else {
      con.left = bindKey(SDLK_LEFT);
      con.right = bindKey(SDLK_RIGHT);
      con.up = bindKey(SDLK_UP);
      con.down = bindKey(SDLK_DOWN);
      con.jump = bindKey(SDLK_d);
      con.fire = bindKey(SDLK_f);
      con.reset = bindKey(SDLK_BACKSPACE);
   }

   while (running) {
      if (con.reset->pressed) {
         loadLevel("startroom.txt", 0);
      }
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
      if (p1.alive) {
         switch (songstate) {
            case ss_silent:
               if (countof(boulder) == 0) {
                  Mix_FadeInMusic(music.level_theme, -1, 1000);
                  songstate = ss_leveltheme;
               }
               if (mirv.active) {
                  Mix_FadeInMusic(music.mirv_theme, -1, 1000);
                  songstate = ss_bosstheme;
               }
               break;
            case ss_leveltheme:
               if (countof(boulder) > 0) {
                  Mix_FadeOutMusic(1000);
               }
               if (!Mix_PlayingMusic()) {
                  songstate = ss_silent;
               }
               if (mirv.active) {
                  Mix_FadeInMusic(music.mirv_theme, -1, 1000);
                  songstate = ss_bosstheme;
               }
               break;
            case ss_bosstheme:
               if (!mirv.active) {
                  Mix_HaltMusic();
                  songstate = ss_silent;
               }
               break;
         };
      } else {
         songstate = ss_silent;
         Mix_HaltMusic();
      }
      SDL_SetRenderTarget(ren, pixelbuffer);
      SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
      SDL_RenderClear(ren);


      tickPlayer(&p1);
      stepPshots();
      tickEnemies();


      SDL_SetRenderDrawColor(ren, 0, 255, 255, 255);
      //debugDrawWalls(ren);
      drawTilemap();
      drawLadders();
      drawEnemies();
      doMirv();
      drawPlayer(&p1);
      drawPshots();
      drawEffects();
      //drawConnections();
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
#ifdef _WIN32
         SDL_Delay(0);
#else
         SDL_Delay(1);
#endif
      }
      next_step = SDL_GetPerformanceCounter() + step_size;
      frame++;
   }
}

