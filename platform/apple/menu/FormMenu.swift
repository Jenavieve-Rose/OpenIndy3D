// A form of settings, described by the C side as a list of items (J3DMenuUI_Form, j3dmenu_ui.h): the game play,
// sound and display options. What the items mean, and what changing one does to the others, stays in C; after each
// change the form asks (pfChanged) and reads the items again when told they changed.

import SwiftUI

struct FormItem: Identifiable {
    let id: Int
    var kind: Int32
    var group: String?
    var label: String
    var value: Int
    var maxValue: Int
    var disabled: Bool
    var names: [String]
}

func readItems(_ items: UnsafeMutablePointer<J3DMenuItem>, _ count: Int) -> [FormItem] {
    (0..<count).map { i in
        let item = items[i]
        var names: [String] = []
        if let list = item.apNames {
            for n in 0..<Int(item.numNames) { names.append(menuString(list[n])) }
        }
        return FormItem(id: i, kind: item.kind, group: item.pGroup.map { menuString($0) }, label: menuString(item.pLabel),
                        value: Int(item.value), maxValue: Int(item.maxValue), disabled: item.bDisabled != 0, names: names)
    }
}

final class FormModel: ObservableObject {
    @Published var items: [FormItem]
    private let cItems: UnsafeMutablePointer<J3DMenuItem>
    private let count: Int
    private let changed: J3DMenuFormChanged?

    init(cItems: UnsafeMutablePointer<J3DMenuItem>, count: Int, changed: J3DMenuFormChanged?) {
        self.cItems = cItems
        self.count = count
        self.changed = changed
        items = readItems(cItems, count)
    }

    var canReset: Bool { changed != nil }

    /// The values as they are on screen go to the C items.
    func writeBack() {
        for item in items { cItems[item.id].value = Int32(item.value) }
    }

    /// Tells the C side an item changed (-1: the defaults button) and takes over what it changed in return.
    func notify(_ index: Int) {
        guard let changed else { return }
        writeBack()
        if changed(cItems, Int32(count), Int32(index)) != 0 {
            items = readItems(cItems, count)
        }
    }

    func set(_ index: Int, _ value: Int, notifying: Bool = true) {
        guard items[index].value != value else { return }
        items[index].value = value
        if notifying { notify(index) }
    }
}

/// The items of a form in their titled groups, each drawn as its kind's control. The in-game forms and the launcher's
/// Options both use it.
struct FormItemsView: View {
    @ObservedObject var model: FormModel

    /// The items in their groups: a group starts at each item that names one.
    private var groups: [(title: String, items: [FormItem])] {
        var result: [(title: String, items: [FormItem])] = []
        for item in model.items {
            if let group = item.group {
                result.append((group, [item]))
            } else if result.isEmpty {
                result.append(("", [item]))
            } else {
                result[result.count - 1].items.append(item)
            }
        }
        return result
    }

    var body: some View {
        VStack(spacing: 12) {
            ForEach(Array(groups.enumerated()), id: \.offset) { _, group in
                MenuGroup(title: group.title) {
                    ForEach(group.items) { item in
                        row(item)
                            .disabled(item.disabled)
                            .opacity(item.disabled ? 0.4 : 1)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func row(_ item: FormItem) -> some View {
        let index = item.id
        switch item.kind {
        case J3DMENU_ITEM_TOGGLE:
            MenuToggle(title: item.label, isOn: Binding(get: { model.items[index].value != 0 },
                                                        set: { model.set(index, $0 ? 1 : 0) }))
        case J3DMENU_ITEM_STEPS:
            VStack(spacing: 6) {
                labelText(item.label)
                Text(item.names.indices.contains(item.value) ? item.names[item.value] : "")
                    .font(.system(size: 20, weight: .semibold).monospacedDigit())
                    .foregroundColor(Palette.text)
                    .frame(maxWidth: .infinity)
                MenuSlider(value: Binding(get: { Double(model.items[index].value) },
                                          set: { model.set(index, Int($0.rounded()), notifying: false) }),
                           range: 0...Double(max(item.names.count - 1, 1)), step: 1, onEnd: { model.notify(index) })
                if item.names.count > 1 {
                    captions([item.names.first ?? "", item.names.last ?? ""])
                }
            }
        case J3DMENU_ITEM_SLIDER:
            VStack(spacing: 6) {
                labelText(item.label)
                MenuSlider(value: Binding(get: { Double(model.items[index].value) },
                                          set: { model.set(index, Int($0.rounded()), notifying: false) }),
                           range: 0...Double(max(item.maxValue, 1)), onEnd: { model.notify(index) })
                captions(item.names)
            }
        case J3DMENU_ITEM_LIST:
            HStack(spacing: 12) {
                if !item.label.isEmpty {
                    Text(item.label)
                        .font(.system(size: 15))
                        .foregroundColor(Palette.text)
                        .frame(width: 150, alignment: .leading)
                }
                MenuListPicker(options: item.names, selection: Binding(get: { model.items[index].value },
                                                                       set: { model.set(index, $0) }))
            }
        default:
            VStack(alignment: .leading, spacing: 6) {
                labelText(item.label)
                MenuChoiceBar(options: item.names, selection: Binding(get: { model.items[index].value },
                                                                      set: { model.set(index, $0) }))
            }
        }
    }

    @ViewBuilder
    private func labelText(_ label: String) -> some View {
        if !label.isEmpty {
            Text(label)
                .font(.system(size: 15))
                .foregroundColor(Palette.text)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    @ViewBuilder
    private func captions(_ names: [String]) -> some View {
        if !names.isEmpty {
            HStack {
                ForEach(Array(names.enumerated()), id: \.offset) { index, name in
                    if index > 0 { Spacer() }
                    Text(name)
                }
            }
            .font(.system(size: 12))
            .foregroundColor(Palette.dimText)
        }
    }
}

private struct FormMenu: View {
    let title: String
    let okLabel: String
    let cancelLabel: String
    let defaultsLabel: String?
    @ObservedObject var model: FormModel
    let answer: (Int) -> Void

    var body: some View {
        MenuCard(title: title, width: 520) {
            VStack(spacing: 12) {
                FormItemsView(model: model)
                HStack(spacing: 8) {
                    if let defaultsLabel {
                        Button(defaultsLabel) { model.notify(-1) }
                            .buttonStyle(MenuButtonStyle())
                    }
                    Spacer()
                    Button(cancelLabel) { answer(0) }
                        .buttonStyle(MenuButtonStyle())
                        .keyboardShortcut(.cancelAction)
                    Button(okLabel) {
                        model.writeBack()
                        answer(1)
                    }
                    .buttonStyle(MenuButtonStyle(prominent: true))
                    .keyboardShortcut(.defaultAction)
                }
                .padding(.top, 4)
            }
            .padding(20)
        }
    }
}

@_cdecl("J3DMenuUI_Form")
public func J3DMenuUI_Form(_ gameWindow: UnsafeMutableRawPointer?, _ title: UnsafePointer<CChar>?, _ items: UnsafeMutablePointer<J3DMenuItem>?,
                           _ numItems: Int32, _ okLabel: UnsafePointer<CChar>?, _ cancelLabel: UnsafePointer<CChar>?,
                           _ defaultsLabel: UnsafePointer<CChar>?, _ changed: J3DMenuFormChanged?) -> Int32 {
    guard let items, numItems > 0 else { return 0 }
    let titleText = menuString(title)
    let ok = menuString(okLabel)
    let cancel = menuString(cancelLabel)
    let defaults = defaultsLabel.map { menuString($0) }
    return MainActor.assumeIsolated {
        let model = FormModel(cItems: items, count: Int(numItems), changed: changed)
        let result = runMenu(over: gameWindow) { answer in
            FormMenu(title: titleText, okLabel: ok, cancelLabel: cancel, defaultsLabel: defaults, model: model, answer: answer)
        }
        return result == 1 ? 1 : 0
    }
}
