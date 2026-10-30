import UIKit

/// The graphics part of the Project RELOAD mod (gta5-mods.com, ProjectRELOAD v1.0): timecycles (sky, light,
/// colour, fog, water), weather and snow effects, clouds, visual settings and the weather/sky textures.
/// The package is bundled (Resources/ProjectRELOAD.zip, files at their game paths) and is installed into
/// Mods/Project RELOAD on first use; turning it off renames the folder to ".off", like Settings › Mods does.
/// ENB and ReShade (bloom, DOF, SMAA) are Windows-only programs and are not part of it.
enum BundledReload {
    static let name = "Project RELOAD"
    private static let resource = "ProjectRELOAD"
    private static var current: ModInstaller.Installed? { ModInstaller.installed().first { $0.name == name } }
    static var enabled: Bool { current?.enabled ?? false }

    static func setEnabled(_ on: Bool) throws {
        if let mod = current { try ModInstaller.setEnabled(mod, on); return }
        guard on else { return }
        guard let zip = Bundle.main.url(forResource: resource, withExtension: "zip") else { throw ModError("Thiếu gói Project RELOAD tích hợp.") }
        let fm = FileManager.default
        let stage = try ModInstaller.unpack(zip)
        defer { try? fm.removeItem(at: stage) }
        var plan = ModInstaller.Plan(name: name)
        plan.kinds.insert("replace")
        let base = stage.standardizedFileURL.path
        guard let walk = fm.enumerator(at: stage, includingPropertiesForKeys: [.isRegularFileKey]) else { throw ModError("Không đọc được gói Project RELOAD.") }
        for case let url as URL in walk {
            guard (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else { continue }
            let relative = String(url.standardizedFileURL.path.dropFirst(base.count + 1))
            plan.items.append(ModInstaller.Item(source: url, target: relative, note: "Project RELOAD"))
        }
        try fm.createDirectory(at: ModInstaller.modsFolder, withIntermediateDirectories: true)
        try ModInstaller.install(plan)
    }
}

/// Settings › Project RELOAD: one switch. The change is read the next time the game starts.
final class BundledReloadViewController: UITableViewController {
    init() { super.init(style: .insetGrouped); title = "Đồ hoạ Project RELOAD" }
    required init?(coder: NSCoder) { fatalError() }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int { 1 }

    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        cell.textLabel?.text = "Bật đồ hoạ Project RELOAD"
        let toggle = UISwitch()
        toggle.isOn = BundledReload.enabled
        toggle.addAction(UIAction { [weak self] action in
            guard let self, let toggle = action.sender as? UISwitch else { return }
            let on = toggle.isOn
            toggle.isEnabled = false
            DispatchQueue.global(qos: .userInitiated).async {
                let result = Result { try BundledReload.setEnabled(on) }
                DispatchQueue.main.async {
                    toggle.isEnabled = true
                    if case .failure(let error) = result {
                        toggle.setOn(!on, animated: true)
                        let alert = UIAlertController(title: "Không bật được Project RELOAD", message: error.localizedDescription, preferredStyle: .alert)
                        alert.addAction(UIAlertAction(title: "OK", style: .default))
                        self.present(alert, animated: true)
                    }
                    self.tableView.reloadData()
                }
            }
        }, for: .valueChanged)
        cell.accessoryView = toggle
        cell.selectionStyle = .none
        return cell
    }

    override func tableView(_ tableView: UITableView, titleForFooterInSection section: Int) -> String? {
        "Gói đồ hoạ lấy từ mod Project RELOAD v1.0 (gta5-mods.com): bầu trời, mây, sương mù, nước, ánh sáng và màu sắc theo giờ, thời tiết, mưa, tuyết và texture trời. Bật tại đây trước khi vào game; đồ hoạ áp dụng khi game khởi động, tắt công tắc để về đồ hoạ gốc. ENB và ReShade (bloom, DOF, SMAA) trong mod chỉ chạy trên PC Windows nên không có trên iPhone. Mod cũng hiện trong Cài đặt › Mods để bật/tắt hoặc gỡ."
    }
}
