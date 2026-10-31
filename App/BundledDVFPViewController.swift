import UIKit
final class BundledDVFPViewController:UITableViewController {
 init(){super.init(style:.insetGrouped);title="Plugin First Person"}
 required init?(coder:NSCoder){fatalError()}
 override func tableView(_ tableView:UITableView,numberOfRowsInSection section:Int)->Int{1}
 override func tableView(_ tableView:UITableView,cellForRowAt indexPath:IndexPath)->UITableViewCell{
  let cell=UITableViewCell(style:.default,reuseIdentifier:nil);cell.textLabel?.text="Bật Dynamic Vehicle First Person tích hợp"
  let toggle=UISwitch();toggle.isOn=BundledDVFP.enabled
  toggle.addAction(UIAction{action in guard let toggle=action.sender as? UISwitch else{return};UserDefaults.standard.set(toggle.isOn,forKey:"NewGenDVFPEnabled")},for:.valueChanged)
  cell.accessoryView=toggle;cell.selectionStyle = .none;return cell
 }
 override func tableView(_ tableView:UITableView,titleForFooterInSection section:Int)->String?{
  "Đã tích hợp mod camera ngôi thứ nhất khi lái xe, không cần tải lại mod. Bật tại đây trước khi chơi. Trong game: Plugin → F7 để mở menu mod (Menyoo vẫn là F8), dùng mũi tên và OK trên màn hình để chọn. Mod mặc định tắt để giữ hành vi bản nền."
 }
}
