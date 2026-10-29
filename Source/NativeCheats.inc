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

static void pg_vehicle_god_mod_tick(w2c_game *instance) {
  if (PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive()) return;
  u32 ped = PG_FN_PLAYER_PED_ID(instance);
  if (!ped) return;
  u32 vehicle = PG_FN_PED_VEHICLE(instance, ped, 0);
  if (!vehicle) return;

  u64 previous = pg_asi_enter_script(instance);
  u32 player = PG_FN_PLAYER_ID(instance);
  if (player < 32) {
    PG_FN_SET_PLAYER_INVINCIBLE(instance, player, 1);
  }
  // Bất tử cho người chơi và xe, không bao giờ chết hay nổ
  PG_FN_VEHICLE_ENGINE_HEALTH(instance, vehicle, 1000.f);
  PG_FN_REPAIR_VEHICLE(instance, vehicle);
  pg_test_call(instance, 0x3882114bde571ad4ull, 2, (uint64_t[]){vehicle, 1}); // SET_ENTITY_INVINCIBLE
  pg_test_call(instance, 0xfaee099c6f890bb8ull, 9, (uint64_t[]){vehicle, 1, 1, 1, 1, 1, 1, 1, 1}); // SET_ENTITY_PROOFS

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

  // Xử lý va chạm: Đâm xe hay NPC thì mục tiêu bay ra xa rồi nổ tung!
  static float lastSpeed = 0.0f;
  static double lastActionTime = 0.0;
  double now = timeMS(CLOCK_MONOTONIC);
  float currentSpeed = PG_FN_GET_ENTITY_SPEED(instance, vehicle);

  int collided = (int)(pg_test_call(instance, 0x8bad02f0368d9e14ull, 1, (uint64_t[]){vehicle}) & 1); // HAS_ENTITY_COLLIDED_WITH_ANYTHING
  uint32_t hitVehTime = (uint32_t)pg_test_call(instance, 0x5d35ecf3a81a0ee0ull, 1, (uint64_t[]){player}); // GET_TIME_SINCE_PLAYER_HIT_VEHICLE
  uint32_t hitPedTime = (uint32_t)pg_test_call(instance, 0xe36a25322dc35f42ull, 1, (uint64_t[]){player}); // GET_TIME_SINCE_PLAYER_HIT_PED
  bool hitRecently = (hitVehTime < 300) || (hitPedTime < 300);
  bool suddenDecel = (lastSpeed - currentSpeed) > 3.0f && lastSpeed > 4.0f;

  if ((collided || hitRecently || suddenDecel) && (now - lastActionTime > 250)) {
    if (vAddr) {
      const float *pos = pg_pointer(&env, vAddr + 144, 12);
      float vx = pos[0], vy = pos[1], vz = pos[2];
      float heading = PG_FN_GET_ENTITY_HEADING(instance, vehicle);
      float rad = heading * 0.0174532925f; // Chuyển đổi sang radian
      float frontX = vx - sinf(rad) * 3.5f;
      float frontY = vy + cosf(rad) * 3.5f;

      bool hitTarget = false;

      // 1. Thử lấy thực thể va chạm gần nhất
      u32 hitEntity = (u32)pg_test_call(instance, 0xa75ee4f689b85391ull, 1, (uint64_t[]){vehicle}); // _GET_LAST_ENTITY_HIT_BY_ENTITY

      // 2. Tìm xe khác ở vị trí va chạm và mũi xe
      u32 otherVehicle = 0;
      if (hitEntity && (int)(pg_test_call(instance, 0x6ac7003fa6e5575eull, 1, (uint64_t[]){hitEntity}) & 1) && hitEntity != vehicle) {
        otherVehicle = hitEntity;
      } else {
        otherVehicle = (u32)pg_test_call(instance, 0xf73eb622c4f1689bull, 6,
          (uint64_t[]){pg_f32_arg(frontX), pg_f32_arg(frontY), pg_f32_arg(vz), pg_f32_arg(6.0f), 0, 0});
        if (!otherVehicle || otherVehicle == vehicle) {
          otherVehicle = (u32)pg_test_call(instance, 0xf73eb622c4f1689bull, 6,
            (uint64_t[]){pg_f32_arg(vx), pg_f32_arg(vy), pg_f32_arg(vz), pg_f32_arg(5.0f), 0, 0});
        }
      }

      if (otherVehicle && otherVehicle != vehicle) {
        lastActionTime = now;
        hitTarget = true;
        // Hất xe NPC bay vọt ra xa lên trời
        float flingDirX = -sinf(rad) * 38.0f;
        float flingDirY = cosf(rad) * 38.0f;
        pg_test_call(instance, 0x1c99bb7b6e96d16full, 4,
          (uint64_t[]){otherVehicle, pg_f32_arg(flingDirX), pg_f32_arg(flingDirY), pg_f32_arg(22.0f)}); // SET_ENTITY_VELOCITY
        // Nổ tung xe NPC
        pg_test_call(instance, 0xba71116adf5b514cull, 3, (uint64_t[]){otherVehicle, 1, 0}); // EXPLODE_VEHICLE
        pg_test_call(instance, 0xe3ad2bdbaee269acull, 8,
          (uint64_t[]){pg_f32_arg(frontX), pg_f32_arg(frontY), pg_f32_arg(vz), 2, pg_f32_arg(2.0f), 1, 0, pg_f32_arg(1.0f)}); // ADD_EXPLOSION
      }

      // 3. Tìm NPC ở gần vị trí mũi xe
      u32 nearbyPed = 0;
      if (hitEntity && (int)(pg_test_call(instance, 0x524ac5ecea15343eull, 1, (uint64_t[]){hitEntity}) & 1) && hitEntity != ped) {
        nearbyPed = hitEntity;
      } else {
        nearbyPed = (u32)pg_test_call(instance, 0xc33ab876a77f8164ull, 10,
          (uint64_t[]){pg_f32_arg(frontX), pg_f32_arg(frontY), pg_f32_arg(vz), pg_f32_arg(4.5f), 1, 1, 0, 0, 0, (uint32_t)-1});
      }

      if (nearbyPed && nearbyPed != ped) {
        lastActionTime = now;
        hitTarget = true;
        float flingDirX = -sinf(rad) * 32.0f;
        float flingDirY = cosf(rad) * 32.0f;
        pg_test_call(instance, 0x1c99bb7b6e96d16full, 4,
          (uint64_t[]){nearbyPed, pg_f32_arg(flingDirX), pg_f32_arg(flingDirY), pg_f32_arg(18.0f)}); // Hất NPC bay vọt ra xa
        pg_test_call(instance, 0xe3ad2bdbaee269acull, 8,
          (uint64_t[]){pg_f32_arg(frontX), pg_f32_arg(frontY), pg_f32_arg(vz), 2, pg_f32_arg(2.0f), 1, 0, pg_f32_arg(1.0f)}); // Nổ tung
      }

      // 4. Nếu đâm vào tường / tòa nhà cố định với tốc độ cao (không trúng xe hay NPC) -> hất xe ta bay thẳng lên trời!
      if (!hitTarget && (collided || suddenDecel) && (lastSpeed >= 5.0f || currentSpeed >= 5.0f)) {
        lastActionTime = now;
        pg_test_call(instance, 0x1c99bb7b6e96d16full, 4,
          (uint64_t[]){vehicle, 0, 0, pg_f32_arg(48.0f)}); // SET_ENTITY_VELOCITY vz = 48m/s
      }
    }
  }
  lastSpeed = currentSpeed;
  pg_asi_leave_script(instance, previous);
}

static void pg_native_cheats_tick(w2c_game *instance) {
  pg_streaming_position_tick();
  pg_scene_transition_trace();
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
