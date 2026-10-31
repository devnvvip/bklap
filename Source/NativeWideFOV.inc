// Wide field of view & Camera handler. Included by EngineHost.c after NativeTurbo.inc
//
// Giữ nguyên 100% camera gốc của game và để hai menu mod F7 (DynamicVehicleFirstPerson) & F8 (Menyoo)
// tự do điều chỉnh camera theo lựa chọn của người chơi, không ép buộc can thiệp native camera.
// Báo trạng thái lái xe cho Metal renderer để khóa cứng 80 FPS khi đi bộ và 75 FPS khi lái xe.

static _Atomic bool wfEnabled;
static _Atomic float wfFoot = 1.0f, wfVehicle = 1.0f;

void PGSetPlayerDriving(int inVehicle);
void PGSetWideFOV(bool enabled, float foot, float vehicle) {
  foot = isfinite(foot) ? fminf(fmaxf(foot, 1.0f), 1.6f) : 1.0f;
  vehicle = isfinite(vehicle) ? fminf(fmaxf(vehicle, 1.0f), 1.6f) : 1.0f;
  atomic_store(&wfFoot, foot); atomic_store(&wfVehicle, vehicle); atomic_store(&wfEnabled, enabled);
}

// Hủy hoàn toàn bất kỳ script camera tùy ý nào còn sót lại để nhường quyền 100% cho gameplay gốc & mod menu
static void pg_wf_destroy(w2c_game *instance, int *cam, bool *active) {
  if (*active) pg_test_call(instance, 0x07e5b515db0636fcull, 5, (uint64_t[]){0, 0, 0, 1, 0});            // RENDER_SCRIPT_CAMS(false, false, 0, true, false)
  if (*cam && (pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)*cam}) & 1)) {   // DOES_CAM_EXIST
    pg_test_call(instance, 0x026fb97d0a425f84ull, 2, (uint64_t[]){(uint32_t)*cam, 0});                  // SET_CAM_ACTIVE(cam, false)
    pg_test_call(instance, 0x865908c81a2c22e9ull, 2, (uint64_t[]){(uint32_t)*cam, 0});                  // DESTROY_CAM(cam, false)
  }
  *cam = 0; *active = false;
}

static void pg_wide_fov_tick(w2c_game *instance) {
  static int cam; static bool active;

  if (active || cam) {
    u64 previous = pg_asi_enter_script(instance);
    pg_wf_destroy(instance, &cam, &active);
    pg_asi_leave_script(instance, previous);
  }

  const bool blocked = PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive();
  if (blocked) return;

  u64 previous = pg_asi_enter_script(instance);
  const u32 ped = PG_FN_PLAYER_PED_ID(instance);
  if (!ped) { pg_asi_leave_script(instance, previous); return; }

  const u32 vehicle = PG_FN_PED_VEHICLE(instance, ped, 0);

  // Báo cho Metal Renderer trạng thái lái xe: Đi bộ 80 FPS, Đi xe 75 FPS (-5 FPS), lock tuyệt đối!
  PGSetPlayerDriving(vehicle != 0);

  // Tối ưu hóa khi ở trong nhà (interior): Tạm dừng render ngoại cảnh thế giới bên ngoài (exterior)
  // để loại bỏ hoàn toàn việc render trùng lặp gây tụt FPS và giật lag khi di chuyển trong nhà.
  // Khi bước ra ngoài cửa: tự động tiếp tục render ngoại cảnh bình thường.
  static uint64_t lastInterior = 999999;
  uint64_t interior = pg_test_call(instance, 0x2107ba504071a6bbull, 1, (uint64_t[]){ped}); // GET_INTERIOR_FROM_ENTITY
  if (interior != lastInterior) {
    lastInterior = interior;
    pg_test_call(instance, 0x35cce12eaecb4a51ull, 1, (uint64_t[]){interior != 0 ? 1 : 0}); // _SET_PAUSE_EXTERIOR_RENDERING_WHILE_IN_INTERIOR
  }

  // Triệt tiêu rung giật do động cơ tốc độ cao gây ra
  pg_test_call(instance, 0x0ef93e9f3d08c178ull, 1, (uint64_t[]){1}); // STOP_GAMEPLAY_CAM_SHAKING(true)
  pg_test_call(instance, 0xa87e00932db4d85dull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_SHAKE_AMPLITUDE(0.0f)

  pg_asi_leave_script(instance, previous);
}
