// The controls (PORT-PLAN.md P6.20, P6.21): the control options with the list of control sets, a set viewed or edited
// as one table of action, keys and controller buttons (the original's two lists merged), a new set's name, and the
// wait for a key or a button. The sets and the rules stay in jonesConfig.c: each button is an action the C side
// carries out, and the state is read back from it after each one.

import AppKit
import SwiftUI

private func labelList(_ labels: UnsafePointer<UnsafePointer<CChar>?>?, _ count: Int) -> [String] {
    (0..<count).map { menuString(labels?[$0]) }
}

/// A fixed-size C char array (a tuple in Swift) as a String.
private func tupleString<T>(_ value: T) -> String {
    withUnsafeBytes(of: value) { raw in
        menuString(raw.baseAddress?.assumingMemoryBound(to: CChar.self))
    }
}

// MARK: - Control options (111)

private final class ControlOptionsModel: ObservableObject {
    @Published var names: [String] = []
    @Published var numFixed = 0
    @Published var selected = 0
    @Published var mouse = false
    @Published var joystick = false
    @Published var joystickAvailable = false
    let options: UnsafeMutablePointer<J3DMenuControlOptions>

    init(_ options: UnsafeMutablePointer<J3DMenuControlOptions>) {
        self.options = options
        read()
    }

    func read() {
        let o = options.pointee
        var list: [String] = []
        withUnsafeBytes(of: o.aaNames) { raw in
            let stride = raw.count / Int(J3DMENU_CTRL_MAXSETS)
            for i in 0..<Int(max(o.numSets, 0)) {
                list.append(menuString(raw.baseAddress!.advanced(by: i * stride).assumingMemoryBound(to: CChar.self)))
            }
        }
        names = list
        numFixed = Int(o.numFixedSets)
        selected = Int(o.selected)
        mouse = o.bMouse != 0
        joystick = o.bJoystick != 0
        joystickAvailable = o.bJoystickAvailable != 0
    }

    /// Returns true when the menu is to close.
    func perform(_ action: Int32, _ value: Int = 0) -> Bool {
        guard let pf = options.pointee.pfAction else { return true }
        let close = pf(options, action, Int32(value)) != 0
        if !close { read() }
        return close
    }
}

private struct ControlOptionsMenu: View {
    let labels: [String]
    @ObservedObject var model: ControlOptionsModel
    let answer: (Int) -> Void

    private var isFixed: Bool { model.selected < model.numFixed }

    var body: some View {
        MenuCard(title: labels[0], width: 680) {
            VStack(spacing: 14) {
                MenuGroup(title: labels[1]) {
                    HStack(alignment: .top, spacing: 12) {
                        ScrollView {
                            LazyVStack(spacing: 2) {
                                ForEach(Array(model.names.enumerated()), id: \.offset) { index, name in
                                    row(index, name)
                                }
                            }
                            .padding(4)
                        }
                        .frame(height: 220)
                        .background(Palette.background, in: RoundedRectangle(cornerRadius: 6, style: .continuous))
                        .overlay { RoundedRectangle(cornerRadius: 6, style: .continuous).strokeBorder(Palette.rule, lineWidth: 1) }
                        VStack(spacing: 8) {
                            Button(isFixed ? labels[4] : labels[3]) { act(J3DMENU_CTRLOPT_EDIT) }
                                .buttonStyle(MenuButtonStyle(fullWidth: true))
                            Button(labels[2]) { act(J3DMENU_CTRLOPT_NEW) }
                                .buttonStyle(MenuButtonStyle(fullWidth: true))
                            Button(labels[5]) { act(J3DMENU_CTRLOPT_DELETE) }
                                .buttonStyle(MenuButtonStyle(fullWidth: true))
                                .disabled(isFixed)
                        }
                        .frame(width: 230)
                    }
                    MenuToggle(title: labels[6], isOn: Binding(get: { model.mouse },
                                                               set: { act(J3DMENU_CTRLOPT_MOUSE, $0 ? 1 : 0) }))
                    MenuToggle(title: labels[7], isOn: Binding(get: { model.joystick },
                                                               set: { act(J3DMENU_CTRLOPT_JOYSTICK, $0 ? 1 : 0) }))
                        .disabled(!model.joystickAvailable)
                        .opacity(model.joystickAvailable ? 1 : 0.4)
                }
                HStack(spacing: 8) {
                    Spacer()
                    Button(labels[9]) { act(J3DMENU_CTRLOPT_CANCEL) }
                        .buttonStyle(MenuButtonStyle())
                        .keyboardShortcut(.cancelAction)
                    Button(labels[8]) { act(J3DMENU_CTRLOPT_OK) }
                        .buttonStyle(MenuButtonStyle(prominent: true))
                        .keyboardShortcut(.defaultAction)
                }
            }
            .padding(20)
        }
    }

    private func row(_ index: Int, _ name: String) -> some View {
        let selected = index == model.selected
        return HStack {
            Text(name)
                .font(.system(size: 14, weight: selected ? .semibold : .regular))
                .lineLimit(1)
            Spacer()
            if index < model.numFixed {
                Image(systemName: "lock.fill")
                    .font(.system(size: 11))
                    .opacity(0.6)
                    .accessibilityLabel("The game's own set")
            }
        }
        .foregroundColor(selected ? Color.black.opacity(0.88) : Palette.text)
        .padding(.horizontal, 8)
        .frame(minHeight: 30)
        .background { if selected { RoundedRectangle(cornerRadius: 5, style: .continuous).fill(Palette.gold) } }
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { act(J3DMENU_CTRLOPT_SELECT, index); act(J3DMENU_CTRLOPT_EDIT) }
        .simultaneousGesture(TapGesture().onEnded { act(J3DMENU_CTRLOPT_SELECT, index) })
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func act(_ action: Int32, _ value: Int = 0) {
        if model.perform(action, value) { answer(1) }
    }
}

@_cdecl("J3DMenuUI_ControlOptions")
public func J3DMenuUI_ControlOptions(_ gameWindow: UnsafeMutableRawPointer?, _ labels: UnsafePointer<UnsafePointer<CChar>?>?,
                                     _ options: UnsafeMutablePointer<J3DMenuControlOptions>?) {
    guard let options else { return }
    let texts = labelList(labels, 10)
    MainActor.assumeIsolated {
        let model = ControlOptionsModel(options)
        _ = runMenu(over: gameWindow) { answer in
            ControlOptionsMenu(labels: texts, model: model, answer: answer)
        }
    }
}

// MARK: - A control set (115)

private struct BindingChip: Identifiable {
    let id: Int
    let name: String
    let glyph: String?  // an SF Symbol for the control (P8.10), when there is one and this macOS has it
}

/// The symbol for a control id (j3dmenu_apple.c), if this macOS draws it.
private func controlGlyph(_ controlId: Int32) -> String? {
    guard let cName = J3DMenuUI_ControlGlyph(controlId) else { return nil }
    let name = String(cString: cName)
    return NSImage(systemSymbolName: name, accessibilityDescription: nil) != nil ? name : nil
}

private struct ControlRow: Identifiable {
    let id: Int
    let action: String
    let keys: [BindingChip]
    let buttons: [BindingChip]
}

private struct SetSelection: Equatable {
    var row: Int
    var side: Int
    var bind: Int   // -1: the row's side, not one of its bindings
}

private final class ControlSetModel: ObservableObject {
    @Published var name = ""
    @Published var readOnly = false
    @Published var rows: [ControlRow] = []
    @Published var selection: SetSelection?
    let set: UnsafeMutablePointer<J3DMenuControlSet>

    init(_ set: UnsafeMutablePointer<J3DMenuControlSet>) {
        self.set = set
        read()
    }

    func read() {
        let s = set.pointee
        name = tupleString(s.aName)
        readOnly = s.bReadOnly != 0
        var list: [ControlRow] = []
        withUnsafeBytes(of: s.aRows) { raw in
            let rowsPointer = raw.baseAddress!.assumingMemoryBound(to: J3DMenuControlRow.self)
            for i in 0..<Int(max(s.numRows, 0)) {
                let r = rowsPointer[i]
                func chips<T>(_ binds: T, _ count: Int32) -> [BindingChip] {
                    withUnsafeBytes(of: binds) { b in
                        let p = b.baseAddress!.assumingMemoryBound(to: J3DMenuBind.self)
                        return (0..<Int(max(count, 0))).map {
                            BindingChip(id: $0, name: tupleString(p[$0].aName), glyph: controlGlyph(p[$0].id))
                        }
                    }
                }
                list.append(ControlRow(id: i, action: tupleString(r.aAction), keys: chips(r.aKeys, r.numKeys),
                                       buttons: chips(r.aButtons, r.numButtons)))
            }
        }
        rows = list
        if let sel = selection {
            let count = sel.row < rows.count ? (sel.side == 0 ? rows[sel.row].keys.count : rows[sel.row].buttons.count) : 0
            if sel.row >= rows.count { selection = nil } else if sel.bind >= count { selection?.bind = -1 }
        }
    }

    func perform(_ action: Int32) -> Bool {
        guard let pf = set.pointee.pfAction else { return true }
        let sel = selection ?? SetSelection(row: -1, side: 0, bind: -1)
        let close = pf(set, action, Int32(sel.row), Int32(sel.side), Int32(sel.bind)) != 0
        if !close { read() }
        return close
    }
}

private struct ControlSetMenu: View {
    let labels: [String]
    @ObservedObject var model: ControlSetModel
    let answer: (Int) -> Void

    var body: some View {
        MenuCard(title: model.name, width: 920) {
            VStack(spacing: 12) {
                HStack(spacing: 8) {
                    Text("").frame(width: 220, alignment: .leading)
                    header(labels[0])
                    header(labels[1])
                }
                .padding(.horizontal, 10)
                ScrollView {
                    LazyVStack(spacing: 2) {
                        ForEach(model.rows) { row in
                            rowView(row)
                        }
                    }
                    .padding(4)
                }
                .frame(height: 400)
                .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Palette.rule.opacity(0.7), lineWidth: 1) }
                HStack(spacing: 8) {
                    if !model.readOnly {
                        Button(labels[2]) { act(J3DMENU_CTRLSET_ADD) }
                            .buttonStyle(MenuButtonStyle())
                            .disabled(model.selection == nil)
                        Button(labels[3]) { act(J3DMENU_CTRLSET_CHANGE) }
                            .buttonStyle(MenuButtonStyle())
                            .disabled((model.selection?.bind ?? -1) < 0)
                        Button(labels[4]) { act(J3DMENU_CTRLSET_REMOVE) }
                            .buttonStyle(MenuButtonStyle())
                            .disabled((model.selection?.bind ?? -1) < 0)
                        Button(labels[5]) { act(J3DMENU_CTRLSET_DEFAULTS) }
                            .buttonStyle(MenuButtonStyle())
                    }
                    Spacer()
                    if model.readOnly {
                        Button(labels[8]) { act(J3DMENU_CTRLSET_CANCEL) }
                            .buttonStyle(MenuButtonStyle(prominent: true))
                            .keyboardShortcut(.defaultAction)
                        cancelKey
                    } else {
                        Button(labels[7]) { act(J3DMENU_CTRLSET_CANCEL) }
                            .buttonStyle(MenuButtonStyle())
                            .keyboardShortcut(.cancelAction)
                        Button(labels[6]) { act(J3DMENU_CTRLSET_SAVE) }
                            .buttonStyle(MenuButtonStyle(prominent: true))
                            .keyboardShortcut(.defaultAction)
                    }
                }
            }
            .padding(20)
        }
    }

    private var cancelKey: some View {
        Button("") { act(J3DMENU_CTRLSET_CANCEL) }
            .keyboardShortcut(.cancelAction)
            .opacity(0)
            .frame(width: 0, height: 0)
            .accessibilityHidden(true)
    }

    private func header(_ text: String) -> some View {
        Text(text.uppercased())
            .font(.system(size: 12, weight: .bold))
            .tracking(1.1)
            .foregroundColor(Palette.gold)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func rowView(_ row: ControlRow) -> some View {
        HStack(spacing: 8) {
            Text(row.action)
                .font(.system(size: 14))
                .foregroundColor(Palette.text)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
                .frame(width: 220, alignment: .leading)
            cell(row, side: 0, chips: row.keys)
            cell(row, side: 1, chips: row.buttons)
        }
        .padding(.horizontal, 6)
        .frame(minHeight: 34)
        .background {
            if model.selection?.row == row.id {
                RoundedRectangle(cornerRadius: 5, style: .continuous).fill(Palette.panelHover)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(row.action)
    }

    private func cell(_ row: ControlRow, side: Int, chips: [BindingChip]) -> some View {
        let cellSelected = model.selection == SetSelection(row: row.id, side: side, bind: -1)
        return HStack(spacing: 4) {
            ForEach(chips) { chip in
                let selected = model.selection == SetSelection(row: row.id, side: side, bind: chip.id)
                Group {
                    if let glyph = chip.glyph {
                        Image(systemName: glyph).font(.system(size: 15, weight: .medium))
                    } else {
                        Text(chip.name)
                    }
                }
                    .font(.system(size: 13, weight: .medium))
                    .help(chip.name)
                    .accessibilityLabel(chip.name)
                    .lineLimit(1)
                    .foregroundColor(selected ? Color.black.opacity(0.88) : Palette.text)
                    .padding(.horizontal, 7)
                    .frame(minHeight: 24)
                    .background {
                        let shape = RoundedRectangle(cornerRadius: 5, style: .continuous)
                        if selected { shape.fill(Palette.gold) } else { shape.fill(Palette.background); shape.strokeBorder(Palette.rule, lineWidth: 1) }
                    }
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { select(row.id, side, chip.id); act(J3DMENU_CTRLSET_CHANGE) }
                    .simultaneousGesture(TapGesture().onEnded { select(row.id, side, chip.id) })
                    .accessibilityAddTraits(.isButton)
                    .accessibilityAddTraits(selected ? .isSelected : [])
            }
            Spacer(minLength: 0)
        }
        .padding(4)
        .frame(maxWidth: .infinity, minHeight: 30)
        .background {
            RoundedRectangle(cornerRadius: 5, style: .continuous).strokeBorder(cellSelected ? Palette.gold : Color.clear, lineWidth: 1)
        }
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { select(row.id, side, -1); act(J3DMENU_CTRLSET_ADD) }
        .simultaneousGesture(TapGesture().onEnded { if model.selection?.row != row.id || model.selection?.side != side || model.selection?.bind == -1 { select(row.id, side, -1) } })
    }

    private func select(_ row: Int, _ side: Int, _ bind: Int) {
        guard !model.readOnly else { return }
        model.selection = SetSelection(row: row, side: side, bind: bind)
    }

    private func act(_ action: Int32) {
        if model.perform(action) { answer(1) }
    }
}

@_cdecl("J3DMenuUI_ControlSet")
public func J3DMenuUI_ControlSet(_ gameWindow: UnsafeMutableRawPointer?, _ labels: UnsafePointer<UnsafePointer<CChar>?>?,
                                 _ set: UnsafeMutablePointer<J3DMenuControlSet>?) {
    guard let set else { return }
    let texts = labelList(labels, 9)
    MainActor.assumeIsolated {
        let model = ControlSetModel(set)
        _ = runMenu(over: gameWindow) { answer in
            ControlSetMenu(labels: texts, model: model, answer: answer)
        }
    }
}

// MARK: - A name (116)

private struct AskNameMenu: View {
    let title: String
    let prompt: String
    let okLabel: String
    let cancelLabel: String
    @State var name: String
    let answer: (Int, String) -> Void

    var body: some View {
        MenuCard(title: title, width: 440) {
            VStack(alignment: .leading, spacing: 12) {
                Text(prompt)
                    .font(.system(size: 15))
                    .foregroundColor(Palette.text)
                TextField("", text: $name)
                    .textFieldStyle(.plain)
                    .font(.system(size: 15))
                    .foregroundColor(Palette.text)
                    .padding(8)
                    .background(Palette.background, in: RoundedRectangle(cornerRadius: 6, style: .continuous))
                    .overlay { RoundedRectangle(cornerRadius: 6, style: .continuous).strokeBorder(Palette.gold, lineWidth: 1) }
                    .onSubmit { answer(1, name) }
                HStack(spacing: 8) {
                    Spacer()
                    Button(cancelLabel) { answer(0, name) }
                        .buttonStyle(MenuButtonStyle())
                        .keyboardShortcut(.cancelAction)
                    Button(okLabel) { answer(1, name) }
                        .buttonStyle(MenuButtonStyle(prominent: true))
                        .keyboardShortcut(.defaultAction)
                }
            }
            .padding(20)
        }
    }
}

@_cdecl("J3DMenuUI_AskName")
public func J3DMenuUI_AskName(_ gameWindow: UnsafeMutableRawPointer?, _ title: UnsafePointer<CChar>?, _ prompt: UnsafePointer<CChar>?,
                              _ okLabel: UnsafePointer<CChar>?, _ cancelLabel: UnsafePointer<CChar>?,
                              _ name: UnsafeMutablePointer<CChar>?, _ size: Int32) -> Int32 {
    guard let name, size > 1 else { return 0 }
    let titleText = menuString(title)
    let promptText = menuString(prompt)
    let ok = menuString(okLabel)
    let cancel = menuString(cancelLabel)
    let start = menuString(UnsafePointer(name))
    var typed = start
    return MainActor.assumeIsolated {
        let result = runMenu(over: gameWindow) { answer in
            AskNameMenu(title: titleText, prompt: promptText, okLabel: ok, cancelLabel: cancel, name: start) { value, text in
                typed = text
                answer(value)
            }
        }
        guard result == 1 else { return 0 }
        // The game's text is Latin-1 where it is not ASCII
        let bytes = Array((typed.data(using: .isoLatin1, allowLossyConversion: true) ?? Data()).prefix(Int(size) - 1))
        for (i, b) in bytes.enumerated() { name[i] = CChar(bitPattern: b) }
        name[bytes.count] = 0
        return 1
    }
}

// MARK: - Waiting for a key or a button (117)

/// The Mac's virtual key codes (Carbon's kVK_*) as USB HID usages, by position, as SDL's scancodes are.
private let keyCodeToHid: [UInt16: Int32] = [
    0x00: 0x04, 0x01: 0x16, 0x02: 0x07, 0x03: 0x09, 0x04: 0x0B, 0x05: 0x0A, 0x06: 0x1D, 0x07: 0x1B, 0x08: 0x06,
    0x09: 0x19, 0x0A: 0x64, 0x0B: 0x05, 0x0C: 0x14, 0x0D: 0x1A, 0x0E: 0x08, 0x0F: 0x15, 0x10: 0x1C, 0x11: 0x17,
    0x12: 0x1E, 0x13: 0x1F, 0x14: 0x20, 0x15: 0x21, 0x16: 0x23, 0x17: 0x22, 0x18: 0x2E, 0x19: 0x26, 0x1A: 0x24,
    0x1B: 0x2D, 0x1C: 0x25, 0x1D: 0x27, 0x1E: 0x30, 0x1F: 0x12, 0x20: 0x18, 0x21: 0x2F, 0x22: 0x0C, 0x23: 0x13,
    0x24: 0x28, 0x25: 0x0F, 0x26: 0x0D, 0x27: 0x34, 0x28: 0x0E, 0x29: 0x33, 0x2A: 0x31, 0x2B: 0x36, 0x2C: 0x38,
    0x2D: 0x11, 0x2E: 0x10, 0x2F: 0x37, 0x30: 0x2B, 0x31: 0x2C, 0x32: 0x35, 0x33: 0x2A, 0x35: 0x29, 0x36: 0xE7,
    0x37: 0xE3, 0x38: 0xE1, 0x39: 0x39, 0x3A: 0xE2, 0x3B: 0xE0, 0x3C: 0xE5, 0x3D: 0xE6, 0x3E: 0xE4, 0x40: 0x6C,
    0x41: 0x63, 0x43: 0x55, 0x45: 0x57, 0x47: 0x53, 0x4B: 0x54, 0x4C: 0x58, 0x4E: 0x56, 0x4F: 0x6D, 0x50: 0x6E,
    0x51: 0x67, 0x52: 0x62, 0x53: 0x59, 0x54: 0x5A, 0x55: 0x5B, 0x56: 0x5C, 0x57: 0x5D, 0x58: 0x5E, 0x59: 0x5F,
    0x5A: 0x6F, 0x5B: 0x60, 0x5C: 0x61, 0x60: 0x3E, 0x61: 0x3F, 0x62: 0x40, 0x63: 0x3C, 0x64: 0x41, 0x65: 0x42,
    0x67: 0x44, 0x69: 0x68, 0x6A: 0x6B, 0x6B: 0x69, 0x6D: 0x43, 0x6F: 0x45, 0x71: 0x6A, 0x72: 0x49, 0x73: 0x4A,
    0x74: 0x4B, 0x75: 0x4C, 0x76: 0x3D, 0x77: 0x4D, 0x78: 0x3B, 0x79: 0x4E, 0x7A: 0x3A, 0x7B: 0x50, 0x7C: 0x4F,
    0x7D: 0x51, 0x7E: 0x52,
]

/// A modifier key's flag: its flags-changed event is a press when the flag is set.
private let modifierFlag: [UInt16: NSEvent.ModifierFlags] = [
    0x36: .command, 0x37: .command, 0x38: .shift, 0x3C: .shift, 0x3A: .option, 0x3D: .option, 0x3B: .control,
    0x3E: .control, 0x39: .capsLock,
]

private final class CaptureState: ObservableObject {
    var monitor: Any?
    var timer: Timer?
    var layer: MenuLayer?
}

private struct CaptureMenu: View {
    let title: String
    let prompt: String
    let action: String
    let cancelLabel: String
    let input: @convention(c) (Int32) -> Int32
    let answer: (Int) -> Void
    @StateObject private var state = CaptureState()

    var body: some View {
        MenuCard(title: title, width: 460) {
            VStack(spacing: 14) {
                Text(prompt)
                    .font(.system(size: 15))
                    .foregroundColor(Palette.text)
                Text(action)
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundColor(Palette.gold)
                Image(systemName: "keyboard")
                    .font(.system(size: 28))
                    .foregroundColor(Palette.dimText)
                    .accessibilityHidden(true)
                Button(cancelLabel) { finish(0) }
                    .buttonStyle(MenuButtonStyle())
            }
            .frame(maxWidth: .infinity)
            .padding(20)
        }
        .onAppear(perform: start)
        .onDisappear(perform: stop)
    }

    private func start() {
        state.layer = menuFrontLayer()
        // Every key goes to the game's check while this is the menu in front; a message it shows takes them back
        state.monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { event in
            guard let layer = state.layer, MainActor.assumeIsolated({ menuIsFront(layer) }) else { return event }
            var hid: Int32 = 0
            if event.type == .keyDown {
                if event.isARepeat { return nil }
                hid = keyCodeToHid[event.keyCode] ?? 0
            } else if let flag = modifierFlag[event.keyCode], event.modifierFlags.contains(flag) {
                hid = keyCodeToHid[event.keyCode] ?? 0
            }
            if hid != 0 && input(hid) != 0 { finish(1) }
            return nil
        }
        let timer = Timer(timeInterval: 0.1, repeats: true) { _ in
            MainActor.assumeIsolated {
                guard let layer = state.layer, menuIsFront(layer) else { return }
                if input(0) != 0 { finish(1) }
            }
        }
        RunLoop.main.add(timer, forMode: .modalPanel)
        state.timer = timer
    }

    private func stop() {
        if let monitor = state.monitor { NSEvent.removeMonitor(monitor) }
        state.monitor = nil
        state.timer?.invalidate()
        state.timer = nil
    }

    private func finish(_ value: Int) {
        stop()
        answer(value)
    }
}

@_cdecl("J3DMenuUI_Capture")
public func J3DMenuUI_Capture(_ gameWindow: UnsafeMutableRawPointer?, _ title: UnsafePointer<CChar>?, _ prompt: UnsafePointer<CChar>?,
                              _ action: UnsafePointer<CChar>?, _ cancelLabel: UnsafePointer<CChar>?,
                              _ input: (@convention(c) (Int32) -> Int32)?) -> Int32 {
    guard let input else { return 0 }
    let titleText = menuString(title)
    let promptText = menuString(prompt)
    let actionText = menuString(action)
    let cancel = menuString(cancelLabel)
    return MainActor.assumeIsolated {
        let result = runMenu(over: gameWindow) { answer in
            CaptureMenu(title: titleText, prompt: promptText, action: actionText, cancelLabel: cancel, input: input, answer: answer)
        }
        return result == 1 ? 1 : 0
    }
}
