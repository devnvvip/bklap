import Foundation
/// Dynamic Vehicle First Person (camera ngôi thứ nhất khi lái xe), tích hợp sẵn như Menyoo; menu mở bằng phím F7.
/// Tắt mặc định; bật ở Settings › Plugin First Person trước khi chơi.
enum BundledDVFP {
 static let name="_newgen_dvfp"
 static var enabled:Bool {UserDefaults.standard.bool(forKey:"NewGenDVFPEnabled")}
 static func prepare() throws {
  let fm=FileManager.default,root=ModInstaller.modsFolder
  try fm.createDirectory(at:root,withIntermediateDirectories:true)
  let on=root.appendingPathComponent(name),off=root.appendingPathComponent(name+".off")
  var target=fm.fileExists(atPath:on.path) ? on:off
  if !fm.fileExists(atPath:target.path) {
   guard let zip=Bundle.main.url(forResource:"BundledDVFP",withExtension:"zip") else{throw ModError("Thiếu gói Dynamic Vehicle First Person tích hợp.")}
   let stage=try ModInstaller.unpack(zip);defer{try? fm.removeItem(at:stage)}
   try "1".write(to:stage.appendingPathComponent(".newgen-dvfp"),atomically:true,encoding:.utf8)
   try fm.moveItem(at:stage,to:off);target=off
  }
  guard fm.fileExists(atPath:target.appendingPathComponent(".newgen-dvfp").path) else{throw ModError("Thư mục Dynamic Vehicle First Person không thuộc gói tích hợp; giữ nguyên.")}
  let desired=enabled ? on:off
  if target != desired {
   guard !fm.fileExists(atPath:desired.path) else{throw ModError("Có hai bản Dynamic Vehicle First Person tích hợp; hãy giữ lại một bản trong Mods.")}
   try fm.moveItem(at:target,to:desired)
  }
 }
}
