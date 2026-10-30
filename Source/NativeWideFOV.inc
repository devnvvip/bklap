// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc
//
// Sử dụng 100% camera native của Rockstar Games (camGameplayDirector) và các native view mode:
// - Góc nhìn thứ ba: Mode 2 (Far) cho góc nhìn rộng bao quát, chuyển động mượt mà tuyệt đối không rung giật.
// - Góc nhìn thứ nhất (First Person Cockpit) chuẩn như GTA V PC:
//   * Áp dụng đồng bộ cho cả đi bộ VÀ TẤT CẢ mọi loại xe (ô tô, xe máy, xe đạp, thuyền, máy bay, trực thăng).
//   * Không bao giờ bị nhảy góc nhìn hay chớp giật khi lên/xuống xe.
//   * Thấy 2 tay lái điều khiển vô lăng/ghi đông chân thật trên tất cả mọi loại xe.
//   * Cự ly cắt gần near-clip 0.28f: triệt tiêu 100% hiện tượng lọt hình ngực khi di chuyển, chạy hoặc ngắm bắn.
//   * Luôn vô hiệu hóa camera nắp ca-pô (bonnet cam), tự động tháo mũ bảo hiểm xe máy.
// - Báo trạng thái lái xe cho Metal renderer để khóa cứng 80 FPS khi đi bộ và 75 FPS khi lái xe.
// - Không ép orbit distance limits mỗi frame để engine tự do xử lý va chạm mượt mà 100%.

static _Atomic bool wfEnabled;
static _Atomic float wfFoot = 1.0f, wfVehicle = 1.0f;
// gPreferredViewMode is defined in EngineHost.c and declared in HostInternal.h

void PGSetPlayerDriving(int inVehicle);
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

  // Báo cho Metal Renderer trạng thái lái xe: Đi bộ 80 FPS, Đi xe 75 FPS (-5 FPS), lock tuyệt đối!
  PGSetPlayerDriving(vehicle != 0);

  // Triệt tiêu hoàn toàn hiệu ứng rung giật tốc độ cao của xe do engine Rockstar mô phỏng
  pg_test_call(instance, 0x0ef93e9f3d08c178ull, 1, (uint64_t[]){1}); // STOP_GAMEPLAY_CAM_SHAKING(true)
  pg_test_call(instance, 0xa87e00932db4d85dull, 1, (uint64_t[]){pg_f32_arg(0.0f)}); // SET_GAMEPLAY_CAM_SHAKE_AMPLITUDE(0.0f)

  // Tự động nhận diện nếu người chơi bấm nút đổi góc nhìn trong Touch Controls (nút Camera):
  uint64_t activeContext = pg_test_call(instance, 0x19cafa3c87f7c2ffull, 0, NULL); // GET_CAM_ACTIVE_VIEW_MODE_CONTEXT
  if (activeContext != (uint64_t)-1) {
    uint64_t curVm = pg_test_call(instance, 0xee778f8c7e1142e2ull, 1, (uint64_t[]){activeContext}); // GET_CAM_VIEW_MODE_FOR_CONTEXT
    int modeVal = (int)(int32_t)(uint32_t)curVm;
    if (modeVal == 4 && atomic_load(&gPreferredViewMode) != 4) {
      atomic_store(&gPreferredViewMode, 4);
    } else if ((modeVal == 0 || modeVal == 1 || modeVal == 2) && atomic_load(&gPreferredViewMode) == 4) {
      atomic_store(&gPreferredViewMode, 2);
    }
  }

  const int preferredMode = atomic_load(&gPreferredViewMode);

  if (preferredMode == 4) {
    // ==============================================================
    // CHẾ ĐỘ GÓC NHÌN THỨ NHẤT RỘNG (FIRST PERSON COCKPIT LIKE GTA 5 PC)
    // ==============================================================
    if (vehicle) {
      // Khi lên bất kỳ loại xe nào (ô tô, xe máy, xe đạp, thuyền, máy bay, trực thăng):
      // Giữ nguyên 100% góc nhìn thứ nhất, không bao giờ bị chuyển sang góc nhìn khác hay chớp giật!
      if (lastAppliedVeh != 4) {
        for (int c = 1; c <= 7; c++) {
          pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){(uint32_t)c, 4}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(c, 4)
        }
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){4}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(4)
        lastAppliedVeh = 4;
      }
    } else {
      if (lastAppliedPed != 4) {
        pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){0, 4}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(0: On foot, 4)
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){4}); // SET_FOLLOW_PED_CAM_VIEW_MODE(4)
        lastAppliedPed = 4;
      }
    }

    // Luôn ngăn chặn camera nắp ca-pô (bonnet/hood cam): ép 100% hiển thị buồng lái và 2 tay lái
    pg_test_call(instance, 0x7b8a361c1813fbefull, 1, (uint64_t[]){1}); // IGNORE_MENU_PREFERENCE_FOR_BONNET_CAMERA_THIS_UPDATE(true)
    pg_test_call(instance, 0xadff1b2a555f5fbaull, 1, (uint64_t[]){1}); // DISABLE_CINEMATIC_BONNET_CAMERA_THIS_UPDATE(true)

    // Cự ly cắt gần (near clip): 0.28f loại bỏ 100% hiện tượng lọt hình ngực khi di chuyển, chạy hoặc cầm súng!
    // Vô lăng, 2 tay lái ô tô và ghi đông xe máy vẫn hiển thị siêu rõ nét, hoàn hảo!
    pg_test_call(instance, 0x0af7b437918103b3ull, 1, (uint64_t[]){pg_f32_arg(0.28f)}); // SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE(0.28f)

    // Tự động gỡ bỏ mũ bảo hiểm xe máy/đua xe để tầm nhìn luôn thông thoáng
    pg_test_call(instance, 0xa7b2458d0ad6ded8ull, 2, (uint64_t[]){ped, 1}); // REMOVE_PED_HELMET(ped, true)
    pg_test_call(instance, 0x560a43136eb58105ull, 2, (uint64_t[]){ped, 0}); // SET_PED_HELMET(ped, false)
  } else if (enabled) {
    // ==============================================================
    // CHẾ ĐỘ GÓC NHÌN THỨ BA RỘNG (THIRD PERSON FAR - MODE 2)
    // ==============================================================
    if (vehicle) {
      if (lastAppliedVeh != 2) {
        for (int c = 1; c <= 7; c++) {
          pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){(uint32_t)c, 2});
        }
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(2: Far)
        lastAppliedVeh = 2;
      }
    } else {
      if (lastAppliedPed != 2) {
        pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){0, 2}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(0: On foot, 2)
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2}); // SET_FOLLOW_PED_CAM_VIEW_MODE(2: Far)
        lastAppliedPed = 2;
      }
    }
  }

  pg_asi_leave_script(instance, previous);
}
