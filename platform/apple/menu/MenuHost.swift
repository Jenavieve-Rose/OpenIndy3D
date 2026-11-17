// Lays a SwiftUI menu over the game window and waits for its answer (PORT-PLAN.md Phase 6, decision 20).
//
// The game calls from its own loop on the main thread and expects the answer on return, as DialogBoxParam gave it.
// So a menu is a view laid inside the game window, over its picture, and the call runs the window's events until the
// menu answers: the game's last frame stays on screen behind it, dimmed, and the game's own view gets no events until
// it closes. The game window stays the key window throughout: a menu in a window of its own took the focus from it,
// which the game took for the player leaving (in full screen SDL minimized it). A menu opened from a menu (a
// confirmation) is a second layer over the first.

import AppKit
import SwiftUI

/// One menu on screen: the window it is in, its view, and whether it has answered. The layers form a stack; only the
/// last one takes the keyboard, the controller and the tests' timers.
@MainActor
final class MenuLayer {
    let window: NSWindow
    weak var view: NSView?
    private(set) var isDone = false

    init(window: NSWindow) {
        self.window = window
    }

    /// Ends the layer's wait (`runMenu`) or modal session (`runWindow`).
    func close(modal: Bool) {
        guard !isDone else { return }
        isDone = true
        if modal {
            NSApp.stopModal()
        } else if let wake = NSEvent.otherEvent(with: .applicationDefined, location: .zero, modifierFlags: [], timestamp: 0,
                                                windowNumber: 0, context: nil, subtype: 0, data1: 0, data2: 0) {
            NSApp.postEvent(wake, atStart: false) // the wait sleeps until an event comes
        }
    }
}

@MainActor private var menuLayers: [MenuLayer] = []

@MainActor func menuPushLayer(_ layer: MenuLayer) { menuLayers.append(layer) }
@MainActor func menuPopLayer(_ layer: MenuLayer) { menuLayers.removeAll { $0 === layer } }
@MainActor func menuFrontLayer() -> MenuLayer? { menuLayers.last }

/// Whether `layer` is the menu in front: the last layer, in the window that takes events.
@MainActor func menuIsFront(_ layer: MenuLayer) -> Bool {
    guard menuLayers.last === layer else { return false }
    return NSApp.modalWindow == nil || NSApp.modalWindow === layer.window
}

/// The menu's view takes the keyboard from the game's view while it is up.
private final class MenuHostingView<Content: View>: NSHostingView<Content> {
    override var acceptsFirstResponder: Bool { true }
}

private final class MenuAnswer {
    var value = -1
}

/// Text from the game: UTF-8 where it is valid, else the game's Latin-1.
func menuString(_ pointer: UnsafePointer<CChar>?) -> String {
    guard let pointer else { return "" }
    return String(validatingUTF8: pointer) ?? String(cString: pointer, encoding: .isoLatin1) ?? ""
}

/// Shows `content` inside the game window (or any window: the launcher's Options lays its menus in its own) until it
/// calls its `answer` closure; returns what it passed, or -1 when there is no window to show it in.
@MainActor
func runMenu<Content: View>(over gameWindowPointer: UnsafeMutableRawPointer?,
                            @ViewBuilder content: (_ answer: @escaping (Int) -> Void) -> Content) -> Int {
    guard let gameWindowPointer else { return -1 }
    let gameWindow = Unmanaged<NSWindow>.fromOpaque(gameWindowPointer).takeUnretainedValue()
    guard let gameView = gameWindow.contentView else { return -1 }

    // Menus are laid out for a 960 by 720 point window and are scaled to fit it, up to 2 for a large display or full
    // screen, so they keep their share of the picture (PORT-PLAN.md P6.40).
    let bounds = gameView.bounds
    let scale = min(min(bounds.width / 960, bounds.height / 720), 2)

    let layer = MenuLayer(window: gameWindow)
    let answer = MenuAnswer()
    let root = ZStack {
        Color.black.opacity(0.55)
        content { value in
            answer.value = value
            layer.close(modal: false)
        }
        .scaleEffect(scale)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .preferredColorScheme(.dark)
    let host = MenuHostingView(rootView: root)
    host.appearance = NSAppearance(named: .darkAqua)
    host.frame = bounds
    host.autoresizingMask = [.width, .height]
    gameView.addSubview(host, positioned: .above, relativeTo: nil)
    layer.view = host

    let previousResponder = gameWindow.firstResponder
    menuPushLayer(layer)
    gameWindow.makeKeyAndOrderFront(nil)
    gameWindow.makeFirstResponder(host)
    let unattended = menuUnattendedTimer(for: layer)
    let pad = menuPadTimer(for: layer)
    let arrows = menuArrowMonitor(for: layer)

    // The wait: this window's events, and the timers of the modal mode, until the menu answers. Clicks and keys meant
    // for another window are dropped, as a modal session drops them.
    let inputTypes: Set<NSEvent.EventType> = [.leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp, .otherMouseDown,
                                              .otherMouseUp, .keyDown, .keyUp, .scrollWheel]
    while !layer.isDone {
        autoreleasepool {
            guard let event = NSApp.nextEvent(matching: .any, until: .distantFuture, inMode: .modalPanel, dequeue: true) else { return }
            if inputTypes.contains(event.type), let target = event.window, target !== gameWindow {
                return
            }
            NSApp.sendEvent(event)
        }
    }

    unattended?.invalidate()
    pad.invalidate()
    NSEvent.removeMonitor(arrows)
    menuPopLayer(layer)
    host.removeFromSuperview()
    gameWindow.makeFirstResponder(previousResponder)
    return answer.value
}

// For tests, as J3DGui_SetModalHook is for the Win32 dialogs: with J3D_MENU_AUTOCANCEL=<seconds> in the environment
// every menu closes unanswered after that long (its caller sees a cancel), and with J3D_MENU_SHOT=<prefix> as well
// the menu is first written to <prefix>-<n>.png. The game's own frame is not in the picture: Metal drew it.
private var menuShotCount = 0

@MainActor
func menuUnattendedTimer(for layer: MenuLayer, modal: Bool = false) -> Timer? {
    let environment = ProcessInfo.processInfo.environment
    guard let text = environment["J3D_MENU_AUTOCANCEL"], let seconds = Double(text), seconds > 0 else { return nil }
    // Repeating: while a menu opened over this one is in front, this one waits for it to close
    let timer = Timer(timeInterval: seconds, repeats: true) { timer in
        MainActor.assumeIsolated {
            guard menuIsFront(layer) else { return }
            timer.invalidate()
            if let prefix = environment["J3D_MENU_SHOT"], let view = layer.view ?? layer.window.contentView,
               let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                view.cacheDisplay(in: view.bounds, to: rep)
                menuShotCount += 1
                try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: "\(prefix)-\(menuShotCount).png"))
            }
            layer.close(modal: modal)
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
func menuArrowMonitor(for layer: MenuLayer) -> Any {
    NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
        guard MainActor.assumeIsolated({ menuIsFront(layer) }) else { return event }
        let moves: [UInt16: MenuMove] = [126: .up, 125: .down, 123: .left, 124: .right]
        if let move = moves[event.keyCode] {
            NotificationCenter.default.post(name: .menuMove, object: layer.window, userInfo: ["move": move])
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
func menuPadTimer(for layer: MenuLayer) -> Timer {
    let timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { _ in
        MainActor.assumeIsolated {
            guard menuIsFront(layer), let poll = menuPadPoll else { return }
            let code = poll()
            let keys: [Int32: (UInt16, String)] = [
                1: (126, "\u{F700}"), 2: (125, "\u{F701}"), 3: (123, "\u{F702}"), 4: (124, "\u{F703}"),
                5: (36, "\r"), 6: (53, "\u{1B}"),
            ]
            guard let (keyCode, characters) = keys[code] else { return }
            for type in [NSEvent.EventType.keyDown, .keyUp] {
                if let event = NSEvent.keyEvent(with: type, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                                windowNumber: layer.window.windowNumber, context: nil, characters: characters,
                                                charactersIgnoringModifiers: characters, isARepeat: false, keyCode: keyCode) {
                    NSApp.postEvent(event, atStart: false)
                }
            }
        }
    }
    RunLoop.main.add(timer, forMode: .modalPanel)
    return timer
}
