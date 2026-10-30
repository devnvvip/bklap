import UIKit

/// The gold of the compact start screen (buttons, selection, divider).
private let homeGold = UIColor(red: 0.94, green: 0.74, blue: 0.30, alpha: 1)
private let homeGoldDeep = UIColor(red: 0.80, green: 0.60, blue: 0.18, alpha: 1)

/// The start screen: background art, the game's title, where the game files are, the mode choice,
/// Play, and the way into Settings and the controls layout. The view controller keeps the logic;
/// this view only lays things out and reports taps.
final class HomeView: UIView {
    enum FilesState { case ready, attention, problem, busy }

    let playButton = Theme.primaryButton(AppLanguage.choose("Play"), symbol: "play.fill")
    let settingsButton = Theme.glassButton(AppLanguage.choose("Settings"), symbol: "gearshape.fill")
    let layoutButton = Theme.glassButton(AppLanguage.choose("Controls layout"), symbol: "hand.tap.fill")
    let telegramButton = Theme.glassButton(AppLanguage.choose("Telegram"), symbol: "paperplane.fill")
    let preloadLabel = UILabel()
    let controllerLabel = UILabel()
    /// Taps on the game files card.
    var onFiles: (() -> Void)?
    /// A mode card was tapped (index into GameMode.allCases).
    var onMode: ((Int) -> Void)?

    private let filesCard = UIControl()
    private let filesIcon = UIImageView()
    private let filesTitle = UILabel()
    private let filesDetail = UILabel()
    private let filesSpinner = UIActivityIndicatorView(style: .medium)
    private var modeCards: [ModeCard] = []
    private let modeDetail = UILabel()
    private let brandTitle = UILabel()
    private let brandSubtitle = UILabel()
    private let mapTitle = UILabel()
    private let divider = UIView()
    /// Online (VPS) is the chosen mode: the subtitle says so.
    private var online = false
    /// Sandbox only: which map (it takes the place of the mode description).
    private let mapRow = UIStackView()
    private var mapChips: [UIButton] = []
    var onMap: ((Int) -> Void)?

    init(modes: [(title: String, symbol: String)], log: UIView) {
        super.init(frame: .zero)
        let backdrop = Theme.backdrop(image: "StartBackground")
        pin(backdrop, to: self)

        let icon = UIImageView(image: UIImage(named: "HomeIcon"))
        icon.layer.cornerRadius = 10
        icon.layer.cornerCurve = .continuous
        icon.clipsToBounds = true
        icon.widthAnchor.constraint(equalToConstant: 44).isActive = true
        icon.heightAnchor.constraint(equalToConstant: 44).isActive = true
        let title = brandTitle
        title.text = "MUGOAT"
        title.font = Theme.display(26)
        title.textColor = Theme.text
        title.adjustsFontSizeToFitWidth = true
        title.minimumScaleFactor = 0.7
        let subtitle = brandSubtitle
        subtitle.text = brandSubtitleText()
        subtitle.font = Theme.body(12, .semibold)
        subtitle.textColor = Theme.secondaryText
        subtitle.numberOfLines = 1
        let titles = UIStackView(arrangedSubviews: [title, subtitle])
        titles.axis = .vertical
        titles.spacing = 1
        let header = UIStackView(arrangedSubviews: [icon, titles])
        header.spacing = 10
        header.alignment = .center

        buildFilesCard()

        divider.backgroundColor = homeGold.withAlphaComponent(0.8)
        divider.heightAnchor.constraint(equalToConstant: 1).isActive = true
        playButton.configuration?.contentInsets = NSDirectionalEdgeInsets(top: 9, leading: 24, bottom: 9, trailing: 24)
        playButton.configuration?.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { attributes in
            var attributes = attributes; attributes.font = Theme.display(17, .heavy); return attributes
        }
        playButton.configurationUpdateHandler = { button in
            button.configuration?.background.backgroundColor = button.isEnabled ? (button.isHighlighted ? homeGoldDeep : homeGold) : UIColor(white: 1, alpha: 0.16)
            button.configuration?.baseForegroundColor = button.isEnabled ? UIColor(red: 0.16, green: 0.10, blue: 0.02, alpha: 1) : UIColor(white: 1, alpha: 0.6)
        }
        playButton.layer.shadowColor = homeGold.cgColor
        playButton.layer.shadowRadius = 10

        for (index, mode) in modes.enumerated() {
            let card = ModeCard(title: mode.title, symbol: mode.symbol)
            card.addAction(UIAction { [weak self] _ in self?.onMode?(index) }, for: .touchUpInside)
            modeCards.append(card)
        }
        let modes = UIStackView(arrangedSubviews: modeCards)
        modes.spacing = 8
        modes.distribution = .fillEqually
        modeDetail.font = Theme.body(11)
        modeDetail.textColor = Theme.secondaryText
        modeDetail.numberOfLines = 2
        modeDetail.adjustsFontSizeToFitWidth = true
        modeDetail.minimumScaleFactor = 0.8

        telegramButton.addAction(UIAction { _ in UIApplication.shared.open(Theme.telegramURL) }, for: .touchUpInside)
        telegramButton.accessibilityLabel = AppLanguage.choose("Five Star Port on Telegram")
        let secondary = self.secondary
        // Icons only, in every language; the names stay for VoiceOver.
        settingsButton.accessibilityLabel = AppLanguage.choose("Settings")
        layoutButton.accessibilityLabel = AppLanguage.choose("Controls layout")
        for button in [telegramButton, layoutButton, settingsButton] { button.configuration?.title = nil }
        secondary.addArrangedSubviews([telegramButton, layoutButton, settingsButton])
        secondary.axis = .vertical
        secondary.alignment = .center
        secondary.spacing = 8
        for button in [telegramButton, layoutButton, settingsButton] {
            button.configuration?.contentInsets = NSDirectionalEdgeInsets(top: 9, leading: 9, bottom: 9, trailing: 9)
            button.widthAnchor.constraint(equalToConstant: 38).isActive = true
            button.heightAnchor.constraint(equalToConstant: 38).isActive = true
        }
        secondary.translatesAutoresizingMaskIntoConstraints = false
        for label in [preloadLabel, controllerLabel] {
            label.font = .monospacedDigitSystemFont(ofSize: 11, weight: .medium)
            label.textColor = Theme.tertiaryText
            label.numberOfLines = 0
        }
        preloadLabel.isHidden = true

        let mapLabel = mapTitle
        mapLabel.text = AppLanguage.choose("Map")
        mapLabel.font = Theme.body(12, .semibold)
        mapLabel.textColor = Theme.secondaryText
        mapRow.addArrangedSubview(mapLabel)
        for (index, title) in GameSettings.sandboxMapTitles.enumerated() {
            var config = UIButton.Configuration.gray()
            config.title = title
            config.cornerStyle = .capsule
            config.contentInsets = NSDirectionalEdgeInsets(top: 4, leading: 10, bottom: 4, trailing: 10)
            config.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { a in var a = a; a.font = Theme.body(12, .semibold); return a }
            let chip = UIButton(configuration: config)
            chip.addAction(UIAction { [weak self] _ in self?.onMap?(index) }, for: .touchUpInside)
            mapChips.append(chip)
            mapRow.addArrangedSubview(chip)
        }
        mapRow.addArrangedSubview(UIView())
        mapRow.spacing = 6
        mapRow.alignment = .center
        mapRow.isHidden = true
        let column = UIStackView(arrangedSubviews: [header, divider, modes, modeDetail, mapRow, filesCard, playButton, preloadLabel, controllerLabel])
        column.axis = .vertical
        column.spacing = 8
        column.setCustomSpacing(8, after: header)
        column.setCustomSpacing(10, after: divider)
        column.setCustomSpacing(6, after: modes)
        column.setCustomSpacing(8, after: modeDetail)
        column.setCustomSpacing(8, after: mapRow)
        column.setCustomSpacing(10, after: filesCard)
        column.setCustomSpacing(6, after: playButton)
        column.alignment = .fill

        let scroll = self.scroll
        scroll.showsVerticalScrollIndicator = false
        scroll.alwaysBounceVertical = false
        scroll.translatesAutoresizingMaskIntoConstraints = false
        column.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scroll)
        scroll.addSubview(column)
        log.translatesAutoresizingMaskIntoConstraints = false
        addSubview(log)
        addSubview(secondary)
        let guide = safeAreaLayoutGuide
        let width = column.widthAnchor.constraint(equalTo: guide.widthAnchor, multiplier: 0.36)
        width.priority = .defaultHigh
        NSLayoutConstraint.activate([
            scroll.leadingAnchor.constraint(equalTo: guide.leadingAnchor, constant: 24),
            scroll.topAnchor.constraint(equalTo: topAnchor),
            scroll.bottomAnchor.constraint(equalTo: bottomAnchor),
            scroll.widthAnchor.constraint(equalTo: column.widthAnchor),
            column.leadingAnchor.constraint(equalTo: scroll.contentLayoutGuide.leadingAnchor),
            column.trailingAnchor.constraint(equalTo: scroll.contentLayoutGuide.trailingAnchor),
            column.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 12),
            column.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor, constant: -12),
            // Centred vertically when there's room, scrolling when there isn't.
            column.heightAnchor.constraint(greaterThanOrEqualTo: scroll.frameLayoutGuide.heightAnchor, constant: -24),
            width,
            column.widthAnchor.constraint(lessThanOrEqualToConstant: 340),
            column.widthAnchor.constraint(greaterThanOrEqualToConstant: 280),
            secondary.trailingAnchor.constraint(equalTo: guide.trailingAnchor, constant: -20),
            // Long translations (Russian, German) must not run over the title column.
            secondary.leadingAnchor.constraint(greaterThanOrEqualTo: scroll.trailingAnchor, constant: 12),
            secondary.topAnchor.constraint(equalTo: guide.topAnchor, constant: 10),
            log.leadingAnchor.constraint(equalTo: scroll.trailingAnchor, constant: 20),
            log.trailingAnchor.constraint(equalTo: guide.trailingAnchor, constant: -16),
            log.topAnchor.constraint(equalTo: secondary.bottomAnchor, constant: 12),
            log.bottomAnchor.constraint(equalTo: guide.bottomAnchor, constant: -16)
        ])
        column.distribution = .fill
        // Spare height goes above and below the column's content rather than into it.
        let topSpace = UIView(), bottomSpace = UIView()
        column.insertArrangedSubview(topSpace, at: 0)
        column.addArrangedSubview(bottomSpace)
        topSpace.heightAnchor.constraint(equalTo: bottomSpace.heightAnchor).isActive = true
        for view in column.arrangedSubviews where view !== topSpace && view !== bottomSpace {
            view.setContentHuggingPriority(.required, for: .vertical)
            view.setContentCompressionResistancePriority(.required, for: .vertical)
        }
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Update language-specific titles in place without touching downloaded game files or layout.
    private let scroll = UIScrollView()
    private let secondary = UIStackView()
    private func brandSubtitleText() -> String { "V6 · " + (online ? "CHƠI ONLINE" : "CHƠI OFFLINE") }
    func refreshLanguage() {
        brandTitle.text = "MUGOAT"
        brandSubtitle.text = brandSubtitleText()
        mapTitle.text = AppLanguage.choose("Map")
        playButton.configuration?.title = AppLanguage.choose("Play")
        // The top-right buttons are icons in every language (long titles overlapped the column, GTA-47);
        // their names stay for VoiceOver.
        settingsButton.configuration?.title = nil
        layoutButton.configuration?.title = nil
        telegramButton.configuration?.title = nil
        settingsButton.accessibilityLabel = AppLanguage.choose("Settings")
        layoutButton.accessibilityLabel = AppLanguage.choose("Controls layout")
        for (index, card) in modeCards.enumerated() {
            card.setTitle(GameMode.allCases[index].title)
        }
        for (i, chip) in mapChips.enumerated() {
            chip.configuration?.title = GameSettings.sandboxMapTitles[i]
        }
    }

    private func buildFilesCard() {
        let panel = Theme.panel(cornerRadius: 12)
        panel.isUserInteractionEnabled = false
        pin(panel, to: filesCard)
        filesIcon.contentMode = .scaleAspectFit
        filesIcon.preferredSymbolConfiguration = UIImage.SymbolConfiguration(pointSize: 18, weight: .semibold)
        filesIcon.widthAnchor.constraint(equalToConstant: 24).isActive = true
        filesTitle.font = Theme.body(14, .semibold)
        filesTitle.textColor = Theme.text
        filesDetail.font = Theme.body(11)
        filesDetail.textColor = Theme.secondaryText
        filesDetail.numberOfLines = 1
        let texts = UIStackView(arrangedSubviews: [filesTitle, filesDetail])
        texts.axis = .vertical
        texts.spacing = 1
        let chevron = UIImageView(image: UIImage(systemName: "chevron.right", withConfiguration: UIImage.SymbolConfiguration(pointSize: 12, weight: .bold)))
        chevron.tintColor = Theme.tertiaryText
        chevron.setContentHuggingPriority(.required, for: .horizontal)
        filesSpinner.color = Theme.text
        let iconHolder = UIView()
        for view in [filesIcon, filesSpinner] as [UIView] {
            view.translatesAutoresizingMaskIntoConstraints = false
            iconHolder.addSubview(view)
            NSLayoutConstraint.activate([view.centerXAnchor.constraint(equalTo: iconHolder.centerXAnchor), view.centerYAnchor.constraint(equalTo: iconHolder.centerYAnchor)])
        }
        iconHolder.widthAnchor.constraint(equalToConstant: 24).isActive = true
        let row = UIStackView(arrangedSubviews: [iconHolder, texts, chevron])
        row.spacing = 10
        row.alignment = .center
        row.isUserInteractionEnabled = false
        row.translatesAutoresizingMaskIntoConstraints = false
        filesCard.addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: filesCard.leadingAnchor, constant: 12),
            row.trailingAnchor.constraint(equalTo: filesCard.trailingAnchor, constant: -12),
            row.topAnchor.constraint(equalTo: filesCard.topAnchor, constant: 7),
            row.bottomAnchor.constraint(equalTo: filesCard.bottomAnchor, constant: -7)
        ])
        filesCard.accessibilityTraits = .button
        filesCard.isAccessibilityElement = true
        filesCard.addAction(UIAction { [weak self] _ in self?.onFiles?() }, for: .touchUpInside)
        filesCard.addAction(UIAction { [weak self] _ in self?.filesCard.alpha = 0.7 }, for: [.touchDown, .touchDragEnter])
        filesCard.addAction(UIAction { [weak self] _ in self?.filesCard.alpha = 1 }, for: [.touchUpInside, .touchUpOutside, .touchCancel, .touchDragExit])
    }

    /// Shows where the game files are and whether they're ready.
    func setFiles(_ state: FilesState, title: String, detail: String) {
        filesTitle.text = title
        filesDetail.text = detail
        filesDetail.isHidden = detail.isEmpty
        filesSpinner.isHidden = state != .busy
        filesIcon.isHidden = state == .busy
        if state == .busy { filesSpinner.startAnimating() } else { filesSpinner.stopAnimating() }
        let (symbol, color): (String, UIColor) = switch state {
        case .ready: ("checkmark.circle.fill", Theme.ok)
        case .attention: ("folder.badge.plus", Theme.warning)
        case .problem: ("exclamationmark.triangle.fill", Theme.problem)
        case .busy: ("", Theme.text)
        }
        filesIcon.image = symbol.isEmpty ? nil : UIImage(systemName: symbol)
        filesIcon.tintColor = color
        filesCard.accessibilityLabel = AppLanguage.interpolate(AppLanguage.choose("Game files: {arg0}. {arg1}"), [title, detail])
    }

    /// Marks the chosen mode (nil: none yet) and shows its description.
    /// Shows the map choice (Sandbox) instead of the mode description, with `index` selected.
    func setMap(_ index: Int, visible: Bool, enabled: Bool, testMapAvailable: Bool = true) {
        mapRow.isHidden = !visible
        modeDetail.isHidden = visible
        for (i, chip) in mapChips.enumerated() {
            let selected = i == index
            // The GTA VI test map greys out when the game files don't include it.
            chip.isEnabled = enabled && (i != 1 || testMapAvailable)
            chip.configuration?.baseForegroundColor = selected ? homeGold : Theme.text
            chip.configuration?.background.backgroundColor = selected ? homeGold.withAlphaComponent(0.18) : UIColor(white: 1, alpha: 0.1)
            chip.configuration?.background.strokeColor = selected ? homeGold : .clear
            chip.configuration?.background.strokeWidth = selected ? 1.5 : 0
            chip.accessibilityTraits = selected ? [.button, .selected] : .button
            chip.accessibilityLabel = AppLanguage.choose("Sandbox map: ") + GameSettings.sandboxMapTitles[i]
        }
    }

    func setMode(_ index: Int?, detail: String, enabled: Bool) {
        for (i, card) in modeCards.enumerated() { card.isSelected = i == index; card.isEnabled = enabled }
        modeDetail.text = detail
        online = index != nil && index == GameMode.allCases.firstIndex(of: .online)
        brandSubtitle.text = brandSubtitleText()
    }

    private func pin(_ view: UIView, to parent: UIView) {
        view.translatesAutoresizingMaskIntoConstraints = false
        parent.addSubview(view)
        NSLayoutConstraint.activate([
            view.leadingAnchor.constraint(equalTo: parent.leadingAnchor), view.trailingAnchor.constraint(equalTo: parent.trailingAnchor),
            view.topAnchor.constraint(equalTo: parent.topAnchor), view.bottomAnchor.constraint(equalTo: parent.bottomAnchor)
        ])
    }
}

/// One of the Story mode / Sandbox cards.
private final class ModeCard: UIControl {
    private let panel = Theme.panel(cornerRadius: 11)
    private let icon = UIImageView()
    private let label = UILabel()

    init(title: String, symbol: String) {
        super.init(frame: .zero)
        panel.isUserInteractionEnabled = false
        panel.translatesAutoresizingMaskIntoConstraints = false
        addSubview(panel)
        icon.image = UIImage(systemName: symbol, withConfiguration: UIImage.SymbolConfiguration(pointSize: 14, weight: .semibold))
        label.text = title
        label.font = Theme.body(13, .semibold)
        let row = UIStackView(arrangedSubviews: [icon, label])
        row.spacing = 6
        row.alignment = .center
        row.isUserInteractionEnabled = false
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            panel.leadingAnchor.constraint(equalTo: leadingAnchor), panel.trailingAnchor.constraint(equalTo: trailingAnchor),
            panel.topAnchor.constraint(equalTo: topAnchor), panel.bottomAnchor.constraint(equalTo: bottomAnchor),
            row.centerXAnchor.constraint(equalTo: centerXAnchor), row.centerYAnchor.constraint(equalTo: centerYAnchor),
            row.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 10),
            heightAnchor.constraint(equalToConstant: 34)
        ])
        isAccessibilityElement = true
        accessibilityLabel = title
        update()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    func setTitle(_ title: String) {
        label.text = title
        accessibilityLabel = title
    }

    override var isSelected: Bool { didSet { update() } }
    override var isEnabled: Bool { didSet { update() } }
    override var isHighlighted: Bool { didSet { alpha = isHighlighted ? 0.75 : 1 } }

    private func update() {
        let color = isSelected ? homeGold : Theme.text
        icon.tintColor = color
        label.textColor = isEnabled ? color : Theme.tertiaryText
        panel.layer.borderColor = (isSelected ? homeGold : Theme.panelStroke).cgColor
        panel.layer.borderWidth = isSelected ? 1.5 : 1
        accessibilityTraits = isSelected ? [.button, .selected] : .button
    }
}

private extension UIStackView {
    func addArrangedSubviews(_ views: [UIView]) { views.forEach(addArrangedSubview) }
}
