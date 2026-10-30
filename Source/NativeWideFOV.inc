// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc (it needs pg_test_call,
// pg_wasm_text and the script helpers) and run by pg_asi_script_tick on the game thread, never from UIKit.
//
// A script camera copies the gameplay camera position and rotation smoothly without stuttering or jitter,
// and widens the field of view according to the player's settings.
// When switching camera view modes (first person, cinematic, bumper, etc.), cutscenes or missions with
// dedicated cameras, the script camera gracefully releases control to the game's native camera director.
static _Atomic bool wfEnabled;
static _Atomic float wfFoot = 1.0f, wfVehicle = 1.0f;
void PGSetWideFOV(bool enabled, float foot, float vehicle) {
  foot = isfinite(foot) ? fminf(fmaxf(foot, 1.0f), 1.6f) : 1.0f;
  vehicle = isfinite(vehicle) ? fminf(fmaxf(vehicle, 1.0f), 1.6f) : 1.0f;
  atomic_store(&wfFoot, foot); atomic_store(&wfVehicle, vehicle); atomic_store(&wfEnabled, enabled);
}
// A command that returns a Vector3 leaves three floats, 8 bytes apart, in the result block that pg_test_call fills.
static bool pg_wf_vector(w2c_game *instance, uint64_t hash, int count, const uint64_t *arguments, float out[3]) {
  if (pg_test_call(instance, hash, count, arguments) == (uint64_t)-1) return false;
  const uint8_t *result = pg_pointer(&env, pg_test_buffer + 8 * 32, 24);
  memcpy(&out[0], result, 4); memcpy(&out[1], result + 8, 4); memcpy(&out[2], result + 16, 4);
  return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}
// Gives the camera back to the game. Call with the "main" script entered (pg_asi_enter_script).
static void pg_wf_destroy(w2c_game *instance, int *cam, bool *active) {
  if (*active) pg_test_call(instance, 0x07e5b515db0636fcull, 5, (uint64_t[]){0, 0, 0, 1, 0});            // RENDER_SCRIPT_CAMS(false, false, 0, true, false)
  if (*cam && (pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)*cam}) & 1)) {   // DOES_CAM_EXIST
    pg_test_call(instance, 0x026fb97d0a425f84ull, 2, (uint64_t[]){(uint32_t)*cam, 0});                  // SET_CAM_ACTIVE(cam, false)
    pg_test_call(instance, 0x865908c81a2c22e9ull, 2, (uint64_t[]){(uint32_t)*cam, 0});                  // DESTROY_CAM(cam, false)
  }
  *cam = 0; *active = false;
}
static void pg_wide_fov_tick(w2c_game *instance) {
  static int cam; static bool active; static float factor = 1.0f;
  static double pauseUntil, activeSince; static uint64_t nameSlot;
  static int lastViewMode = -1;
  static double lastTickTime = 0.0;
  const bool enabled = atomic_load(&wfEnabled);
  const bool blocked = PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive();
  if (!enabled || blocked) {
    if (active) { u64 previous = pg_asi_enter_script(instance); pg_wf_destroy(instance, &cam, &active); pg_asi_leave_script(instance, previous); }
    factor = 1.0f; lastViewMode = -1;
    return;
  }
  const double now = timeMS(CLOCK_MONOTONIC);
  if (now < pauseUntil) return;
  u64 previous = pg_asi_enter_script(instance);
  const u32 ped = PG_FN_PLAYER_PED_ID(instance), vehicle = ped ? PG_FN_PED_VEHICLE(instance, ped, 0) : 0;
  const float target = vehicle ? atomic_load(&wfVehicle) : atomic_load(&wfFoot);

  // Kiểm tra chế độ góc nhìn camera hiện tại:
  // Mode 0: Close, 1: Medium, 2: Far, 4: First person, 3/5: Cinematic / Bumper / Hood
  const uint64_t vmVal = ped ? pg_test_call(instance, vehicle ? 0xa4ff579ac0e3aaaeull : 0x8d4d46230b2c353aull, 0, NULL) : 0;
  const int viewMode = (int)(int32_t)(uint32_t)vmVal;

  // First person (mode 4 hoặc khi ngắm bắn góc nhìn thứ nhất): tuyệt đối không can thiệp
  const bool firstPerson = ped && (viewMode == 4 || (pg_test_call(instance, 0x5e346d934122613full, 0, NULL) & 1));
  // Các góc nhìn đặc biệt khác: Cinematic (mode 3), Bumper/Hood (mode 5 khi lái xe)
  const bool specialCam = vehicle && (viewMode == 3 || viewMode == 5);
  if (firstPerson || specialCam) {
    if (active) pg_wf_destroy(instance, &cam, &active);
    factor = 1.0f; pauseUntil = now + 150;
    pg_asi_leave_script(instance, previous); return;
  }

  // Khi người chơi bấm nút đổi góc cam (Close -> Mid -> Far):
  // Nhả script camera 800ms để hoạt ảnh chuyển đổi góc nhìn của Rockstar diễn ra hoàn toàn mượt mà tự nhiên, không giật nảy!
  if (lastViewMode != -1 && viewMode != lastViewMode) {
    lastViewMode = viewMode;
    if (active) pg_wf_destroy(instance, &cam, &active);
    factor = 1.0f; pauseUntil = now + 800;
    pg_asi_leave_script(instance, previous); return;
  }
  lastViewMode = viewMode;

  const bool scripted = !ped || (pg_test_call(instance, 0xd3c2e180a40f031eull, 0, NULL) & 1)             // IS_CUTSCENE_PLAYING
                        || (pg_test_call(instance, 0xd9d2cfff49fab35full, 0, NULL) & 1);                  // IS_PLAYER_SWITCH_IN_PROGRESS
  if (scripted) {
    if (active) pg_wf_destroy(instance, &cam, &active);
    factor = 1.0f; pauseUntil = now + 500;
    pg_asi_leave_script(instance, previous); return;
  }

  // Script khác đang dùng camera (nhiệm vụ cốt truyện): nhường quyền cho game
  if (active && now - activeSince > 300 && !(pg_test_call(instance, 0x02ec0af5c5a49b7aull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {   // IS_CAM_RENDERING
    pg_wf_destroy(instance, &cam, &active); factor = 1.0f; pauseUntil = now + 4000;
    pg_asi_leave_script(instance, previous); return;
  }

  // Chuyển đổi mềm mại độ mở rộng FOV
  factor += (target - factor) * 0.1f;
  if (fabsf(target - 1.0f) < 0.005f && fabsf(factor - 1.0f) < 0.005f) {      // Không mở rộng: để camera game gốc hoàn toàn
    if (active) { pg_wf_destroy(instance, &cam, &active); pauseUntil = now + 200; }
    factor = 1.0f; pg_asi_leave_script(instance, previous); return;
  }

  if (!active && !(pg_test_call(instance, 0x39b5d1b10383f0c8ull, 0, NULL) & 1)) {   // IS_GAMEPLAY_CAM_RENDERING
    pauseUntil = now + 1000; pg_asi_leave_script(instance, previous); return;
  }

  if (!cam || !(pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {
    uint64_t name = pg_wasm_text(&nameSlot, "DEFAULT_SCRIPTED_CAMERA");
    cam = name ? (int)(uint32_t)pg_test_call(instance, 0xc3981dce61d9e13full, 2, (uint64_t[]){name, 1}) : 0;   // CREATE_CAM
    active = false;
    if (cam <= 0 || !(pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {
      cam = 0; pauseUntil = now + 3000; pg_asi_leave_script(instance, previous); return;
    }
    // Cấu hình camera script mượt mà: không ngắm bắn xung đột và KHÔNG kế thừa roll xe để chống rung lắc khung hình
    pg_test_call(instance, 0xb82cb5da673752eull, 2, (uint64_t[]){(uint32_t)cam, 0}); // SET_CAM_AFFECTS_AIMING(cam, false)
    pg_test_call(instance, 0x17fca7199c0897f2ull, 2, (uint64_t[]){(uint32_t)cam, 0}); // SET_CAM_INHERIT_ROLL_VEHICLE(cam, false)
  }

  // Triệt tiêu hoàn toàn hiệu ứng rung giật tốc độ cao của xe do game Rockstar tạo ra
  pg_test_call(instance, 0x0ef93e9f3d08c178ull, 1, (uint64_t[]){1}); // STOP_GAMEPLAY_CAM_SHAKING(true)
  pg_test_call(instance, 0xa87e00932db4d85dull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_SHAKE_AMPLITUDE(0.0f)
  if (cam) {
    pg_test_call(instance, 0xbdecf64367884ac3ull, 2, (uint64_t[]){(uint32_t)cam, 1}); // STOP_CAM_SHAKING(cam, true)
    pg_test_call(instance, 0xd93db43b82bc0d00ull, 2, (uint64_t[]){(uint32_t)cam, pg_f32_arg(0.0f)}); // SET_CAM_SHAKE_AMPLITUDE(cam, 0.0f)
  }

  float position[3], rotation[3], fov;
  const uint64_t fovBits = pg_test_call(instance, 0x65019750a0324133ull, 0, NULL);                         // GET_GAMEPLAY_CAM_FOV
  memcpy(&fov, &fovBits, 4);
  if (!pg_wf_vector(instance, 0x14d6f5678d8f1b37ull, 0, NULL, position)                                    // GET_GAMEPLAY_CAM_COORD
      || !pg_wf_vector(instance, 0x837765a25378f0bbull, 1, (uint64_t[]){2}, rotation)                      // GET_GAMEPLAY_CAM_ROT(2)
      || !(fov > 5.0f && fov < 130.0f)) {
    pg_asi_leave_script(instance, previous); return;
  }

  // Bù trễ vật lý 1 frame của xe khi đang chạy tốc độ cao:
  // Lấy vector vận tốc thực tế của xe từ physics engine (GET_ENTITY_VELOCITY 0x4805d2b1d8cf94a9ull).
  // Vì lấy trực tiếp vận tốc vật lý của xe chứ không lấy hiệu tọa độ camera, nên vuốt xoay màn hình
  // không bị giật hay nảy tiếp tuyến, đồng thời camera và xe luôn bám dính nhau siêu mượt!
  float finalX = position[0];
  float finalY = position[1];
  float finalZ = position[2];
  if (vehicle) {
    float vel[3] = {0, 0, 0};
    if (pg_wf_vector(instance, 0x4805d2b1d8cf94a9ull, 1, (uint64_t[]){vehicle}, vel)) {
      float dt = (lastTickTime > 0.0) ? (float)((now - lastTickTime) * 0.001) : (1.0f / 60.0f);
      if (dt <= 0.0f || dt > 0.05f) dt = 1.0f / 60.0f;
      finalX += vel[0] * dt;
      finalY += vel[1] * dt;
      finalZ += fminf(fmaxf(vel[2] * dt, -0.04f), 0.04f);
    }
  }
  lastTickTime = now;

  const float widened = fminf(fov * factor, 110.0f);

  pg_test_call(instance, 0x4d41783fb745e42eull, 4, (uint64_t[]){(uint32_t)cam, pg_f32_arg(finalX), pg_f32_arg(finalY), pg_f32_arg(finalZ)});   // SET_CAM_COORD
  pg_test_call(instance, 0x85973643155d0b07ull, 5, (uint64_t[]){(uint32_t)cam, pg_f32_arg(rotation[0]), pg_f32_arg(rotation[1]), pg_f32_arg(rotation[2]), 2});               // SET_CAM_ROT
  pg_test_call(instance, 0xb13c14f66a00d047ull, 2, (uint64_t[]){(uint32_t)cam, pg_f32_arg(widened)});                                                                      // SET_CAM_FOV

  if (!active) {
    pg_test_call(instance, 0x026fb97d0a425f84ull, 2, (uint64_t[]){(uint32_t)cam, 1});                    // SET_CAM_ACTIVE(cam, true)
    pg_test_call(instance, 0x07e5b515db0636fcull, 5, (uint64_t[]){1, 0, 0, 1, 0});                       // RENDER_SCRIPT_CAMS(true, false, 0, true, false)
    active = true; activeSince = now;
    pg_log("Wide FOV: script camera active (smooth velocity tracking, zero jitter).");
  }
  pg_asi_leave_script(instance, previous);
}
