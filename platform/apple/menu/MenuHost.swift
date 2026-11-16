// Lays a SwiftUI menu over the game window and waits for its answer (PORT-PLAN.md Phase 6, decision 20).
//
// The game calls from its own loop on the main thread and expects the answer on return, as DialogBoxParam gave it.
// So a menu is a borderless, transparent child window covering the game window's content, run as a modal session:
// the game's last frame stays on screen behind it, dimmed, and the game window gets no events until it closes.
// A menu opened from a menu (a confirmation) is a second session over the first.

import AppKit
import SwiftUI

/// A borderless window does not take the keyboard unless it says it can.
private final class MenuOverlayWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
}

private final class MenuAnswer {
    var value = -1
}

/// Text from the game: UTF-8 where it is valid, else the game's Latin-1.
func menuString(_ pointer: UnsafePointer<CChar>?) -> String {
    guard let pointer else { return "" }
    return String(validatingUTF8: pointer) ?? String(cString: pointer, encoding: .isoLatin1) ?? ""
}

/// Shows `content` over the game window until it calls its `answer` closure; returns what it passed, or -1 when
/// there is no window to show it on.
@MainActor
func runMenu<Content: View>(over gameWindowPointer: UnsafeMutableRawPointer?,
                            @ViewBuilder content: (_ answer: @escaping (Int) -> Void) -> Content) -> Int {
    guard let gameWindowPointer else { return -1 }
    let gameWindow = Unmanaged<NSWindow>.fromOpaque(gameWindowPointer).takeUnretainedValue()
    let frame = gameWindow.contentView.map { gameWindow.convertToScreen($0.convert($0.bounds, to: nil)) } ?? gameWindow.frame

    let overlay = MenuOverlayWindow(contentRect: frame, styleMask: [.borderless], backing: .buffered, defer: false)
    overlay.isReleasedWhenClosed = false
    overlay.isOpaque = false
    overlay.backgroundColor = .clear
    overlay.hasShadow = false
    overlay.appearance = NSAppearance(named: .darkAqua)

    let answer = MenuAnswer()
    // Menus are laid out for a 960 by 720 point window and are scaled with the window, down to 0.6 for the game's
    // smallest (640 by 480) and up to 2 for a large display or fullscreen, so they keep their share of the picture.
    let scale = min(max(min(frame.width / 960, frame.height / 720), 0.6), 2)
    let root = ZStack {
        Color.black.opacity(0.55)
        content { value in
            answer.value = value
            NSApp.stopModal()
        }
        .scaleEffect(scale)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .preferredColorScheme(.dark)
    overlay.contentView = NSHostingView(rootView: root)
    overlay.setFrame(frame, display: false)

    gameWindow.addChildWindow(overlay, ordered: .above)
    overlay.makeKeyAndOrderFront(nil)
    let unattended = menuUnattendedTimer(for: overlay)
    NSApp.runModal(for: overlay)
    unattended?.invalidate()
    gameWindow.removeChildWindow(overlay)
    overlay.orderOut(nil)
    if NSApp.modalWindow == nil {
        gameWindow.makeKeyAndOrderFront(nil)
    }
    return answer.value
}

// For tests, as J3DGui_SetModalHook is for the Win32 dialogs: with J3D_MENU_AUTOCANCEL=<seconds> in the environment
// every menu closes unanswered after that long (its caller sees a cancel), and with J3D_MENU_SHOT=<prefix> as well
// the menu is first written to <prefix>-<n>.png. The game's own frame is not in the picture: Metal drew it.
private var menuShotCount = 0

@MainActor
private func menuUnattendedTimer(for overlay: NSWindow) -> Timer? {
    let environment = ProcessInfo.processInfo.environment
    guard let text = environment["J3D_MENU_AUTOCANCEL"], let seconds = Double(text), seconds > 0 else { return nil }
    let timer = Timer(timeInterval: seconds, repeats: false) { _ in
        MainActor.assumeIsolated {
            guard NSApp.modalWindow === overlay else { return }
            if let prefix = environment["J3D_MENU_SHOT"], let view = overlay.contentView,
               let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                view.cacheDisplay(in: view.bounds, to: rep)
                menuShotCount += 1
                try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: "\(prefix)-\(menuShotCount).png"))
            }
            NSApp.stopModal()
        }
    }
    RunLoop.main.add(timer, forMode: .modalPanel)
    return timer
}
