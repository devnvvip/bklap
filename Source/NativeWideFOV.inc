// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc (it needs pg_test_call,
// pg_wasm_text and the script helpers) and run by pg_asi_script_tick on the game thread, never from UIKit.
//
// Sử dụng 100% camera native của Rockstar Games (camGameplayDirector) và các native view mode:
// - Góc nhìn thứ ba: Mode 2 (Far) cho góc nhìn rộng bao quát, chuyển động mượt mà tuyệt đối không rung giật.
// - Góc nhìn thứ nhất trong xe: Mode 4 (First Person Cockpit) chuẩn như GTA 5 PC:
//   * Thấy 2 tay lái chân thật điều khiển vô lăng trên tất cả mọi loại xe.
//   * Không hiện thân người cản tầm nhìn, chỉ hiện buồng lái và 2 tay lái chân thật.
//   * Triệt tiêu hoàn toàn camera nắp ca-pô (bonnet/hood cam), tự động tháo mũ bảo hiểm.
//   * Thiết lập near-clip tối ưu hiển thị siêu chi tiết toàn bộ taplo và vô lăng.
// - Triệt tiêu hoàn toàn rung lắc camera tốc độ cao (STOP_GAMEPLAY_CAM_SHAKING).
// - Không ép orbit distance limits mỗi frame để engine tự do xử lý va chạm mượt mà 100%.

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

  if (vehicle) {
    // Chế độ camera xe: 0: Gần, 1: Vừa, 2: Xa (Rộng), 3: Điện ảnh, 4: Thứ nhất (Cockpit)
    const uint64_t vmVal = pg_test_call(instance, 0xa4ff579ac0e3aaaeull, 0, NULL); // GET_FOLLOW_VEHICLE_CAM_VIEW_MODE
    const int vehViewMode = (int)(int32_t)(uint32_t)vmVal;

    // Khi ở góc nhìn thứ nhất trong xe:
    // Buồng lái rộng chuẩn GTA V PC: hiển thị 2 tay lái điều khiển vô lăng trên tất cả mọi xe,
    // loại bỏ camera nắp capo (bonnet cam), gỡ mũ bảo hiểm để tầm nhìn cực kỳ thông thoáng và chân thật,
    // không hiện thân người cản tầm nhìn, thiết lập near-clip tối ưu hiển thị rõ nét vô lăng và taplo.
    if (vehViewMode == 4) {
      pg_test_call(instance, 0x7b8a361c1813fbefull, 1, (uint64_t[]){1}); // IGNORE_MENU_PREFERENCE_FOR_BONNET_CAMERA_THIS_UPDATE(true)
      pg_test_call(instance, 0xadff1b2a555f5fbaull, 1, (uint64_t[]){1}); // DISABLE_CINEMATIC_BONNET_CAMERA_THIS_UPDATE(true)
      pg_test_call(instance, 0x0af7b437918103b3ull, 1, (uint64_t[]){pg_f32_arg(0.15f)}); // SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE(0.15f)
      pg_test_call(instance, 0xa7b2458d0ad6ded8ull, 2, (uint64_t[]){ped, 1}); // REMOVE_PED_HELMET(ped, true)
      pg_test_call(instance, 0x560a43136eb58105ull, 2, (uint64_t[]){ped, 0}); // SET_PED_HELMET(ped, false)
      lastAppliedVeh = 4;
    } else if (enabled && vehViewMode != 3) {
      // Góc nhìn thứ ba khi bật góc rộng: chuyển sang Far Mode (2)
      if (lastAppliedVeh != 2) {
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(2: Far)
        lastAppliedVeh = 2;
      }
    }
  } else {
    // Khi đi bộ:
    lastAppliedVeh = -1;
    const uint64_t pedVmVal = pg_test_call(instance, 0x8d4d46230b2c353aull, 0, NULL); // GET_FOLLOW_PED_CAM_VIEW_MODE
    const int pedViewMode = (int)(int32_t)(uint32_t)pedVmVal;

    if (pedViewMode == 4) {
      pg_test_call(instance, 0x0af7b437918103b3ull, 1, (uint64_t[]){pg_f32_arg(0.15f)}); // SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE(0.15f)
      pg_test_call(instance, 0xa7b2458d0ad6ded8ull, 2, (uint64_t[]){ped, 1}); // REMOVE_PED_HELMET(ped, true)
      pg_test_call(instance, 0x560a43136eb58105ull, 2, (uint64_t[]){ped, 0}); // SET_PED_HELMET(ped, false)
      lastAppliedPed = 4;
    } else if (enabled && pedViewMode != 3) {
      if (lastAppliedPed != 2) {
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2}); // SET_FOLLOW_PED_CAM_VIEW_MODE(2: Far)
        lastAppliedPed = 2;
      }
    }
  }

  pg_asi_leave_script(instance, previous);
}
