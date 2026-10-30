// PGLoadSceneSeconds truncates the first second to zero and expires at 180 s.
// Lifecycle safety must use the actual active flag, not that UI duration.
bool PGLoadSceneActive(void) {
    if (env.memory.size > 23389293 && atomic_load_explicit((const _Atomic uint8_t *)((const uint8_t *)(void *)env.memory.data + 23389293),memory_order_relaxed) == 1) return true;
    return atomic_load(&loadSceneSince) > 0;
}
// Bounded, read-only transition trace: no fake completion, timeout bypass or reload.
// Called on the existing main-script tick, not a new timer or worker.
static void pg_scene_transition_trace(void) {
    static bool wasLoading; static double started, next;
    bool loading = PGLoadSceneActive();
    if (!loading && !wasLoading) return;
    double now = timeMS(CLOCK_MONOTONIC);
    if (loading && !wasLoading) { started=now; next=now+3000; }
    if (loading && now >= next && env.memory.size > 23417974) {
        const uint8_t *m=(const uint8_t *)(void *)env.memory.data;
        uint32_t state; uint16_t entities,bounds;
        memcpy(&state,m+23389296,4);memcpy(&entities,m+23417896,2);memcpy(&bounds,m+23417912,2);
        char line[240];snprintf(line,sizeof line,"Transition wait %.1f s: phase %u, volume %u, entities %u, bounds %u, collision flags %u/%u/%u, control status 0x%x.",(now-started)/1000,state,m[23417824],entities,bounds,m[23417920],m[23417972],m[23417973],PGGameStatus());pg_log(line);
        next=now+5000;
    }
    if (!loading && wasLoading) { char line[100];snprintf(line,sizeof line,"Transition streaming released after %.1f s; control status 0x%x.",(now-started)/1000,PGGameStatus());pg_log(line); }
    wasLoading=loading;
}
// Settings › Cheats: one-off actions (repair, clean, time, weather) and the God mode and Never wanted
// switches. Included by EngineHost.c. The app only queues requests here; pg_native_cheats_tick runs
// them on the game thread during the "main" script's turn (pg_asi_script_tick), where game commands
// are safe. Running them from UIKit or the controller thread races the engine's own updates.
void PG_FN_CAMERA_PED_MODE(w2c_game*, u32);
void PG_FN_CAMERA_VEHICLE_MODE(w2c_game*, u32);
u32 PG_FN_PED_VEHICLE(w2c_game*, u32, u32);
void PG_FN_REPAIR_VEHICLE(w2c_game*, u32);
void PG_FN_VEHICLE_ENGINE_HEALTH(w2c_game*, u32, f32);
void PG_FN_VEHICLE_DIRT(w2c_game*, u32, f32);
void PG_FN_SET_CLOCK(w2c_game*, u32, u32, u32);
void PG_FN_SET_WEATHER(w2c_game*, u64);
void PG_FN_CLEAR_WEATHER(w2c_game*);
void PG_FN_CLEAR_WEATHER_OVERRIDE(w2c_game*);
u32 PG_FN_PLAYER_ID(w2c_game*);
void PG_FN_SET_PLAYER_INVINCIBLE(w2c_game*, u32, u32);
u32 PG_FN_GET_PLAYER_INVINCIBLE(w2c_game*, u32);
void PG_FN_CLEAR_WANTED(w2c_game*, u32);
u32 PG_FN_GET_WANTED(w2c_game*, u32);
void PG_FN_SET_MAX_WANTED(w2c_game*, u32);
u32 PG_FN_GET_MAX_WANTED(w2c_game*);
u64 PG_FN_TURBO_ENTITY_ADDRESS(w2c_game*, u32);
f32 PG_FN_GET_ENTITY_SPEED(w2c_game*, u32);
f32 PG_FN_GET_ENTITY_HEADING(w2c_game*, u32);
void PG_FN_SET_ENTITY_COORDS_NO_OFFSET(w2c_game*, u32, u64, u32, u32, u32);
u64 PG_FN_OPERATOR_NEW(w2c_game*, u64);

static pthread_mutex_t cheatMenuLock = PTHREAD_MUTEX_INITIALIZER;
static PGCheatStatus cheatMenuStatus;
static PGCheatCommand cheatMenuCommand;
static int32_t cheatMenuValue;
static double cheatMenuQueuedAt;
static bool cheatGodMode, cheatNeverWanted; // What the game has now: written on the game thread, read under the lock.
static bool cheatGodRequested, cheatWantedRequested; // What the switches show: under the lock.
static unsigned cheatSwitchPending; // Bit 0 god mode, bit 1 never wanted: flipped since the game last applied them. Under the lock.
static u32 cheatPreviousMaxWanted; // The wanted limit before Never wanted, given back when it's turned off. Game thread only.
static const char *const menuWeather[] = {"CLEAR", "EXTRASUNNY", "CLOUDS", "OVERCAST", "RAIN", "THUNDER", "FOGGY"};

bool PGQueueCheat(PGCheatCommand command, int32_t value) {
  if ((unsigned)command > PGCheatCameraMode) return false;
  if (command == PGCheatSetTime && (value < 0 || value > 23)) return false;
  if (command == PGCheatSetWeather && (value < 0 || value >= (int32_t)(sizeof menuWeather / sizeof *menuWeather))) return false;
  if (command == PGCheatCameraMode && value != 1 && value != 4) return false;
  pthread_mutex_lock(&cheatMenuLock);
  bool accepted = cheatMenuStatus != PGCheatWaiting;
  if (accepted) { cheatMenuCommand = command; cheatMenuValue = value; cheatMenuQueuedAt = timeMS(CLOCK_MONOTONIC); cheatMenuStatus = PGCheatWaiting; }
  pthread_mutex_unlock(&cheatMenuLock);
  return accepted;
}
PGCheatStatus PGGetCheatStatus(void) {
  pthread_mutex_lock(&cheatMenuLock);
  PGCheatStatus status = cheatMenuStatus;
  pthread_mutex_unlock(&cheatMenuLock);
  return status;
}
bool PGCheatEnabled(PGCheatSwitch cheat) {
  pthread_mutex_lock(&cheatMenuLock);
  bool enabled = cheat == PGCheatGodMode ? cheatGodRequested : cheat == PGCheatNeverWanted && cheatWantedRequested;
  pthread_mutex_unlock(&cheatMenuLock);
  return enabled;
}
bool PGSetPersistentCheatEnabled(PGCheatSwitch cheat, bool enabled) {
  if (cheat != PGCheatGodMode && cheat != PGCheatNeverWanted) return false;
  pthread_mutex_lock(&cheatMenuLock);
  if (cheat == PGCheatGodMode) { cheatGodRequested = enabled; cheatSwitchPending |= 1; }
  else { cheatWantedRequested = enabled; cheatSwitchPending |= 2; }
  pthread_mutex_unlock(&cheatMenuLock);
  return true;
}
static void pg_apply_cheat_switch(w2c_game *instance, PGCheatSwitch cheat, bool enabled) {
  if (cheat == PGCheatGodMode) {
    if (enabled != cheatGodMode) PG_FN_SET_PLAYER_INVINCIBLE(instance, PG_FN_PLAYER_ID(instance), enabled);
    pthread_mutex_lock(&cheatMenuLock);
    cheatGodMode = enabled;
    if (!(cheatSwitchPending & 1)) cheatGodRequested = enabled;
    pthread_mutex_unlock(&cheatMenuLock);
  } else {
    if (enabled && !cheatNeverWanted) {
      cheatPreviousMaxWanted = PG_FN_GET_MAX_WANTED(instance);
      PG_FN_SET_MAX_WANTED(instance, 0);
      PG_FN_CLEAR_WANTED(instance, PG_FN_PLAYER_ID(instance));
    } else if (!enabled && cheatNeverWanted && PG_FN_GET_MAX_WANTED(instance) == 0) {
      // Only when the limit is still ours: a mission that set its own limit since then keeps it.
      PG_FN_SET_MAX_WANTED(instance, cheatPreviousMaxWanted);
    }
    pthread_mutex_lock(&cheatMenuLock);
    cheatNeverWanted = enabled;
    if (!(cheatSwitchPending & 2)) cheatWantedRequested = enabled;
    pthread_mutex_unlock(&cheatMenuLock);
  }
}
static void pg_apply_pending_cheat_switches(w2c_game *instance) {
  if (PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive() ||
      !PG_FN_PLAYER_PED_ID(instance) || PG_FN_PLAYER_ID(instance) >= 32) return;
  pthread_mutex_lock(&cheatMenuLock);
  unsigned pending = cheatSwitchPending;
  bool god = cheatGodRequested, wanted = cheatWantedRequested;
  cheatSwitchPending = 0;
  pthread_mutex_unlock(&cheatMenuLock);
  if (!pending) return;
  u64 previous = pg_asi_enter_script(instance);
  if (pending & 1) pg_apply_cheat_switch(instance, PGCheatGodMode, god);
  if (pending & 2) pg_apply_cheat_switch(instance, PGCheatNeverWanted, wanted);
  pg_asi_leave_script(instance, previous);
  char line[96];
  snprintf(line, sizeof line, "Cheats: god mode %s, never wanted %s.", cheatGodMode ? "on" : "off", cheatNeverWanted ? "on" : "off");
  pg_log(line);
}
static void pg_native_cheats_action_tick(w2c_game *instance) {
  pthread_mutex_lock(&cheatMenuLock);
  PGCheatStatus status = cheatMenuStatus;
  PGCheatCommand command = cheatMenuCommand;
  int32_t value = cheatMenuValue;
  double queuedAt = cheatMenuQueuedAt;
  pthread_mutex_unlock(&cheatMenuLock);
  if (status != PGCheatWaiting) return;
  // Counted from when it was queued: scripts may not run at all while paused, and an action asked
  // for long ago must not suddenly happen when play resumes.
  if (timeMS(CLOCK_MONOTONIC) - queuedAt >= 10000) status = PGCheatNotReady;
  else if (PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive()) return;
  else {
    u64 previous = pg_asi_enter_script(instance);
    // The weather name goes on this thread's own engine stack, freed again below.
    u64 stack = instance->w2c_g0;
    instance->w2c_g0 -= 32;
    status = PGCheatSucceeded;
    switch (command) {
      case PGCheatRepairVehicle:
      case PGCheatCleanVehicle: {
        u32 ped = PG_FN_PLAYER_PED_ID(instance);
        u32 vehicle = ped ? PG_FN_PED_VEHICLE(instance, ped, 0) : 0;
        if (!vehicle) { status = PGCheatNoVehicle; break; }
        if (command == PGCheatRepairVehicle) {
          PG_FN_VEHICLE_ENGINE_HEALTH(instance, vehicle, 1000.f);
          PG_FN_REPAIR_VEHICLE(instance, vehicle);
        }
        PG_FN_VEHICLE_DIRT(instance, vehicle, 0.f);
        break;
      }
      case PGCheatCameraMode:
        PG_FN_CAMERA_PED_MODE(instance, (u32)value);
        PG_FN_CAMERA_VEHICLE_MODE(instance, (u32)value);
        break;
      case PGCheatSetTime: PG_FN_SET_CLOCK(instance, (u32)value, 0, 0); break;
      case PGCheatSetWeather:
        snprintf(pg_pointer(&env, instance->w2c_g0, 32), 32, "%s", menuWeather[value]);
        PG_FN_CLEAR_WEATHER_OVERRIDE(instance);
        PG_FN_SET_WEATHER(instance, instance->w2c_g0);
        break;
      case PGCheatNaturalWeather:
        PG_FN_CLEAR_WEATHER_OVERRIDE(instance);
        PG_FN_CLEAR_WEATHER(instance);
        break;
    }
    instance->w2c_g0 = stack;
    pg_asi_leave_script(instance, previous);
  }
  char line[96];
  snprintf(line, sizeof line, "Cheats: action %d (value %d) finished with status %d.", command, value, status);
  pg_log(line);
  pthread_mutex_lock(&cheatMenuLock);
  cheatMenuStatus = status;
  pthread_mutex_unlock(&cheatMenuLock);
}

// Làm mờ màn hình: gỡ mờ chuyển cảnh còn sót và tắt motion blur của camera, người chơi và xe đang lái.
// Không chạy khi đang ở menu pause hoặc đang tải để không phá nền mờ hợp lệ của menu.
static void pg_clear_all_screen_blur(w2c_game *instance) {
  if (!instance || PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive()) return;
  u32 ped = PG_FN_PLAYER_PED_ID(instance);
  if (!ped) return;
  u64 previous = pg_asi_enter_script(instance);
  if (pg_test_call(instance, 0x7b226c785a52a0a9ull, 0, NULL) & 1) { // IS_SCREENBLUR_FADE_RUNNING
    pg_test_call(instance, 0xde81239437e8c5a8ull, 0, NULL); // DISABLE_SCREENBLUR_FADE
    pg_test_call(instance, 0xefacc8aef94430d5ull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // TRIGGER_SCREENBLUR_FADE_OUT(0.0f)
  }
  pg_test_call(instance, 0xb4eddc19532bfb85ull, 0, NULL); // ANIMPOSTFX_STOP_ALL
  pg_test_call(instance, 0x487a82c650eb7799ull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_MOTION_BLUR_SCALING_THIS_UPDATE(0.0f)
  pg_test_call(instance, 0x0225778816fdc28cull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_MAX_MOTION_BLUR_STRENGTH_THIS_UPDATE(0.0f)
  pg_test_call(instance, 0x0a986918b102b448ull, 2, (uint64_t[]){ped, 0}); // SET_PED_MOTION_BLUR(ped, FALSE)
  pg_test_call(instance, 0x295d82a8559f9150ull, 2, (uint64_t[]){ped, 0}); // SET_ENTITY_MOTION_BLUR(ped, FALSE)
  u32 vehicle = PG_FN_PED_VEHICLE(instance, ped, 0);
  if (vehicle) pg_test_call(instance, 0x295d82a8559f9150ull, 2, (uint64_t[]){vehicle, 0}); // SET_ENTITY_MOTION_BLUR(vehicle, FALSE)
  pg_asi_leave_script(instance, previous);
}

// Xe/NPC bị xe mod đâm: bị hất bay lên rồi nổ giữa không trung. Mỗi mục tiêu đi qua 3 bước theo thời gian:
// 0 phóng lên, 1 phóng lại sau ~90 ms (vật lý hay xoá vận tốc ở khung đầu), 2 nổ sau ~700 ms.
// Mục tiêu giữ trong danh sách 6 giây để xác xe/NPC vừa nổ không bị kích hoạt lại.
#define PG_HIT_MAX 12
typedef struct { u32 entity; bool vehicle; int stage; double at; float vx, vy, vz; } PGHitTarget;
static PGHitTarget pgHitTargets[PG_HIT_MAX];

static bool pg_hit_known(u32 entity) {
  for (int i = 0; i < PG_HIT_MAX; i++) if (pgHitTargets[i].entity == entity) return true;
  return false;
}
static bool pg_hit_exists(w2c_game *instance, u32 entity) {
  return entity && (pg_test_call(instance, 0x7239b21a38f536baull, 1, (uint64_t[]){entity}) & 1); // DOES_ENTITY_EXIST
}
static bool pg_hit_position(w2c_game *instance, u32 entity, float *out) {
  uint64_t addr = PG_FN_TURBO_ENTITY_ADDRESS(instance, entity);
  if (!addr) return false;
  const float *pos = pg_pointer(&env, addr + 144, 12);
  if (!pos) return false;
  out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2];
  return true;
}
static void pg_hit_launch(w2c_game *instance, const PGHitTarget *t) {
  pg_test_call(instance, 0x1c99bb7b6e96d16full, 4,
    (uint64_t[]){t->entity, pg_f32_arg(t->vx), pg_f32_arg(t->vy), pg_f32_arg(t->vz)}); // SET_ENTITY_VELOCITY
}
// Cảnh sát: gây nổ do người chơi thì tính vào người chơi (ADD_OWNED_EXPLOSION) và đảm bảo có truy nã tối thiểu 2 sao.
static void pg_hit_wanted(w2c_game *instance, u32 player) {
  if (cheatNeverWanted || player >= 32 || PG_FN_GET_MAX_WANTED(instance) == 0) return;
  if (PG_FN_GET_WANTED(instance, player) < 2) {
    pg_test_call(instance, 0x39ff19c64ef7da5bull, 3, (uint64_t[]){player, 2, 0}); // SET_PLAYER_WANTED_LEVEL(player, 2, FALSE)
    pg_test_call(instance, 0xe0a7d1e497ffcd6full, 2, (uint64_t[]){player, 0}); // SET_PLAYER_WANTED_LEVEL_NOW(player, FALSE)
  }
}
static void pg_hit_start(w2c_game *instance, u32 entity, bool isVehicle, float dirX, float dirY, double now, u32 player) {
  int slot = -1;
  for (int i = 0; i < PG_HIT_MAX; i++) if (!pgHitTargets[i].entity) { slot = i; break; }
  if (slot < 0) return;
  // Gỡ mọi lớp bất tử/proof của mục tiêu để nó chắc chắn bay và nổ được (cả xe mod và xe đặc biệt).
  pg_test_call(instance, 0x3882114bde571ad4ull, 2, (uint64_t[]){entity, 0}); // SET_ENTITY_INVINCIBLE(entity, FALSE)
  pg_test_call(instance, 0xfaee099c6f890bb8ull, 9, (uint64_t[]){entity, 0, 0, 0, 0, 0, 0, 0, 0}); // SET_ENTITY_PROOFS: tắt hết
  pg_test_call(instance, 0x1760ffa8ab074d66ull, 2, (uint64_t[]){entity, 1}); // SET_ENTITY_CAN_BE_DAMAGED(entity, TRUE)
  PGHitTarget *t = &pgHitTargets[slot];
  t->entity = entity; t->vehicle = isVehicle; t->stage = 0; t->at = now;
  t->vx = dirX * (isVehicle ? 42.0f : 36.0f);
  t->vy = dirY * (isVehicle ? 42.0f : 36.0f);
  t->vz = isVehicle ? 24.0f : 18.0f;
  if (!isVehicle) {
    pg_test_call(instance, 0xb128377056a54e2aull, 2, (uint64_t[]){entity, 1}); // SET_PED_CAN_RAGDOLL(ped, TRUE)
    pg_test_call(instance, 0xae99fb955581844aull, 7, (uint64_t[]){entity, 5000, 5000, 0, 1, 1, 0}); // SET_PED_TO_RAGDOLL
  }
  pg_hit_launch(instance, t);
  pg_hit_wanted(instance, player);
}
static void pg_hit_explode(w2c_game *instance, const PGHitTarget *t, u32 owner) {
  float pos[3] = {0.f, 0.f, 0.f};
  bool have = pg_hit_position(instance, t->entity, pos);
  if (t->vehicle) {
    PG_FN_VEHICLE_ENGINE_HEALTH(instance, t->entity, -4000.f);
    pg_test_call(instance, 0x70db57649fa8d0d8ull, 2, (uint64_t[]){t->entity, pg_f32_arg(-1000.0f)}); // SET_VEHICLE_PETROL_TANK_HEALTH
    pg_test_call(instance, 0xba71116adf5b514cull, 3, (uint64_t[]){t->entity, 1, 0}); // EXPLODE_VEHICLE(vehicle, audible, visible)
  } else {
    pg_test_call(instance, 0x6b76dc1f3ae6e6a3ull, 3, (uint64_t[]){t->entity, 0, 0}); // SET_ENTITY_HEALTH(ped, 0)
  }
  if (have) {
    pg_test_call(instance, 0x172aa1b624fa1013ull, 9, (uint64_t[]){owner, pg_f32_arg(pos[0]), pg_f32_arg(pos[1]),
      pg_f32_arg(pos[2] + (t->vehicle ? 0.6f : 1.0f)), 2, pg_f32_arg(t->vehicle ? 3.0f : 2.5f), 1, 0, pg_f32_arg(1.0f)}); // ADD_OWNED_EXPLOSION
  }
}
static void pg_hit_tick(w2c_game *instance, u32 owner, double now) {
  for (int i = 0; i < PG_HIT_MAX; i++) {
    PGHitTarget *t = &pgHitTargets[i];
    if (!t->entity) continue;
    double age = now - t->at;
    if (age >= 6000.0 || !pg_hit_exists(instance, t->entity)) { t->entity = 0; continue; }
    if (t->stage == 0 && age >= 90.0) { pg_hit_launch(instance, t); t->stage = 1; }
    if (t->stage == 1 && age >= 700.0) { pg_hit_explode(instance, t, owner); t->stage = 2; }
  }
}
// Cảnh sát luôn sẵn sàng truy nã: không bị bỏ qua, được điều động, và có thể tự sinh (khi game cho phép truy nã).
static void pg_police_ready(w2c_game *instance, u32 player, double now) {
  static double last = 0.0;
  if (cheatNeverWanted || player >= 32 || now - last < 2000.0) return;
  last = now;
  if (PG_FN_GET_MAX_WANTED(instance) == 0) return;
  pg_test_call(instance, 0x32c62aa929c2da6aull, 2, (uint64_t[]){player, 0}); // SET_POLICE_IGNORE_PLAYER(player, FALSE)
  pg_test_call(instance, 0x8eeda153ad141ba4ull, 2, (uint64_t[]){player, 0}); // SET_EVERYONE_IGNORE_PLAYER(player, FALSE)
  pg_test_call(instance, 0xdb172424876553f4ull, 2, (uint64_t[]){player, 1}); // SET_DISPATCH_COPS_FOR_PLAYER(player, TRUE)
  pg_test_call(instance, 0x102e68b2024d536dull, 1, (uint64_t[]){1}); // SET_CREATE_RANDOM_COPS(TRUE)
}
static u32 pg_hit_valid_candidate(u32 candidate, u32 own) {
  return (candidate && candidate != (u32)-1 && candidate != own && !pg_hit_known(candidate)) ? candidate : 0;
}

// Bất tử + đâm xe nổ chỉ hoạt động sau khi đã hoàn thành toàn bộ nhiệm vụ cốt truyện. Cờ này được app lưu lại
// (GameSettings.storyCompleted) và đặt lúc khởi động bằng PGSetStoryCompleted; khi game chạy cảnh kết thúc cốt truyện
// (script finale_endgame hoặc finale_credits) thì tự bật và báo cho app bằng PGStoryCompletedNewlyDetected.
static _Atomic bool storyDone, storyDoneNew;
void PGSetStoryCompleted(bool done) { atomic_store(&storyDone, done); }
bool PGStoryCompletedNewlyDetected(void) { return atomic_exchange(&storyDoneNew, false); }
static void pg_story_watch_tick(void) {
  static double last = 0.0;
  if (atomic_load(&storyDone) || !atomic_load(&inputBlock)) return;
  const double now = timeMS(CLOCK_MONOTONIC);
  if (now - last < 1000.0) return;      // mỗi giây một lần là đủ
  last = now;
  const uint8_t *m = (const uint8_t *)(void *)env.memory.data; const uint64_t msize = env.memory.size;
  uint64_t threads = 0; uint16_t count = 0;
  if (!pg_context_u64(m, msize, 15409704, &threads) || !threads || !pg_context_u16(m, msize, 15409712, &count)) return;
  for (uint16_t i = 0; i < count && i < 512; i++) {
    uint64_t thread;
    if (!pg_context_u64(m, msize, threads + 8ull * i, &thread) || !thread || thread + 460 > msize) continue;
    const char *name = (const char *)(m + thread + 428);
    if (!strncasecmp(name, "finale_endgame", 14) || !strncasecmp(name, "finale_credits", 14)) {
      atomic_store(&storyDone, true); atomic_store(&storyDoneNew, true);
      pg_log("Story: finished, god mode and crash explosions are now on.");
      return;
    }
  }
}

static void pg_vehicle_god_mod_tick(w2c_game *instance) {
  if (PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive()) return;
  if (!atomic_load(&storyDone)) return;      // chưa xong cốt truyện: không bất tử, không đâm xe nổ
  u32 ped = PG_FN_PLAYER_PED_ID(instance);
  if (!ped) return;

  u64 previous = pg_asi_enter_script(instance);
  double now = timeMS(CLOCK_MONOTONIC);
  u32 player = PG_FN_PLAYER_ID(instance);

  // Bất tử cho người chơi trên thực thể ped (không dùng SET_PLAYER_INVINCIBLE để giữ nguyên cảnh sát truy nã)
  pg_test_call(instance, 0x3882114bde571ad4ull, 2, (uint64_t[]){ped, 1}); // SET_ENTITY_INVINCIBLE: ped
  pg_test_call(instance, 0xfaee099c6f890bb8ull, 9, (uint64_t[]){ped, 1, 1, 1, 1, 1, 1, 1, 1}); // SET_ENTITY_PROOFS: ped
  pg_test_call(instance, 0x6b76dc1f3ae6e6a3ull, 3, (uint64_t[]){ped, 200, 0}); // SET_ENTITY_HEALTH: 200

  pg_hit_tick(instance, ped, now);
  pg_police_ready(instance, player, now);

  u32 vehicle = PG_FN_PED_VEHICLE(instance, ped, 0);
  if (!vehicle) {
    pg_asi_leave_script(instance, previous);
    return;
  }

  // Bất tử cho xe, không bao giờ hỏng hay nổ
  PG_FN_VEHICLE_ENGINE_HEALTH(instance, vehicle, 1000.f);
  PG_FN_REPAIR_VEHICLE(instance, vehicle);
  pg_test_call(instance, 0x3882114bde571ad4ull, 2, (uint64_t[]){vehicle, 1}); // SET_ENTITY_INVINCIBLE: vehicle
  pg_test_call(instance, 0xfaee099c6f890bb8ull, 9, (uint64_t[]){vehicle, 1, 1, 1, 1, 1, 1, 1, 1}); // SET_ENTITY_PROOFS: vehicle

  // Tăng tốc độ chạy cho tất cả các loại xe (nhanh và mạnh mẽ hơn)
  pg_test_call(instance, 0xb59e4bd37ae292dbull, 2, (uint64_t[]){vehicle, pg_f32_arg(2.5f)}); // SET_VEHICLE_CHEAT_POWER_INCREASE: 2.5x torque
  pg_test_call(instance, 0x93a3996368c94158ull, 2, (uint64_t[]){vehicle, pg_f32_arg(1.4f)}); // MODIFY_VEHICLE_TOP_SPEED: +40% top speed

  // Tuyệt đối không bao giờ bị ngã hay văng ra khỏi xe (thắt dây an toàn, không rơi xe máy, không ragdoll)
  pg_test_call(instance, 0x7a6535691b477c48ull, 2, (uint64_t[]){ped, 1}); // SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE: 1 (KNOCKOFFVEH_NEVER)
  pg_test_call(instance, 0xb282749d5e028163ull, 2, (uint64_t[]){ped, 0}); // SET_PED_CAN_BE_KNOCKED_OFF_BIKE: 0
  pg_test_call(instance, 0xb128377056a54e2aull, 2, (uint64_t[]){ped, 0}); // SET_PED_CAN_RAGDOLL: 0

  // Chức năng chống lọt map: nếu độ cao z < -40.0f thì đưa xe lên mặt đường an toàn
  uint64_t vAddr = PG_FN_TURBO_ENTITY_ADDRESS(instance, vehicle);
  if (vAddr) {
    const float *pos = pg_pointer(&env, vAddr + 144, 12);
    if (pos && pos[2] < -40.f) {
      static uint64_t safeVec;
      if (!safeVec) safeVec = PG_FN_OPERATOR_NEW(instance, 32);
      if (safeVec) {
        float *v = pg_pointer(&env, safeVec, 24);
        v[0] = pos[0]; v[2] = pos[1]; v[4] = 30.f;
        PG_FN_SET_ENTITY_COORDS_NO_OFFSET(instance, vehicle, safeVec, 0, 0, 0);
      }
    }
  }

  // Quản lý trạng thái trên không: chống nảy / chống bay lại liên tục khi tiếp đất
  static float lastSpeed = 0.0f;
  static double lastAirTime = 0.0;
  static double landedTime = 0.0;
  static bool wasInAir = false;
  static double lastBuildingJumpTime = 0.0;
  float currentSpeed = PG_FN_GET_ENTITY_SPEED(instance, vehicle);

  int inAir = (int)(pg_test_call(instance, 0x886e37ec497200b6ull, 1, (uint64_t[]){vehicle}) & 1); // IS_ENTITY_IN_AIR
  if (inAir) {
    wasInAir = true;
    lastAirTime = now;
  } else {
    if (wasInAir) {
      wasInAir = false;
      landedTime = now; // Vừa tiếp đất an toàn
      // Khi vừa tiếp đất, tự động chỉnh xe ngay ngắn trên 4 bánh
      pg_test_call(instance, 0x49733e92263139d1ull, 1, (uint64_t[]){vehicle}); // SET_VEHICLE_ON_GROUND_PROPERLY
    }
  }
  // Chức năng tự lật xe nếu bị lật ngửa nóc xe xuống đất
  int stuckOnRoof = (int)(pg_test_call(instance, 0xb497f06b288dcfdfull, 1, (uint64_t[]){vehicle}) & 1); // IS_VEHICLE_STUCK_ON_ROOF
  if (stuckOnRoof) {
    pg_test_call(instance, 0x49733e92263139d1ull, 1, (uint64_t[]){vehicle}); // SET_VEHICLE_ON_GROUND_PROPERLY
  }

  bool recentlyLanded = (now - landedTime < 2500) || (now - lastAirTime < 1500); // 2.5s sau khi tiếp đất tuyệt đối không kích hoạt bay xe

  int collided = (int)(pg_test_call(instance, 0x8bad02f0368d9e14ull, 1, (uint64_t[]){vehicle}) & 1); // HAS_ENTITY_COLLIDED_WITH_ANYTHING
  uint32_t hitVehTime = (uint32_t)pg_test_call(instance, 0x5d35ecf3a81a0ee0ull, 1, (uint64_t[]){player}); // GET_TIME_SINCE_PLAYER_HIT_VEHICLE
  uint32_t hitPedTime = (uint32_t)pg_test_call(instance, 0xe36a25322dc35f42ull, 1, (uint64_t[]){player}); // GET_TIME_SINCE_PLAYER_HIT_PED
  bool suddenDecel = (lastSpeed - currentSpeed) > 3.0f && lastSpeed > 3.5f;

  if (vAddr) {
    const float *pos = pg_pointer(&env, vAddr + 144, 12);
    if (pos) {
      float vx = pos[0], vy = pos[1], vz = pos[2];
      float heading = PG_FN_GET_ENTITY_HEADING(instance, vehicle);
      float rad = heading * 0.0174532925f;
      float dirX = -sinf(rad);
      float dirY = cosf(rad);

      bool hitTarget = false;

      if (currentSpeed > 0.5f || lastSpeed > 0.5f || collided || hitVehTime < 500) {
        // Điểm quét cách tâm xe 3.4 m trở lên (bán kính 2.8 m nên không bao giờ chọn nhầm xe của người chơi): 4 điểm phía
        // trước, 1 phía sau và 2 bên, để xe nào chạm vào, từ hướng nào (kể cả xe cảnh sát tông từ sau hoặc hông), cũng bị bắt.
        static const float probe[7][2] = {{3.4f, 0.0f}, {5.0f, 0.0f}, {6.6f, 0.0f}, {8.2f, 0.0f}, {-3.4f, 0.0f}, {0.0f, 3.4f}, {0.0f, -3.4f}};   // {tiến, sang phải}

        // 1. XE KHÁC: chỉ cần chạm nhẹ là bị hất bay rồi nổ (mọi loại xe, kể cả cảnh sát, xe không người lái/xe đỗ/xe mod)
        int started = 0;
        for (int k = 0; k < 7 && started < 2; k++) {
          float px = vx + dirX * probe[k][0] + dirY * probe[k][1], py = vy + dirY * probe[k][0] - dirX * probe[k][1];
          for (int pass = 0; pass < 2; pass++) {
            u32 other = pg_hit_valid_candidate((u32)pg_test_call(instance, 0xf73eb622c4f1689bull, 6,
              (uint64_t[]){pg_f32_arg(px), pg_f32_arg(py), pg_f32_arg(vz), pg_f32_arg(2.8f), 0, pass ? 0 : 70}), vehicle); // GET_CLOSEST_VEHICLE (lần 2 không lọc cờ)
            if (!other) continue;
            int dead = (int)(pg_test_call(instance, 0x5f9532f3b5cc2551ull, 1, (uint64_t[]){other}) & 1); // IS_ENTITY_DEAD
            if (!dead) {
              int touching = (int)(pg_test_call(instance, 0x17ffc1b2ba35a494ull, 2, (uint64_t[]){vehicle, other}) & 1); // IS_ENTITY_TOUCHING_ENTITY
              bool close = false;
              if (!touching && collided && hitVehTime < 500) {
                float op[3];
                close = pg_hit_position(instance, other, op) && sqrtf((op[0] - vx) * (op[0] - vx) + (op[1] - vy) * (op[1] - vy)) < 7.5f;
              }
              if (touching || close) { hitTarget = true; started++; pg_hit_start(instance, other, true, dirX, dirY, now, player); }
            }
            break;
          }
        }

        // 2. NPC: bị hất văng lên rồi nổ (chỉ khi thực sự đâm trúng)
        static uint64_t gPedOutBuf = 0;
        if (!gPedOutBuf) gPedOutBuf = PG_FN_OPERATOR_NEW(instance, 64);
        static const float pedProbeDist[3] = {3.0f, 4.4f, 5.8f};
        u32 nearbyPed = 0;
        if (gPedOutBuf) {
          for (int k = 0; k < 3 && !nearbyPed; k++) {
            float px = vx + dirX * pedProbeDist[k], py = vy + dirY * pedProbeDist[k];
            *(uint32_t *)pg_pointer(&env, gPedOutBuf, 4) = 0;
            int found = (int)(pg_test_call(instance, 0xc33ab876a77f8164ull, 10,
              (uint64_t[]){pg_f32_arg(px), pg_f32_arg(py), pg_f32_arg(vz), pg_f32_arg(2.6f), 1, 1, gPedOutBuf, 0, 0, (uint32_t)-1}) & 1); // GET_CLOSEST_PED
            if (found) nearbyPed = pg_hit_valid_candidate(*(uint32_t *)pg_pointer(&env, gPedOutBuf, 4), ped);
          }
        }
        if (nearbyPed) {
          int touchingPed = (int)(pg_test_call(instance, 0x17ffc1b2ba35a494ull, 2, (uint64_t[]){vehicle, nearbyPed}) & 1); // IS_ENTITY_TOUCHING_ENTITY
          int pedDead = (int)(pg_test_call(instance, 0x5f9532f3b5cc2551ull, 1, (uint64_t[]){nearbyPed}) & 1); // IS_ENTITY_DEAD
          int pedInCar = (int)(pg_test_call(instance, 0x997abd671d25ca0bull, 2, (uint64_t[]){nearbyPed, 0}) & 1); // IS_PED_IN_ANY_VEHICLE
          if (!pedDead && !pedInCar && (touchingPed || (collided && hitPedTime < 350))) {
            hitTarget = true;
            pg_hit_start(instance, nearbyPed, false, dirX, dirY, now, player);
          }
        }
      }

      // 3. CHỈ ĐÂM VÀO CÂY HOẶC TÒA NHÀ CỐ ĐỊNH MỚI BAY LÊN TRỜI (KHÔNG TRÚNG XE/NPC)
      // Rơi xuống tiếp đất KHÔNG ĐƯỢC BAY TIẾP, vẫn chạy bình thường
      if (!hitTarget && !inAir && !recentlyLanded && (collided && suddenDecel) && lastSpeed >= 7.5f && (now - lastBuildingJumpTime > 3000)) {
        lastBuildingJumpTime = now;
        lastAirTime = now;
        // Bay lên vừa phải (vz = 14.0f, cao khoảng 10-12m) kèm xung lực lướt tới
        float jumpX = dirX * 8.0f;
        float jumpY = dirY * 8.0f;
        pg_test_call(instance, 0x1c99bb7b6e96d16full, 4,
          (uint64_t[]){vehicle, pg_f32_arg(jumpX), pg_f32_arg(jumpY), pg_f32_arg(14.0f)}); // SET_ENTITY_VELOCITY vz = 14m/s
      }
    }
  }
  lastSpeed = currentSpeed;
  pg_asi_leave_script(instance, previous);
}

static void pg_native_cheats_tick(w2c_game *instance) {
  pg_streaming_position_tick();
  pg_scene_transition_trace();
  // Luôn dọn dẹp triệt để mọi hiệu ứng làm mờ màn hình mỗi frame trong script context
  pg_clear_all_screen_blur(instance);
  pg_story_watch_tick();
  pg_vehicle_god_mod_tick(instance);
  pg_native_cheats_action_tick(instance);
  pg_apply_pending_cheat_switches(instance);
  if ((!cheatGodMode && !cheatNeverWanted) || PGPauseMenuActive() ||
      PGGameStatus() != 0 || PGLoadSceneActive()) return;
  u64 previous = pg_asi_enter_script(instance);
  u32 player = PG_FN_PLAYER_ID(instance);
  if (player < 32 && PG_FN_PLAYER_PED_ID(instance)) {
    // Asks for the current player every time: a character switch or a respawn makes a new one, and
    // scripts can take invincibility away or raise the wanted limit. Writes only what changed.
    if (cheatGodMode && !PG_FN_GET_PLAYER_INVINCIBLE(instance, player))
      PG_FN_SET_PLAYER_INVINCIBLE(instance, player, 1);
    if (cheatNeverWanted) {
      u32 limit = PG_FN_GET_MAX_WANTED(instance);
      if (limit != 0) {
        cheatPreviousMaxWanted = limit;
        PG_FN_SET_MAX_WANTED(instance, 0);
      }
      if (PG_FN_GET_WANTED(instance, player) != 0) PG_FN_CLEAR_WANTED(instance, player);
    }
  }
  pg_asi_leave_script(instance, previous);
}
