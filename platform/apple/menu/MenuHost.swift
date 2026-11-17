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
    let gameContent = gameWindow.contentView.map { gameWindow.convertToScreen($0.convert($0.bounds, to: nil)) } ?? gameWindow.frame
    // Menus are laid out for a 960 by 720 point window and are scaled with it, up to 2 for a large display or
    // fullscreen, so they keep their share of the picture; but never below 13/15, where the 15-point body text would
    // drop under 13 points (PORT-PLAN.md P6.40). Over a smaller window (the game's smallest is 640 by 480) the menu's
    // window reaches past the game's, centred on it and kept on the screen.
    let minimumScale = 13.0 / 15.0
    var frame = gameContent
    let least = CGSize(width: 960 * minimumScale, height: 720 * minimumScale)
    if frame.width < least.width || frame.height < least.height {
        let visible = gameWindow.screen?.visibleFrame ?? frame
        let size = CGSize(width: min(max(frame.width, least.width), visible.width), height: min(max(frame.height, least.height), visible.height))
        var origin = CGPoint(x: frame.midX - size.width / 2, y: frame.midY - size.height / 2)
        origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
        origin.y = min(max(origin.y, visible.minY), visible.maxY - size.height)
        frame = CGRect(origin: origin, size: size)
    }

    let overlay = MenuOverlayWindow(contentRect: frame, styleMask: [.borderless], backing: .buffered, defer: false)
    overlay.isReleasedWhenClosed = false
    overlay.isOpaque = false
    overlay.backgroundColor = .clear
    overlay.hasShadow = false
    overlay.appearance = NSAppearance(named: .darkAqua)

    let answer = MenuAnswer()
    let scale = min(max(min(frame.width / 960, frame.height / 720), minimumScale), 2)
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
    let pad = menuPadTimer(for: overlay)
    let arrows = menuArrowMonitor(for: overlay)
    NSApp.runModal(for: overlay)
    unattended?.invalidate()
    pad.invalidate()
    NSEvent.removeMonitor(arrows)
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
func menuUnattendedTimer(for overlay: NSWindow) -> Timer? {
    let environment = ProcessInfo.processInfo.environment
    guard let text = environment["J3D_MENU_AUTOCANCEL"], let seconds = Double(text), seconds > 0 else { return nil }
    // Repeating: while a menu opened over this one is in front, this one waits for it to close
    let timer = Timer(timeInterval: seconds, repeats: true) { timer in
        MainActor.assumeIsolated {
            guard NSApp.modalWindow === overlay else { return }
            timer.invalidate()
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

// MARK: - Arrows and the controller (PORT-PLAN.md P6.41)

/// Posted with the direction (`MenuMove`) when an arrow key or the controller's pad moves in the menu in front.
extension Notification.Name {
    static let menuMove = Notification.Name("J3DMenuMove")
}

enum MenuMove: Int {
    case up = 1, down, left, right
}

/// The arrow keys, as moves the menu in front can take (a choice moves its highlight). They are still delivered.
@MainActor
func menuArrowMonitor(for overlay: NSWindow) -> Any {
    NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
        guard NSApp.modalWindow === overlay else { return event }
        let moves: [UInt16: MenuMove] = [126: .up, 125: .down, 123: .left, 124: .right]
        if let move = moves[event.keyCode] {
            NotificationCenter.default.post(name: .menuMove, object: overlay, userInfo: ["move": move])
        }
        return event
    } as Any
}

private var menuPadPoll: (@convention(c) () -> Int32)?

/// The C side's controller reading (j3dmenu_apple.c): 1 to 4 a move (up, down, left, right), 5 accept, 6 cancel, 0
/// nothing new.
@_cdecl("J3DMenuUI_SetPadPoll")
public func J3DMenuUI_SetPadPoll(_ poll: (@convention(c) () -> Int32)?) {
    menuPadPoll = poll
}

/// Turns the controller into keys for the menu in front: the pad and stick into arrows, accept into Return, cancel
/// into Escape, so every menu answers a controller as it answers the keyboard.
@MainActor
func menuPadTimer(for overlay: NSWindow) -> Timer {
    let timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { _ in
        MainActor.assumeIsolated {
            guard NSApp.modalWindow === overlay, let poll = menuPadPoll else { return }
            let code = poll()
            let keys: [Int32: (UInt16, String)] = [
                1: (126, "\u{F700}"), 2: (125, "\u{F701}"), 3: (123, "\u{F702}"), 4: (124, "\u{F703}"),
                5: (36, "\r"), 6: (53, "\u{1B}"),
            ]
            guard let (keyCode, characters) = keys[code] else { return }
            for type in [NSEvent.EventType.keyDown, .keyUp] {
                if let event = NSEvent.keyEvent(with: type, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                                windowNumber: overlay.windowNumber, context: nil, characters: characters,
                                                charactersIgnoringModifiers: characters, isARepeat: false, keyCode: keyCode) {
                    NSApp.postEvent(event, atStart: false)
                }
            }
        }
    }
    RunLoop.main.add(timer, forMode: .modalPanel)
    return timer
}
