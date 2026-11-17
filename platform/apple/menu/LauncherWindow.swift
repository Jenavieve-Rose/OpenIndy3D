// The launcher and its Options window (PORT-PLAN.md 6B): windows of their own, shown before the game window, in the
// look the in-game menus share (MenuStyle.swift, MenuControls.swift). They hold no game logic: the C side
// (j3dmenu_apple.c, jonesConfig.c) gives the picture, the labels and the settings as form items, and does what the
// player chose.

import AppKit
import SwiftUI

// MARK: - A window run modally

private final class ModalWindowDelegate: NSObject, NSWindowDelegate {
    var onClose: () -> Void = {}
    func windowWillClose(_ notification: Notification) { onClose() }
}

/// A window without a title bar still takes the keyboard (Return, Escape, Tab).
private final class KeyableWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
}

private final class WindowAnswer {
    var value: Int
    init(_ value: Int) { self.value = value }
}

/// Shows `content` in a window of its own, runs it modally and returns what it answered: titled with the title bar
/// hidden (closing it answers `closeValue`), or with no title bar at all, as the original launcher's window.
@MainActor
private func runWindow<Content: View>(title: String, parent: NSWindow?, closeValue: Int, titled: Bool = true,
                                      @ViewBuilder content: (_ answer: @escaping (Int) -> Void) -> Content) -> Int {
    let answer = WindowAnswer(closeValue)
    let window = KeyableWindow(contentRect: NSRect(x: 0, y: 0, width: 480, height: 600),
                               styleMask: titled ? [.titled, .closable, .fullSizeContentView] : [.borderless],
                               backing: .buffered, defer: false)
    window.hasShadow = true
    window.isReleasedWhenClosed = false
    window.title = title
    window.titleVisibility = .hidden
    window.titlebarAppearsTransparent = true
    window.isMovableByWindowBackground = true
    window.appearance = NSAppearance(named: .darkAqua)
    window.backgroundColor = NSColor(Palette.background)
    let delegate = ModalWindowDelegate()
    var answered = false
    delegate.onClose = {
        if !answered { NSApp.stopModal() }
    }
    window.delegate = delegate
    let host = NSHostingView(rootView: content { value in
        answered = true
        answer.value = value
        NSApp.stopModal()
    }
    .preferredColorScheme(.dark))
    // The content in a plain view of its own, which a menu opened from this window (runMenu) is laid over as a sibling:
    // a view added inside the SwiftUI hosting view was never drawn, though it took the clicks
    let size = host.fittingSize
    let container = NSView(frame: NSRect(origin: .zero, size: size))
    container.wantsLayer = true
    host.frame = container.bounds
    host.autoresizingMask = [.width, .height]
    container.addSubview(host)
    window.contentView = container
    window.setContentSize(size)
    // The hidden title bar's height stays above the content, as it did when the hosting view was the content view
    let titleBar = container.bounds.height - window.contentLayoutRect.height
    if titleBar > 0 {
        window.setContentSize(NSSize(width: size.width, height: size.height + titleBar))
        host.frame = NSRect(origin: .zero, size: size)
    }
    if let parent {
        let p = parent.frame
        window.setFrameOrigin(NSPoint(x: p.midX - window.frame.width / 2, y: p.midY - window.frame.height / 2))
    } else {
        window.center()
    }
    NSApp.setActivationPolicy(.regular)
    NSApp.activate(ignoringOtherApps: true)
    window.makeKeyAndOrderFront(nil)
    let layer = MenuLayer(window: window)
    layer.view = window.contentView
    menuPushLayer(layer)
    let unattended = menuUnattendedTimer(for: layer, modal: true)
    let pad = menuPadTimer(for: layer)
    let arrows = menuArrowMonitor(for: layer)
    NSApp.runModal(for: window)
    unattended?.invalidate()
    pad.invalidate()
    NSEvent.removeMonitor(arrows)
    menuPopLayer(layer)
    window.delegate = nil
    window.orderOut(nil)
    return answer.value
}

// MARK: - Options

private struct OptionsView: View {
    let title: String
    let tabs: [String]
    let models: [FormModel]
    let okLabel: String
    let cancelLabel: String
    let defaultsLabel: String
    let answer: (Int) -> Void
    @State private var tab = 0

    var body: some View {
        VStack(spacing: 0) {
            Text(title.uppercased())
                .font(.system(size: 13, weight: .bold))
                .tracking(1.4)
                .foregroundColor(Palette.gold)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 20)
                .padding(.top, 30)
                .padding(.bottom, 12)
            MenuChoiceBar(options: tabs, selection: $tab)
                .padding(.horizontal, 20)
                .padding(.bottom, 14)
            Rectangle().fill(Palette.goldRule).frame(height: 2)
            ScrollView {
                FormItemsView(model: models[tab])
                    .padding(20)
            }
            .frame(height: 470)
            Rectangle().fill(Palette.rule).frame(height: 1)
            HStack(spacing: 8) {
                Button(defaultsLabel) { models[tab].notify(-1) }
                    .buttonStyle(MenuButtonStyle())
                    .disabled(!models[tab].canReset)
                Spacer()
                Button(cancelLabel) { answer(0) }
                    .buttonStyle(MenuButtonStyle())
                    .keyboardShortcut(.cancelAction)
                Button(okLabel) {
                    for model in models { model.writeBack() }
                    answer(1)
                }
                .buttonStyle(MenuButtonStyle(prominent: true))
                .keyboardShortcut(.defaultAction)
            }
            .padding(16)
        }
        .frame(width: 560)
        .background(Palette.background)
        .onAppear {
            // For tests: J3D_MENU_TEST_PRESS=<tab>:<item> presses a button item, as a click would
            guard let press = ProcessInfo.processInfo.environment["J3D_MENU_TEST_PRESS"] else { return }
            let parts = press.split(separator: ":").compactMap { Int($0) }
            guard parts.count == 2, parts[0] < models.count else { return }
            DispatchQueue.main.async {
                tab = parts[0]
                let timer = Timer(timeInterval: 0.3, repeats: false) { _ in
                    MainActor.assumeIsolated { models[parts[0]].notify(parts[1]) }
                }
                RunLoop.main.add(timer, forMode: .modalPanel)
            }
        }
    }
}

@_cdecl("J3DMenuUI_OptionsWindow")
public func J3DMenuUI_OptionsWindow(_ parentWindow: UnsafeMutableRawPointer?, _ title: UnsafePointer<CChar>?,
                                    _ pages: UnsafeMutablePointer<J3DMenuFormPage>?, _ numPages: Int32,
                                    _ okLabel: UnsafePointer<CChar>?, _ cancelLabel: UnsafePointer<CChar>?,
                                    _ defaultsLabel: UnsafePointer<CChar>?) -> Int32 {
    guard let pages, numPages > 0 else { return 0 }
    let titleText = menuString(title)
    var tabs: [String] = []
    var cPages: [J3DMenuFormPage] = []
    for i in 0..<Int(numPages) {
        tabs.append(menuString(pages[i].pTitle))
        cPages.append(pages[i])
    }
    let ok = menuString(okLabel)
    let cancel = menuString(cancelLabel)
    let defaults = menuString(defaultsLabel)
    return MainActor.assumeIsolated {
        let parent = parentWindow.map { Unmanaged<NSWindow>.fromOpaque($0).takeUnretainedValue() }
        let models = cPages.compactMap { page -> FormModel? in
            guard let items = page.pItems, page.numItems > 0 else { return nil }
            return FormModel(cItems: items, count: Int(page.numItems), changed: page.pfChanged)
        }
        guard models.count == tabs.count else { return 0 }
        let result = runWindow(title: titleText, parent: parent, closeValue: 0) { answer in
            OptionsView(title: titleText, tabs: tabs, models: models, okLabel: ok, cancelLabel: cancel,
                        defaultsLabel: defaults, answer: answer)
        }
        return result == 1 ? 1 : 0
    }
}

// MARK: - Launcher

private struct LaunchPicture: View {
    let image: NSImage?

    var body: some View {
        Group {
            if let image {
                // The original is 320 by 240: drawn at 1.5 times, without smoothing
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(4.0 / 3.0, contentMode: .fit)
            } else {
                ZStack {
                    LinearGradient(colors: [Palette.panelHover, Palette.background], startPoint: .top, endPoint: .bottom)
                    VStack(spacing: 6) {
                        Text("INDIANA JONES")
                            .font(.system(size: 40, weight: .heavy, design: .serif))
                            .foregroundStyle(Palette.goldFill)
                        Text("and the Infernal Machine")
                            .font(.system(size: 20, weight: .medium, design: .serif))
                            .foregroundColor(Palette.text)
                    }
                }
                .aspectRatio(4.0 / 3.0, contentMode: .fit)
            }
        }
        .frame(width: 480)
        .accessibilityLabel("Indiana Jones and the Infernal Machine")
    }
}

private struct LauncherView: View {
    let picture: NSImage?
    let labels: [String]
    let resumeDetail: String?
    let status: String
    let version: String
    let onOptions: () -> Void
    let onHelp: () -> Void
    let hasHelp: Bool
    let answer: (Int) -> Void

    private static let symbols = ["play.fill", "arrow.uturn.forward", "questionmark.circle", "slider.horizontal.3", "xmark"]
    /// The button Return presses: Resume Play when there is a save to resume, else Start New Game (the original's
    /// default), moved with the arrow keys or a controller's pad
    @State private var highlighted: Int?

    private var current: Int { highlighted ?? (resumeDetail != nil ? 1 : 0) }

    private func isEnabled(_ index: Int) -> Bool { !(index == 1 && resumeDetail == nil) && !(index == 2 && !hasHelp) }

    private func move(_ step: Int) {
        var index = current
        for _ in 0..<5 {
            index = (index + step + 5) % 5
            if isEnabled(index) { highlighted = index; return }
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            LaunchPicture(image: picture)
            Rectangle().fill(Palette.goldRule).frame(height: 2)
            VStack(spacing: 8) {
                ForEach(0..<5, id: \.self) { index in
                    button(index)
                }
            }
            .padding(.horizontal, 20)
            .padding(.top, 18)
            HStack(spacing: 6) {
                Text(status)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer(minLength: 8)
                Text(version)
            }
            .font(.system(size: 12))
            .foregroundColor(Palette.dimText)
            .padding(.horizontal, 22)
            .padding(.vertical, 14)
        }
        .frame(width: 480)
        .background(Palette.background)
        .onReceive(NotificationCenter.default.publisher(for: .menuMove)) { note in
            guard let move = note.userInfo?["move"] as? MenuMove else { return }
            move == .up || move == .left ? self.move(-1) : self.move(1)
        }
        .onAppear {
            // For tests: J3D_MENU_TEST_LAUNCHER_PRESS=options presses the launcher's own Options button once it is up,
            // as a click would, then Quit once Options has closed
            guard ProcessInfo.processInfo.environment["J3D_MENU_TEST_LAUNCHER_PRESS"] == "options" else { return }
            let timer = Timer(timeInterval: 0.5, repeats: false) { _ in
                MainActor.assumeIsolated {
                    onOptions()
                    menuAfterAction { answer(Int(J3DMENU_LAUNCH_QUIT)) } // after Options, which waits its turn too
                }
            }
            RunLoop.main.add(timer, forMode: .modalPanel)
        }
    }

    @ViewBuilder
    private func button(_ index: Int) -> some View {
        let primary = current
        let base = Button {
            switch index {
            case 0: answer(Int(J3DMENU_LAUNCH_NEWGAME))
            case 1: answer(Int(J3DMENU_LAUNCH_RESUME))
            case 2: onHelp()
            case 3: onOptions()
            default: answer(Int(J3DMENU_LAUNCH_QUIT))
            }
        } label: {
            HStack(spacing: 10) {
                Image(systemName: Self.symbols[index])
                    .font(.system(size: 14, weight: .semibold))
                    .frame(width: 20)
                Text(labels[index])
                Spacer()
                if index == 1, let resumeDetail {
                    Text(resumeDetail)
                        .font(.system(size: 13))
                        .lineLimit(1)
                        .opacity(0.7)
                }
            }
        }
        .buttonStyle(MenuButtonStyle(prominent: index == primary, large: true))
        .disabled(!isEnabled(index))
        if index == primary {
            base.keyboardShortcut(.defaultAction)
        } else if index == 4 {
            base.keyboardShortcut(.cancelAction)
        } else {
            base
        }
    }
}

@_cdecl("J3DMenuUI_Launcher")
public func J3DMenuUI_Launcher(_ uiPointer: UnsafePointer<J3DMenuLauncherUI>?) -> Int32 {
    guard let uiPointer else { return Int32(J3DMENU_LAUNCH_QUIT) }
    let ui = uiPointer.pointee
    var picture: NSImage?
    if let bytes = ui.pPicture, ui.pictureSize > 0 {
        picture = NSImage(data: Data(bytes: bytes, count: Int(ui.pictureSize)))
    }
    var labels: [String] = []
    withUnsafePointer(to: ui.apLabels) { tuple in
        tuple.withMemoryRebound(to: UnsafePointer<CChar>?.self, capacity: 5) { list in
            for i in 0..<5 { labels.append(menuString(list[i])) }
        }
    }
    let resume = ui.pResumeDetail.map { menuString($0) }
    let status = menuString(ui.pStatus)
    let version = menuString(ui.pVersion)
    let title = menuString(ui.pTitle)
    let pfOptions = ui.pfOptions
    let helpPath = ui.pHelpPath.map { menuString($0) }
    return MainActor.assumeIsolated {
        var launcherWindow: NSWindow?
        let result = runWindow(title: title, parent: nil, closeValue: Int(J3DMENU_LAUNCH_QUIT), titled: false) { answer in
            LauncherView(picture: picture, labels: labels, resumeDetail: resume, status: status, version: version,
                         onOptions: {
                             launcherWindow = launcherWindow ?? NSApp.modalWindow
                             let window = launcherWindow
                             menuAfterAction { pfOptions?(window.map { Unmanaged.passUnretained($0).toOpaque() }) }
                         },
                         onHelp: { if let helpPath { NSWorkspace.shared.open(URL(fileURLWithPath: helpPath)) } },
                         hasHelp: helpPath != nil,
                         answer: answer)
        }
        return Int32(result)
    }
}

@_cdecl("J3DMenuUI_FrontWindow")
public func J3DMenuUI_FrontWindow() -> UnsafeMutableRawPointer? {
    let window = MainActor.assumeIsolated { NSApp.modalWindow.map { Int(bitPattern: Unmanaged.passUnretained($0).toOpaque()) } }
    // Passed through as a plain address: a pointer is not Sendable across the isolation boundary
    return window.flatMap { UnsafeMutableRawPointer(bitPattern: $0) }
}
