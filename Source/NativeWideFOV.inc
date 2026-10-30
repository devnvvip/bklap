// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc (it needs pg_test_call,
// pg_wasm_text and the script helpers) and run by pg_asi_script_tick on the game thread, never from UIKit.
//
// Sử dụng 100% camera native của Rockstar Games (camGameplayDirector) và các native view mode (Mode 2: Far / Wide).
// TUYỆT ĐỐI KHÔNG dùng DEFAULT_SCRIPTED_CAMERA / RENDER_SCRIPT_CAMS để triệt tiêu hoàn toàn rung giật
// khi đi bộ hay lái xe ở tốc độ cao, và đảm bảo mọi góc nhìn khác (Gần, Vừa, Thứ nhất) không bị lây lỗi.
static _Atomic bool wfEnabled;
static _Atomic float wfFoot = 1.0f, wfVehicle = 1.0f;
void PGSetWideFOV(bool enabled, float foot, float vehicle) {
  foot = isfinite(foot) ? fminf(fmaxf(foot, 1.0f), 1.6f) : 1.0f;
  vehicle = isfinite(vehicle) ? fminf(fmaxf(vehicle, 1.0f), 1.6f) : 1.0f;
  atomic_store(&wfFoot, foot); atomic_store(&wfVehicle, vehicle); atomic_store(&wfEnabled, enabled);
}

// Hủy hoàn toàn bất kỳ script camera nào còn sót lại để nhường quyền 100% cho engine native
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
  static int lastAppliedPed = -1, lastAppliedVeh = -1;
  static bool wasEnabled = false;

  // Luôn hủy triệt để script camera nếu có: giải phóng camera gameplay native của game
  if (active || cam) {
    u64 previous = pg_asi_enter_script(instance);
    pg_wf_destroy(instance, &cam, &active);
    pg_asi_leave_script(instance, previous);
  }

  const bool enabled = atomic_load(&wfEnabled);
  const bool blocked = PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive();
  if (blocked) return;

  u64 previous = pg_asi_enter_script(instance);
  const u32 ped = PG_FN_PLAYER_PED_ID(instance);
  if (!ped) { pg_asi_leave_script(instance, previous); return; }

  const u32 vehicle = PG_FN_PED_VEHICLE(instance, ped, 0);

  // Triệt tiêu hoàn toàn hiệu ứng rung giật tốc độ cao của xe do engine Rockstar mô phỏng
  pg_test_call(instance, 0x0ef93e9f3d08c178ull, 1, (uint64_t[]){1}); // STOP_GAMEPLAY_CAM_SHAKING(true)
  pg_test_call(instance, 0xa87e00932db4d85dull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_SHAKE_AMPLITUDE(0.0f)

  if (enabled) {
    const float target = vehicle ? atomic_load(&wfVehicle) : atomic_load(&wfFoot);

    // Khi bật Góc nhìn rộng:
    // Đặt camera native sang chế độ Far (View Mode 2: góc nhìn xa rộng nhất của game).
    // Vì sử dụng 100% camera native của Rockstar Games, nên đi bộ hay chạy xe đều mượt mà 100%,
    // và các góc nhìn khác (Gần, Vừa, Thứ nhất) khi người chơi bấm nút đổi góc đều mượt mà tuyệt đối,
    // không bao giờ bị giật hay lây lỗi!
    if (!wasEnabled) {
      wasEnabled = true;
      lastAppliedPed = -1;
      lastAppliedVeh = -1;
    }

    if (vehicle) {
      if (lastAppliedVeh != 2) {
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(2: Far)
        lastAppliedVeh = 2;
      }
    } else {
      if (lastAppliedPed != 2) {
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2}); // SET_FOLLOW_PED_CAM_VIEW_MODE(2: Far)
        lastAppliedPed = 2;
      }
    }

    // Mở rộng cự ly quan sát native nếu người chơi chỉnh khoảng cách lớn hơn 1.0
    if (target > 1.05f) {
      pg_test_call(instance, 0xdf2e1f7742402e81ull, 2, (uint64_t[]){pg_f32_arg(1.0f), pg_f32_arg(5.5f * target)}); // SET_THIRD_PERSON_CAM_ORBIT_DISTANCE_LIMITS_THIS_UPDATE
    }
  } else {
    wasEnabled = false;
    lastAppliedPed = -1;
    lastAppliedVeh = -1;
  }

  pg_asi_leave_script(instance, previous);
}
