import Foundation
/// Dynamic Vehicle First Person (camera ngôi thứ nhất khi lái xe), tích hợp sẵn như Menyoo; menu mở bằng phím F7.
/// Bật sẵn theo mặc định để người chơi luôn có menu F7.
enum BundledDVFP {
 static let name="_newgen_dvfp"
 static var enabled:Bool {
  if let obj = UserDefaults.standard.object(forKey:"NewGenDVFPEnabled") as? Bool {
   return obj
  }
  return true
 }

 static let styledMenuIni = """
[MENU]
MenuKey = F7
MenuUp = UP
MenuDown = DOWN
MenuLeft = LEFT
MenuRight = RIGHT
MenuSelect = RETURN
MenuCancel = BACKSPACE
MenuX = 0.150000
MenuY = 0.080000
ControllerButton1 = -1
ControllerButton2 = -1
CheatString = dfpv

[Title Text]
Red = 255
Green = 255
Blue = 255
Alpha = 255
Font = 1

[Title Rect]
Red = 42
Green = 166
Blue = 185
Alpha = 250

[Scroller]
Red = 42
Green = 166
Blue = 185
Alpha = 220

[Options Text]
Red = 255
Green = 255
Blue = 255
Alpha = 255
Font = 0

[Options Rect]
Red = 0
Green = 0
Blue = 0
Alpha = 180
"""

 static let styledGeneralIni = """
[Main]
Enable = true


[Controls.Gamepad]
LookMode = 0
SensitivityX = 1.0
SensitivityY = 0.5
Responsiveness = 35.0
CenterTimeout = 800


[Controls.Mouse]
SensitivityX = 1.0
SensitivityY = 1.0
Responsiveness = 35.0
CenterTimeout = 800


[Controls.Wheel]
Responsiveness = 35.0


[Controls.Limits]
MinPitch = -45.0
MaxPitch = 60.0
RearAngleBlocked = 135.0
RearAngleFree = 160.0
RearAngleBack = 170.0


[Controls.Actions.Keyboard]
NextCameraKey1 = N
NextCameraKey2 = None
PauseRecenterKey1 = B
PauseRecenterKey2 = None


[Controls.Actions.Gamepad]
NextCameraButton1 = -1
NextCameraButton2 = -1
PauseRecenterButton1 = -1
PauseRecenterButton2 = -1
"""

 static func prepare() throws {
  let fm=FileManager.default,root=ModInstaller.modsFolder
  try fm.createDirectory(at:root,withIntermediateDirectories:true)
  let on=root.appendingPathComponent(name),off=root.appendingPathComponent(name+".off")
  if fm.fileExists(atPath:off.path) {
   if !fm.fileExists(atPath:on.path) {
    try? fm.moveItem(at:off,to:on)
   } else {
    try? fm.removeItem(at:off)
   }
  }
  if !fm.fileExists(atPath:on.path) {
   guard let zip=Bundle.main.url(forResource:"BundledDVFP",withExtension:"zip") else{throw ModError("Thiếu gói Dynamic Vehicle First Person tích hợp.")}
   let stage=try ModInstaller.unpack(zip);defer{try? fm.removeItem(at:stage)}
   try "1".write(to:stage.appendingPathComponent(".newgen-dvfp"),atomically:true,encoding:.utf8)
   try fm.moveItem(at:stage,to:on)
  }
  // Tự động đồng bộ vị trí và màu sắc chuẩn của Menu F7 giống như Menu F8 Menyoo
  let cfgDir = on.appendingPathComponent("DynamicVehicleFirstPerson")
  try? fm.createDirectory(at:cfgDir, withIntermediateDirectories:true)
  try? styledMenuIni.write(to:cfgDir.appendingPathComponent("settings_menu.ini"), atomically:true, encoding:.utf8)
  try? styledGeneralIni.write(to:cfgDir.appendingPathComponent("settings_general.ini"), atomically:true, encoding:.utf8)
 }
}
