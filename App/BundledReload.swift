import UIKit

/// The graphics part of the Project RELOAD mod (gta5-mods.com, ProjectRELOAD v1.0): timecycles (sky, light,
/// colour, fog, water), weather and snow effects, clouds, visual settings and the weather/sky textures.
/// The package is bundled (Resources/ProjectRELOAD.zip, files at their game paths) and is installed into
/// Mods/Project RELOAD on first use; turning it off renames the folder to ".off", like Settings › Mods does.
/// ENB and ReShade (bloom, DOF, SMAA) are Windows-only programs and are not part of it: MSR (below) gives the
/// picture the same finishing touch natively on Metal.
enum BundledReload {
    static let name = "Project RELOAD"
    private static let resource = "ProjectRELOAD"
    private static let autoKey = "msrReloadInstalledOnce"
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

    /// MSR brings the package with it: the first time the game starts with MSR on and the package was never
    /// installed, it is installed and turned on. After that the player's own switch decides (an installed
    /// package that was turned off stays off).
    static func enableOnce() {
        let defaults = UserDefaults.standard
        guard !defaults.bool(forKey: autoKey) else { return }
        defaults.set(true, forKey: autoKey)
        if current == nil { try? setEnabled(true) }
    }
}

/// Settings › MSR · Đồ hoạ Project RELOAD: MSR (Mugoat Super Resolution), the wide field of view, the frame lock
/// and the Project RELOAD package, on one page of glass cards. MSR, sharpness, glow, look, field of view and the
/// frame lock change while the game runs; the render height and the RELOAD package are read when the game starts.
final class BundledReloadViewController: UIViewController {
    private let engineRunning: Bool
    private let settings = GameSettings.shared
    private let stack = UIStackView()

    init(engineRunning: Bool = false) {
        self.engineRunning = engineRunning
        super.init(nibName: nil, bundle: nil)
        title = "MSR · Đồ hoạ"
    }
    required init?(coder: NSCoder) { fatalError() }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = Theme.night
        let backdrop = Theme.dimmedBackdrop(image: "SettingsBackground")
        backdrop.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(backdrop)
        let scroll = UIScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.alwaysBounceVertical = true
        scroll.showsVerticalScrollIndicator = false
        view.addSubview(scroll)
        stack.axis = .vertical
        stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false
        scroll.addSubview(stack)
        NSLayoutConstraint.activate([
            backdrop.leadingAnchor.constraint(equalTo: view.leadingAnchor), backdrop.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            backdrop.topAnchor.constraint(equalTo: view.topAnchor), backdrop.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor), scroll.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor),
            scroll.topAnchor.constraint(equalTo: view.topAnchor), scroll.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            stack.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 12),
            stack.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor, constant: -32),
            stack.leadingAnchor.constraint(equalTo: scroll.contentLayoutGuide.leadingAnchor, constant: 16),
            stack.trailingAnchor.constraint(equalTo: scroll.contentLayoutGuide.trailingAnchor, constant: -16),
            stack.widthAnchor.constraint(equalTo: scroll.frameLayoutGuide.widthAnchor, constant: -32),
        ])
        build()
    }

    // MARK: Page

    private func build() {
        stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        stack.addArrangedSubview(heroCard())
        stack.addArrangedSubview(msrCard())
        stack.addArrangedSubview(fovCard())
        stack.addArrangedSubview(frameCard())
        stack.addArrangedSubview(reloadCard())
    }

    private func heroCard() -> UIView {
        let panel = Theme.panel(cornerRadius: 24)
        let glow = GradientView(colors: [Theme.accent.withAlphaComponent(0.34), Theme.accentDeep.withAlphaComponent(0.10), .clear],
                                locations: [0, 0.55, 1], horizontal: true)
        glow.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView.addSubview(glow)
        let title = label("MSR", font: Theme.display(34, .heavy), color: Theme.text)
        let subtitle = label("Mugoat Super Resolution", font: Theme.display(15, .semibold), color: Theme.accent)
        let detail = label("Game vẽ ở độ phân giải thấp để nhẹ máy, MetalFX kéo lên đúng độ phân giải màn hình, rồi một lớp hoàn thiện làm nét, thêm bloom và chỉnh màu như ReShade/ENB trên PC.",
                           font: Theme.body(14), color: Theme.secondaryText)
        let chip = label(settings.tier.displaySummary, font: Theme.body(12, .medium), color: Theme.tertiaryText)
        let column = UIStackView(arrangedSubviews: [title, subtitle, detail, chip])
        column.axis = .vertical
        column.spacing = 4
        column.setCustomSpacing(10, after: subtitle)
        column.setCustomSpacing(10, after: detail)
        column.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView.addSubview(column)
        pin(column, in: panel.contentView, inset: 18)
        NSLayoutConstraint.activate([
            glow.leadingAnchor.constraint(equalTo: panel.contentView.leadingAnchor), glow.trailingAnchor.constraint(equalTo: panel.contentView.trailingAnchor),
            glow.topAnchor.constraint(equalTo: panel.contentView.topAnchor), glow.bottomAnchor.constraint(equalTo: panel.contentView.bottomAnchor),
        ])
        return panel
    }

    private func msrCard() -> UIView {
        var rows: [UIView] = [
            switchRow("Bật MSR", detail: "MetalFX lên màn hình + lớp làm nét, bloom, màu. Đổi được ngay khi đang chơi.", isOn: settings.msrEnabled) { [weak self] on in
                self?.settings.msrEnabled = on
            },
            segmentRow("Độ phân giải game vẽ", items: ["540p", "720p", "900p"], selected: [540, 720, 900].firstIndex(of: settings.msrInternalHeight) ?? 1,
                       note: engineRunning ? "Áp dụng khi khởi động lại game. 720p là mức tối ưu cho chip A18." : "720p là mức tối ưu cho chip A18: nhẹ GPU, ít nóng, MSR giữ hình nét.") { [weak self] index in
                self?.settings.msrInternalHeight = [540, 720, 900][index]
            },
            sliderRow("Độ nét", value: settings.msrSharpness, range: 0...1, format: { "\(Int($0 * 100))%" }) { [weak self] value in self?.settings.msrSharpness = value },
            sliderRow("Bloom (ánh sáng toả)", value: settings.msrBloom, range: 0...1, format: { "\(Int($0 * 100))%" }) { [weak self] value in self?.settings.msrBloom = value },
            segmentRow("Màu sắc", items: ["Gốc", "Tự nhiên", "Điện ảnh", "Rực rỡ"], selected: settings.msrLook, note: nil) { [weak self] index in
                self?.settings.msrLook = index
            },
        ]
        rows.append(noteLabel("Giảm nóng: game không còn quay vòng chờ khung hình (trước đây đốt một nhân CPU liên tục), phản xạ Ultra/Max hạ một bậc khi MSR bật; chỉnh riêng trong Đồ hoạ vẫn được ưu tiên."))
        return card(title: "Độ nét và màu sắc", symbol: "wand.and.stars", rows: rows)
    }

    private func fovCard() -> UIView {
        let rows: [UIView] = [
            switchRow("Góc nhìn rộng", detail: "Mở rộng tầm nhìn khi đi bộ và khi lái xe. Tự về góc gốc trong cutscene và nhiệm vụ có camera riêng.", isOn: settings.wideFOV) { [weak self] on in
                self?.settings.wideFOV = on
            },
            sliderRow("Khi đi bộ", value: settings.fovOnFoot, range: 1...1.5, format: { "+\(Int(($0 - 1) * 100))%" }) { [weak self] value in self?.settings.fovOnFoot = value },
            sliderRow("Khi lái xe", value: settings.fovInVehicle, range: 1...1.5, format: { "+\(Int(($0 - 1) * 100))%" }) { [weak self] value in self?.settings.fovInVehicle = value },
        ]
        return card(title: "Góc nhìn", symbol: "eye", rows: rows)
    }

    private func frameCard() -> UIView {
        let rows: [UIView] = [
            segmentRow("Khoá cứng FPS", items: ["80 FPS (+20)", "120 FPS"], selected: settings.frameRateCap == 120 ? 1 : 0,
                       note: "Khóa siêu cứng +20 FPS (80 FPS) tiết kiệm CPU chống nóng máy trên chip A18 iPhone 16 Pro, hoặc 120 FPS ProMotion.") { [weak self] index in
                self?.settings.frameRateCap = index == 1 ? 120 : 60
                NotificationCenter.default.post(name: ProcessInfo.thermalStateDidChangeNotification, object: nil)
            },
        ]
        return card(title: "Khung hình", symbol: "speedometer", rows: rows)
    }

    private func reloadCard() -> UIView {
        let rows: [UIView] = [
            switchRow("Bật đồ hoạ Project RELOAD", detail: engineRunning ? "Áp dụng khi khởi động lại game." : "Bầu trời, mây, sương mù, nước, ánh sáng và màu theo giờ, thời tiết, mưa, tuyết và texture trời. Áp dụng khi vào game.",
                      isOn: BundledReload.enabled) { [weak self] on in self?.setReload(on) },
            noteLabel("ENB và ReShade của mod chỉ chạy trên PC Windows; trên iPhone phần bloom, làm nét và chỉnh màu do MSR thay thế. Mod cũng hiện trong Cài đặt › Mods để bật/tắt hoặc gỡ."),
        ]
        return card(title: "Project RELOAD", symbol: "sparkles", rows: rows)
    }

    private func setReload(_ on: Bool) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let result = Result { try BundledReload.setEnabled(on) }
            DispatchQueue.main.async {
                guard let self else { return }
                if case .failure(let error) = result {
                    let alert = UIAlertController(title: "Không bật được Project RELOAD", message: error.localizedDescription, preferredStyle: .alert)
                    alert.addAction(UIAlertAction(title: "OK", style: .default))
                    self.present(alert, animated: true)
                }
                self.build()
            }
        }
    }

    // MARK: Pieces

    private func label(_ text: String, font: UIFont, color: UIColor) -> UILabel {
        let label = UILabel()
        label.text = text
        label.font = font
        label.textColor = color
        label.numberOfLines = 0
        return label
    }
    private func noteLabel(_ text: String) -> UILabel { label(text, font: Theme.body(12.5), color: Theme.tertiaryText) }

    private func pin(_ inner: UIView, in outer: UIView, inset: CGFloat) {
        NSLayoutConstraint.activate([
            inner.topAnchor.constraint(equalTo: outer.topAnchor, constant: inset), inner.bottomAnchor.constraint(equalTo: outer.bottomAnchor, constant: -inset),
            inner.leadingAnchor.constraint(equalTo: outer.leadingAnchor, constant: inset), inner.trailingAnchor.constraint(equalTo: outer.trailingAnchor, constant: -inset),
        ])
    }

    private func card(title: String, symbol: String, rows: [UIView]) -> UIView {
        let panel = Theme.panel(cornerRadius: 22)
        let icon = UIImageView(image: UIImage(systemName: symbol))
        icon.tintColor = Theme.accent
        icon.setContentHuggingPriority(.required, for: .horizontal)
        let header = UIStackView(arrangedSubviews: [icon, label(title, font: Theme.display(18, .bold), color: Theme.text)])
        header.spacing = 10
        header.alignment = .center
        let column = UIStackView(arrangedSubviews: [header] + rows)
        column.axis = .vertical
        column.spacing = 14
        column.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView.addSubview(column)
        pin(column, in: panel.contentView, inset: 16)
        return panel
    }

    private func switchRow(_ title: String, detail: String, isOn: Bool, changed: @escaping (Bool) -> Void) -> UIView {
        let texts = UIStackView(arrangedSubviews: [label(title, font: Theme.body(16, .semibold), color: Theme.text), noteLabel(detail)])
        texts.axis = .vertical
        texts.spacing = 3
        let toggle = UISwitch()
        toggle.isOn = isOn
        toggle.onTintColor = Theme.accent
        toggle.setContentHuggingPriority(.required, for: .horizontal)
        toggle.addAction(UIAction { action in
            if let toggle = action.sender as? UISwitch { changed(toggle.isOn) }
        }, for: .valueChanged)
        let row = UIStackView(arrangedSubviews: [texts, toggle])
        row.spacing = 12
        row.alignment = .center
        return row
    }

    private func sliderRow(_ title: String, value: Double, range: ClosedRange<Double>, format: @escaping (Double) -> String, changed: @escaping (Double) -> Void) -> UIView {
        let name = label(title, font: Theme.body(15, .medium), color: Theme.text)
        let shown = label(format(value), font: Theme.body(15, .semibold), color: Theme.accent)
        shown.textAlignment = .right
        shown.setContentHuggingPriority(.required, for: .horizontal)
        let top = UIStackView(arrangedSubviews: [name, shown])
        let slider = UISlider()
        slider.minimumValue = Float(range.lowerBound)
        slider.maximumValue = Float(range.upperBound)
        slider.value = Float(value)
        slider.minimumTrackTintColor = Theme.accent
        slider.addAction(UIAction { action in
            guard let slider = action.sender as? UISlider else { return }
            shown.text = format(Double(slider.value))
            changed(Double(slider.value))
        }, for: .valueChanged)
        let column = UIStackView(arrangedSubviews: [top, slider])
        column.axis = .vertical
        column.spacing = 6
        return column
    }

    private func segmentRow(_ title: String, items: [String], selected: Int, note: String?, changed: @escaping (Int) -> Void) -> UIView {
        let control = UISegmentedControl(items: items)
        control.selectedSegmentIndex = min(max(selected, 0), items.count - 1)
        control.selectedSegmentTintColor = Theme.accent
        control.setTitleTextAttributes([.foregroundColor: UIColor(red: 0.16, green: 0.07, blue: 0.02, alpha: 1), .font: Theme.body(14, .bold)], for: .selected)
        control.setTitleTextAttributes([.foregroundColor: Theme.text, .font: Theme.body(14, .medium)], for: .normal)
        control.addAction(UIAction { action in
            if let control = action.sender as? UISegmentedControl { changed(control.selectedSegmentIndex) }
        }, for: .valueChanged)
        var views: [UIView] = [label(title, font: Theme.body(15, .medium), color: Theme.text), control]
        if let note { views.append(noteLabel(note)) }
        let column = UIStackView(arrangedSubviews: views)
        column.axis = .vertical
        column.spacing = 6
        return column
    }
}
