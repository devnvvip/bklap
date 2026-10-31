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
 }
}
