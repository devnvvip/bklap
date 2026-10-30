// Wide field of view (Settings › MSR). Included by EngineHost.c after NativeTurbo.inc (it needs pg_test_call,
// pg_wasm_text and the script helpers) and run by pg_asi_script_tick on the game thread, never from UIKit.
//
// A script camera copies the gameplay camera every frame (position, rotation) and takes its field of view
// multiplied by the chosen factor, so aiming, scopes and the vehicle cameras keep working: only the picture is
// wider. The factor is separate on foot and in a vehicle and moves smoothly when the player gets in or out.
// The camera is dropped (the game's own camera is back) in cutscenes, player switches, the pause menu, loading,
// or when another script takes the camera over; it then waits a few seconds before it tries again.
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
  const bool enabled = atomic_load(&wfEnabled);
  const bool blocked = PGPauseMenuActive() || PGGameStatus() != 0 || PGLoadSceneActive();
  if (!enabled || blocked) {
    if (active) { u64 previous = pg_asi_enter_script(instance); pg_wf_destroy(instance, &cam, &active); pg_asi_leave_script(instance, previous); }
    factor = 1.0f;
    return;
  }
  const double now = timeMS(CLOCK_MONOTONIC);
  if (now < pauseUntil) return;
  u64 previous = pg_asi_enter_script(instance);
  const u32 ped = PG_FN_PLAYER_PED_ID(instance), vehicle = ped ? PG_FN_PED_VEHICLE(instance, ped, 0) : 0;
  const float target = vehicle ? atomic_load(&wfVehicle) : atomic_load(&wfFoot);
  const bool scripted = !ped || (pg_test_call(instance, 0xd3c2e180a40f031eull, 0, NULL) & 1)             // IS_CUTSCENE_PLAYING
                        || (pg_test_call(instance, 0xd9d2cfff49fab35full, 0, NULL) & 1);                  // IS_PLAYER_SWITCH_IN_PROGRESS
  if (scripted) {
    pg_wf_destroy(instance, &cam, &active); factor = 1.0f; pauseUntil = now + 500;
    pg_asi_leave_script(instance, previous); return;
  }
  // Another script took the camera (a mission, a cinematic): leave it alone for a while.
  if (active && now - activeSince > 300 && !(pg_test_call(instance, 0x02ec0af5c5a49b7aull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {   // IS_CAM_RENDERING
    pg_wf_destroy(instance, &cam, &active); factor = 1.0f; pauseUntil = now + 4000;
    pg_asi_leave_script(instance, previous); return;
  }
  factor += (target - factor) * 0.2f;
  if (fabsf(target - 1.0f) < 0.005f && fabsf(factor - 1.0f) < 0.005f) {      // nothing to widen: the game's own camera
    if (active) { pg_wf_destroy(instance, &cam, &active); pauseUntil = now + 200; }
    factor = 1.0f; pg_asi_leave_script(instance, previous); return;
  }
  if (!active && !(pg_test_call(instance, 0x39b5d1b10383f0c8ull, 0, NULL) & 1)) {   // IS_GAMEPLAY_CAM_RENDERING: only take over from the gameplay camera
    pauseUntil = now + 1000; pg_asi_leave_script(instance, previous); return;
  }
  if (!cam || !(pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {
    uint64_t name = pg_wasm_text(&nameSlot, "DEFAULT_SCRIPTED_CAMERA");
    cam = name ? (int)(uint32_t)pg_test_call(instance, 0xc3981dce61d9e13full, 2, (uint64_t[]){name, 1}) : 0;   // CREATE_CAM
    active = false;
    if (cam <= 0 || !(pg_test_call(instance, 0xa7a932170592b50eull, 1, (uint64_t[]){(uint32_t)cam}) & 1)) {
      cam = 0; pauseUntil = now + 3000; pg_asi_leave_script(instance, previous); return;
    }
  }
  float position[3], rotation[3], fov;
  const uint64_t fovBits = pg_test_call(instance, 0x65019750a0324133ull, 0, NULL);                         // GET_GAMEPLAY_CAM_FOV
  memcpy(&fov, &fovBits, 4);
  if (!pg_wf_vector(instance, 0x14d6f5678d8f1b37ull, 0, NULL, position)                                    // GET_GAMEPLAY_CAM_COORD
      || !pg_wf_vector(instance, 0x837765a25378f0bbull, 1, (uint64_t[]){2}, rotation)                      // GET_GAMEPLAY_CAM_ROT(2)
      || !(fov > 5.0f && fov < 130.0f)) {
    pg_asi_leave_script(instance, previous); return;
  }
  const float widened = fminf(fov * factor, 110.0f);
  pg_test_call(instance, 0x4d41783fb745e42eull, 4, (uint64_t[]){(uint32_t)cam, pg_f32_arg(position[0]), pg_f32_arg(position[1]), pg_f32_arg(position[2])});                 // SET_CAM_COORD
  pg_test_call(instance, 0x85973643155d0b07ull, 5, (uint64_t[]){(uint32_t)cam, pg_f32_arg(rotation[0]), pg_f32_arg(rotation[1]), pg_f32_arg(rotation[2]), 2});               // SET_CAM_ROT
  pg_test_call(instance, 0xb13c14f66a00d047ull, 2, (uint64_t[]){(uint32_t)cam, pg_f32_arg(widened)});                                                                      // SET_CAM_FOV
  if (!active) {
    pg_test_call(instance, 0x026fb97d0a425f84ull, 2, (uint64_t[]){(uint32_t)cam, 1});                    // SET_CAM_ACTIVE(cam, true)
    pg_test_call(instance, 0x07e5b515db0636fcull, 5, (uint64_t[]){1, 0, 0, 1, 0});                       // RENDER_SCRIPT_CAMS(true, false, 0, true, false)
    active = true; activeSince = now;
    pg_log("Wide FOV: script camera on.");
  }
  pg_asi_leave_script(instance, previous);
}
