// The in-game menus and their entry points for j3dmenu_apple.c (j3dmenu_ui.h, PORT-PLAN.md Phase 6).
// The game's rules stay on the C side; a menu here only shows what it is given and returns the player's answer.

import AppKit
import SwiftUI

// MARK: - A message or a question

private struct ChoiceMenu: View {
    let title: String
    let text: String
    let buttons: [String]
    let defaultIndex: Int
    let cancelIndex: Int
    let column: Bool
    let disabledMask: UInt32
    let answer: (Int) -> Void

    var body: some View {
        MenuCard(title: title, width: column ? 380 : 440) {
            VStack(spacing: 18) {
                if !text.isEmpty {
                    Text(text)
                        .font(.system(size: 15))
                        .foregroundColor(Palette.text)
                        .multilineTextAlignment(.leading)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                if column {
                    VStack(spacing: 8) { buttonViews }
                } else {
                    HStack(spacing: 8) {
                        Spacer()
                        buttonViews
                    }
                }
            }
            .padding(20)
        }
    }

    @ViewBuilder
    private var buttonViews: some View {
        ForEach(Array(buttons.enumerated()), id: \.offset) { index, label in
            let button = Button(label) { answer(index) }
                .buttonStyle(MenuButtonStyle(prominent: index == defaultIndex, large: column))
                .disabled(disabledMask & (1 << UInt32(index)) != 0)
            if index == defaultIndex {
                // Escape answers with the cancel button even when that is also the default one
                button.keyboardShortcut(.defaultAction)
            } else if index == cancelIndex {
                button.keyboardShortcut(.cancelAction)
            } else {
                button
            }
        }
        if cancelIndex == defaultIndex && cancelIndex >= 0 {
            Button("") { answer(cancelIndex) }
                .keyboardShortcut(.cancelAction)
                .opacity(0)
                .frame(width: 0, height: 0)
                .accessibilityHidden(true)
        }
    }
}

@_cdecl("J3DMenuUI_Choice")
public func J3DMenuUI_Choice(_ gameWindow: UnsafeMutableRawPointer?, _ title: UnsafePointer<CChar>?, _ text: UnsafePointer<CChar>?,
                             _ buttons: UnsafePointer<UnsafePointer<CChar>?>?, _ numButtons: Int32, _ defaultIndex: Int32,
                             _ cancelIndex: Int32, _ layout: Int32, _ disabledMask: UInt32) -> Int32 {
    var labels: [String] = []
    if let buttons {
        for i in 0..<min(Int(numButtons), 8) {
            labels.append(menuString(buttons[i]))
        }
    }
    let titleText = menuString(title)
    let bodyText = menuString(text)
    return MainActor.assumeIsolated {
        Int32(runMenu(over: gameWindow) { answer in
            ChoiceMenu(title: titleText, text: bodyText, buttons: labels, defaultIndex: Int(defaultIndex),
                       cancelIndex: Int(cancelIndex), column: layout == J3DMENU_CHOICE_COLUMN, disabledMask: disabledMask,
                       answer: answer)
        })
    }
}

// MARK: - Saved games

private struct SaveEntry: Identifiable {
    let name: String        // the file's name, with its extension
    let modified: Date
    var id: String { name }

    var title: String {
        name.lowercased().hasSuffix(".nds") ? String(name.dropLast(4)) : name
    }
}

private func thumbnailImage(_ pixels: [UInt8]) -> NSImage? {
    let width = Int(J3DMENU_THUMB_WIDTH)
    let height = Int(J3DMENU_THUMB_HEIGHT)
    guard pixels.count == width * height * 4, let provider = CGDataProvider(data: Data(pixels) as CFData),
          let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                              space: CGColorSpaceCreateDeviceRGB(),
                              bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                              provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
    else { return nil }
    return NSImage(cgImage: image, size: NSSize(width: width, height: height))
}

private struct SavePickerMenu: View {
    let title: String
    let saving: Bool
    let saves: [SaveEntry]
    let acceptLabel: String
    let cancelLabel: String
    let currentThumb: NSImage?
    let loadThumb: (String) -> NSImage?
    /// The chosen name, or nil for cancel.
    let answer: (String?) -> Void

    @State var name: String
    @State private var thumb: NSImage?
    @FocusState private var nameFocused: Bool

    private static let dateFormat: DateFormatter = {
        let format = DateFormatter()
        format.dateStyle = .medium
        format.timeStyle = .short
        return format
    }()

    private var trimmedName: String { name.trimmingCharacters(in: .whitespaces) }

    private func isSelected(_ save: SaveEntry) -> Bool {
        let typed = trimmedName.lowercased()
        return typed == save.name.lowercased() || typed == save.title.lowercased()
    }

    var body: some View {
        MenuCard(title: title, width: 640) {
            VStack(spacing: 14) {
                HStack(alignment: .top, spacing: 14) {
                    saveList
                    VStack(alignment: .leading, spacing: 8) {
                        picture
                        Text(pictureCaption)
                            .font(.system(size: 12))
                            .foregroundColor(Palette.dimText)
                            .lineLimit(2)
                            .frame(width: 256, alignment: .leading)
                    }
                }
                HStack(spacing: 10) {
                    Text("Name")
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(Palette.dimText)
                    TextField("", text: $name)
                        .textFieldStyle(.plain)
                        .font(.system(size: 15))
                        .foregroundColor(Palette.text)
                        .focused($nameFocused)
                        .disabled(!saving)
                        .padding(.horizontal, 10)
                        .frame(height: 32)
                        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 6, style: .continuous))
                        .overlay {
                            RoundedRectangle(cornerRadius: 6, style: .continuous)
                                .strokeBorder(nameFocused ? Palette.gold : Palette.rule, lineWidth: 1)
                        }
                        .accessibilityLabel("Name")
                    Button(cancelLabel) { answer(nil) }
                        .buttonStyle(MenuButtonStyle())
                        .keyboardShortcut(.cancelAction)
                    Button(acceptLabel) { answer(trimmedName) }
                        .buttonStyle(MenuButtonStyle(prominent: true))
                        .keyboardShortcut(.defaultAction)
                        .disabled(trimmedName.isEmpty)
                }
            }
            .padding(20)
        }
        .onAppear {
            refreshThumb()
            nameFocused = saving
        }
        .onChange(of: name) { _ in refreshThumb() }
    }

    private var saveList: some View {
        ScrollView {
            LazyVStack(spacing: 2) {
                if saves.isEmpty {
                    Text("No saved games yet")
                        .font(.system(size: 14))
                        .foregroundColor(Palette.dimText)
                        .padding(.top, 24)
                }
                ForEach(saves) { save in
                    let selected = isSelected(save)
                    HStack {
                        Text(save.title)
                            .font(.system(size: 14, weight: selected ? .semibold : .regular))
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Spacer(minLength: 8)
                        Text(Self.dateFormat.string(from: save.modified))
                            .font(.system(size: 12))
                            .opacity(0.75)
                            .lineLimit(1)
                    }
                    .foregroundColor(selected ? Color.black.opacity(0.88) : Palette.text)
                    .padding(.horizontal, 10)
                    .frame(height: 30)
                    .background(selected ? Palette.gold : Color.clear, in: RoundedRectangle(cornerRadius: 5, style: .continuous))
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) {
                        name = save.name
                        answer(save.name)
                    }
                    .onTapGesture { name = save.name }
                    .accessibilityElement(children: .combine)
                    .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
                }
            }
            .padding(4)
        }
        .frame(height: 230)
        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 7, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 7, style: .continuous).strokeBorder(Palette.rule, lineWidth: 1)
        }
    }

    private var picture: some View {
        ZStack {
            Palette.panel
            if let thumb {
                Image(nsImage: thumb)
                    .resizable()
                    .interpolation(.high)
            } else {
                Image(systemName: "photo")
                    .font(.system(size: 28))
                    .foregroundColor(Palette.rule)
            }
        }
        .frame(width: 256, height: 192)
        .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 7, style: .continuous).strokeBorder(Palette.rule, lineWidth: 1)
        }
        .accessibilityLabel("Picture of the saved game")
    }

    private var pictureCaption: String {
        if saving {
            return saves.contains(where: isSelected) ? "Saving here replaces this saved game." : "The game as it is now."
        }
        return saves.first(where: isSelected).map { Self.dateFormat.string(from: $0.modified) } ?? "Choose a saved game."
    }

    // Saving shows the game as it is now, which is what the save will carry; loading shows the chosen save.
    private func refreshThumb() {
        if saving {
            thumb = currentThumb
        } else {
            thumb = saves.first(where: isSelected).flatMap { loadThumb($0.name) }
        }
    }
}

@_cdecl("J3DMenuUI_PickSave")
public func J3DMenuUI_PickSave(_ gameWindow: UnsafeMutableRawPointer?, _ pickerPointer: UnsafePointer<J3DMenuSavePicker>?,
                               _ outName: UnsafeMutablePointer<CChar>?) -> Int32 {
    guard let pickerPointer, let outName else { return 0 }
    let picker = pickerPointer.pointee
    let thumbBytes = Int(J3DMENU_THUMB_WIDTH) * Int(J3DMENU_THUMB_HEIGHT) * 4

    var saves: [SaveEntry] = []
    if let list = picker.pSaves {
        for i in 0..<Int(picker.numSaves) {
            var entry = list[i]
            let name = withUnsafePointer(to: &entry.aName) {
                $0.withMemoryRebound(to: CChar.self, capacity: Int(J3DMENU_NAME_SIZE)) { menuString($0) }
            }
            saves.append(SaveEntry(name: name, modified: Date(timeIntervalSince1970: TimeInterval(entry.modified))))
        }
    }
    let currentThumb = picker.pCurrentThumb.flatMap { thumbnailImage(Array(UnsafeBufferPointer(start: $0, count: thumbBytes))) }
    let loadThumbCallback = picker.pfLoadThumb
    let loadThumb: (String) -> NSImage? = { name in
        guard let loadThumbCallback else { return nil }
        var pixels = [UInt8](repeating: 0, count: thumbBytes)
        let loaded = name.withCString { loadThumbCallback($0, &pixels) }
        return loaded != 0 ? thumbnailImage(pixels) : nil
    }
    let title = menuString(picker.pTitle)
    let acceptLabel = menuString(picker.pAcceptLabel)
    let cancelLabel = menuString(picker.pCancelLabel)
    let initialName = menuString(picker.pInitialName)
    let saving = picker.bSave != 0

    var chosen: String?
    let result = MainActor.assumeIsolated {
        runMenu(over: gameWindow) { answer in
            SavePickerMenu(title: title, saving: saving, saves: saves, acceptLabel: acceptLabel, cancelLabel: cancelLabel,
                           currentThumb: currentThumb, loadThumb: loadThumb,
                           answer: { name in
                               chosen = name
                               answer(name == nil ? 0 : 1)
                           },
                           name: initialName)
        }
    }
    guard result == 1, let chosen, !chosen.isEmpty else { return 0 }
    // The name goes to the file system as it is, in UTF-8. One too long for the buffer is not a name the game can use.
    let bytes = Array(chosen.utf8)
    guard bytes.count < Int(J3DMENU_NAME_SIZE) else { return 0 }
    for (i, byte) in bytes.enumerated() {
        outName[i] = CChar(bitPattern: byte)
    }
    outName[bytes.count] = 0
    return 1
}
