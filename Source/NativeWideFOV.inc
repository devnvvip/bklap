// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc
//
// Sử dụng 100% camera native của Rockstar Games (camGameplayDirector) và các native view mode:
// - Góc nhìn thứ ba: Mode 2 (Far) cho góc nhìn rộng bao quát, chuyển động mượt mà tuyệt đối không rung giật.
// - Góc nhìn thứ nhất (First Person Cockpit) chuẩn như GTA V PC:
//   * Áp dụng đồng bộ cho cả đi bộ VÀ TẤT CẢ mọi loại xe (ô tô, xe máy, xe đạp, thuyền, máy bay, trực thăng).
//   * Không bao giờ bị nhảy góc nhìn hay chớp giật khi lên/xuống xe.
//   * Thấy trọn vẹn cả cánh tay và vai cùng vô lăng/ghi đông chân thật trên tất cả mọi loại xe.
//   * Cự ly cắt gần near-clip 0.18f: thấy trọn vẹn tay và vai, không bị cắt cụt củn lủn, triệt tiêu lọt hình.
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
  static u32 lastVehicle = 0;
  static int lastAppliedMode = -1;

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

  // Nhận diện khi người chơi nhấn nút đổi camera (Camera button trên HUD hoặc phím V/Select):
  if (pg_test_call(instance, 0x580424a23fb0fc82ull, 2, (uint64_t[]){0, 0})) { // IS_CONTROL_JUST_PRESSED(0, 0: NEXT_CAMERA)
    int cur = atomic_load(&gPreferredViewMode);
    atomic_store(&gPreferredViewMode, (cur == 4) ? 2 : 4);
    lastAppliedMode = -1; // Áp dụng ngay chế độ mới
  }

  const int preferredMode = atomic_load(&gPreferredViewMode);
  const bool vehTransition = (vehicle != lastVehicle);
  lastVehicle = vehicle;

  if (preferredMode == 4) {
    // ==============================================================
    // CHẾ ĐỘ GÓC NHÌN THỨ NHẤT RỘNG (FIRST PERSON COCKPIT LIKE GTA 5 PC)
    // ==============================================================
    if (vehTransition || lastAppliedMode != 4) {
      if (vehicle) {
        // Khi lên bất kỳ loại xe nào (ô tô, xe máy, xe đạp, thuyền, máy bay, trực thăng):
        // Giữ nguyên 100% góc nhìn thứ nhất buồng lái, không bao giờ bị chuyển sang góc nhìn khác hay chớp giật!
        for (int c = 1; c <= 7; c++) {
          pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){(uint32_t)c, 4}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(c, 4)
        }
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){4}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(4)
      } else {
        // Khi đi bộ: giữ nguyên góc nhìn thứ nhất
        pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){0, 4}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(0: On foot, 4)
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){4}); // SET_FOLLOW_PED_CAM_VIEW_MODE(4)
      }
      lastAppliedMode = 4;
    }

    // Luôn ngăn chặn camera nắp ca-pô (bonnet/hood cam): ép 100% hiển thị buồng lái và 2 tay lái
    pg_test_call(instance, 0x7b8a361c1813fbefull, 1, (uint64_t[]){1}); // IGNORE_MENU_PREFERENCE_FOR_BONNET_CAMERA_THIS_UPDATE(true)
    pg_test_call(instance, 0xadff1b2a555f5fbaull, 1, (uint64_t[]){1}); // DISABLE_CINEMATIC_BONNET_CAMERA_THIS_UPDATE(true)

    // Cự ly cắt gần (near clip): 0.18f hiển thị trọn vẹn cả cánh tay và vai (không bị cắt cụt như khi để 0.28f)
    pg_test_call(instance, 0x0af7b437918103b3ull, 1, (uint64_t[]){pg_f32_arg(0.18f)}); // SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE(0.18f)

    // Tự động gỡ bỏ mũ bảo hiểm xe máy/đua xe để tầm nhìn luôn thông thoáng
    pg_test_call(instance, 0xa7b2458d0ad6ded8ull, 2, (uint64_t[]){ped, 1}); // REMOVE_PED_HELMET(ped, true)
    pg_test_call(instance, 0x560a43136eb58105ull, 2, (uint64_t[]){ped, 0}); // SET_PED_HELMET(ped, false)
  } else if (enabled) {
    // ==============================================================
    // CHẾ ĐỘ GÓC NHÌN THỨ BA RỘNG (THIRD PERSON FAR - MODE 2)
    // ==============================================================
    if (vehTransition || lastAppliedMode != 2) {
      if (vehicle) {
        for (int c = 1; c <= 7; c++) {
          pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){(uint32_t)c, 2});
        }
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(2: Far)
      } else {
        pg_test_call(instance, 0x2a2173e46daecd12ull, 2, (uint64_t[]){0, 2}); // SET_CAM_VIEW_MODE_FOR_CONTEXT(0: On foot, 2)
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2}); // SET_FOLLOW_PED_CAM_VIEW_MODE(2: Far)
      }
      lastAppliedMode = 2;
    }
  }

  pg_asi_leave_script(instance, previous);
}
