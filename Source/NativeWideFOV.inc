// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc
//
// Sử dụng 100% camera native của Rockstar Games (camGameplayDirector) và các native view mode:
// - Góc nhìn thứ ba: Mode 2 (Far) cho góc nhìn rộng bao quát, chuyển động mượt mà tuyệt đối không rung giật.
// - Góc nhìn thứ nhất (First Person Cockpit) chuẩn như GTA V PC:
//   * Tự do chuyển đổi bằng nút Camera trên màn hình, không bao giờ ép đảo camera mỗi frame.
//   * Tự động đồng bộ khi lên xe hoặc xuống xe: đang góc thứ nhất khi đi bộ -> lên xe vẫn ở góc thứ nhất;
//     và ngược lại khi xuống xe.
//   * Thấy trọn vẹn cả cánh tay và vai cùng vô lăng/ghi đông chân thật trên tất cả mọi loại xe.
//   * Cự ly cắt gần near-clip 0.18f: thấy trọn vẹn tay và vai, không bị cắt cụt củn lủn, triệt tiêu lọt hình.
//   * Luôn vô hiệu hóa camera nắp ca-pô (bonnet cam), tự động tháo mũ bảo hiểm xe máy.
// - Báo trạng thái lái xe cho Metal renderer để khóa cứng 80 FPS khi đi bộ và 75 FPS khi lái xe.

static _Atomic bool wfEnabled;
static _Atomic float wfFoot = 1.0f, wfVehicle = 1.0f;

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
  static bool initialized = false;

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

  // Lấy chế độ góc nhìn hiện tại
  const int pedMode = (int)pg_test_call(instance, 0x8d4d46230b2c353aull, 0, NULL); // GET_FOLLOW_PED_CAM_VIEW_MODE
  const int vehMode = (int)pg_test_call(instance, 0xa4ff579ac0e3aaaeull, 0, NULL); // GET_FOLLOW_VEHICLE_CAM_VIEW_MODE

  if (!initialized) {
    initialized = true;
    lastVehicle = vehicle;
    if (enabled) {
      if (vehicle) pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2}); // SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(2)
      else pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2});         // SET_FOLLOW_PED_CAM_VIEW_MODE(2)
    }
  } else if (vehicle != lastVehicle) {
    // Chỉ kích hoạt một lần duy nhất tại khoảnh khắc bước lên xe hoặc bước xuống xe:
    // Đồng bộ góc nhìn thứ nhất (Mode 4) mượt mà, không bao giờ bị nhảy sang góc nhìn khác khi lên/xuống xe!
    if (vehicle != 0) {
      // Vừa bước lên xe: Nếu đang ở góc nhìn thứ nhất, giữ nguyên buồng lái thứ nhất cho xe!
      if (pedMode == 4) {
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){4});
      } else if (enabled && pedMode == 2) {
        pg_test_call(instance, 0xac253d7842768f48ull, 1, (uint64_t[]){2});
      }
    } else {
      // Vừa bước xuống xe: Nếu trên xe đang ở góc nhìn thứ nhất, giữ nguyên góc thứ nhất khi đi bộ!
      if (vehMode == 4) {
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){4});
      } else if (enabled && vehMode == 2) {
        pg_test_call(instance, 0x5a4f9edf1673f704ull, 1, (uint64_t[]){2});
      }
    }
    lastVehicle = vehicle;
  }

  const int currentMode = (vehicle != 0) ? vehMode : pedMode;

  if (currentMode == 4) {
    // ==============================================================
    // GÓC NHÌN THỨ NHẤT RỘNG (FIRST PERSON COCKPIT LIKE GTA 5 PC)
    // ==============================================================
    if (vehicle != 0) {
      // Luôn vô hiệu hóa camera nắp ca-pô: thấy trọn vẹn vô lăng và 2 tay lái
      pg_test_call(instance, 0x7b8a361c1813fbefull, 1, (uint64_t[]){1}); // IGNORE_MENU_PREFERENCE_FOR_BONNET_CAMERA_THIS_UPDATE(true)
      pg_test_call(instance, 0xadff1b2a555f5fbaull, 1, (uint64_t[]){1}); // DISABLE_CINEMATIC_BONNET_CAMERA_THIS_UPDATE(true)
    }

    // Cự ly cắt gần near clip 0.18f: thấy trọn vẹn cả cánh tay và vai, triệt tiêu lọt ngực
    pg_test_call(instance, 0x0af7b437918103b3ull, 1, (uint64_t[]){pg_f32_arg(0.18f)}); // SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE(0.18f)

    // Tự động gỡ bỏ mũ bảo hiểm khi lái xe máy để tầm nhìn luôn thông thoáng
    pg_test_call(instance, 0xa7b2458d0ad6ded8ull, 2, (uint64_t[]){ped, 1}); // REMOVE_PED_HELMET(ped, true)
    pg_test_call(instance, 0x560a43136eb58105ull, 2, (uint64_t[]){ped, 0}); // SET_PED_HELMET(ped, false)
  }

  pg_asi_leave_script(instance, previous);
}
