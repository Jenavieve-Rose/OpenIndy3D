// Indy's trading post between levels (J3DMenuUI_Store, j3dmenu_ui.h): what Indy carries, what the store sells and the
// cart, with the game's item icons. The rules (prices, what can be afforded, the confirmation) are the C side's: each
// button is an action it carries out, and the lists are read back from it after each one.

import AppKit
import SwiftUI

private struct StoreRow: Identifiable {
    let id: Int
    let item: Int
    let name: String
    let value: Int
}

private func readRows(_ rows: UnsafePointer<J3DMenuStoreRowUI>?, _ count: Int32) -> [StoreRow] {
    guard let rows else { return [] }
    return (0..<Int(count)).map { i in
        StoreRow(id: i, item: Int(rows[i].item), name: menuString(rows[i].pName), value: Int(rows[i].value))
    }
}

private final class StoreModel: ObservableObject {
    @Published var owned: [StoreRow] = []
    @Published var available: [StoreRow] = []
    @Published var cart: [StoreRow] = []
    @Published var total = 0
    @Published var balance = 0
    @Published var selectedAvailable: Int?
    @Published var selectedCart: Int?
    private let action: J3DMenuStoreAction

    init(action: @escaping J3DMenuStoreAction) {
        self.action = action
        _ = perform(J3DMENU_UI_STORE_READ, 0)
    }

    /// Carries out an action on the C side and reads the store back. Returns true when the store is to close.
    func perform(_ code: Int32, _ index: Int) -> Bool {
        var state = J3DMenuStoreState()
        let close = action(code, Int32(index), &state) != 0
        owned = readRows(state.pOwned, state.numOwned)
        available = readRows(state.pAvailable, state.numAvailable)
        cart = readRows(state.pCart, state.numCart)
        total = Int(state.total)
        balance = Int(state.balance)
        if let s = selectedCart, s >= cart.count { selectedCart = cart.isEmpty ? nil : cart.count - 1 }
        if let s = selectedAvailable, s >= available.count { selectedAvailable = nil }
        return close
    }
}

private struct StoreMenu: View {
    let labels: J3DMenuStoreUI
    let icons: [Int: NSImage]
    @ObservedObject var model: StoreModel
    let answer: (Int) -> Void

    var body: some View {
        MenuCard(title: menuString(labels.pTitle), width: 860) {
            VStack(spacing: 14) {
                HStack(alignment: .top, spacing: 12) {
                    list(menuString(labels.pOwnedTitle), menuString(labels.pCountLabel), model.owned, selection: nil, onDouble: nil)
                    list(menuString(labels.pAvailableTitle), menuString(labels.pCostLabel), model.available,
                         selection: $model.selectedAvailable, onDouble: { add($0) })
                    VStack(spacing: 10) {
                        list(menuString(labels.pCartTitle), menuString(labels.pCostLabel), model.cart,
                             selection: $model.selectedCart, onDouble: { remove($0) }, height: 220)
                        figures
                    }
                }
                HStack(spacing: 8) {
                    Button(menuString(labels.pAddLabel)) { if let s = model.selectedAvailable { add(s) } }
                        .buttonStyle(MenuButtonStyle())
                        .disabled(model.selectedAvailable == nil)
                    Button(menuString(labels.pReturnLabel)) { if let s = model.selectedCart { remove(s) } }
                        .buttonStyle(MenuButtonStyle())
                        .disabled(model.selectedCart == nil)
                    Button(menuString(labels.pClearLabel)) { perform(J3DMENU_UI_STORE_CLEAR, 0) }
                        .buttonStyle(MenuButtonStyle())
                        .disabled(model.cart.isEmpty)
                    Spacer()
                    Button(menuString(labels.pDoneLabel)) { perform(J3DMENU_UI_STORE_DONE, 0) }
                        .buttonStyle(MenuButtonStyle(prominent: true))
                        .keyboardShortcut(.defaultAction)
                    // Escape is the dialog's IDCANCEL, which did what Done does
                    Button("") { perform(J3DMENU_UI_STORE_DONE, 0) }
                        .keyboardShortcut(.cancelAction)
                        .opacity(0)
                        .frame(width: 0, height: 0)
                        .accessibilityHidden(true)
                }
            }
            .padding(20)
        }
    }

    private func add(_ index: Int) { perform(J3DMENU_UI_STORE_ADD, index) }
    private func remove(_ index: Int) { perform(J3DMENU_UI_STORE_RETURN, index) }

    private func perform(_ code: Int32, _ index: Int) {
        if model.perform(code, index) { answer(1) }
    }

    private var figures: some View {
        VStack(spacing: 6) {
            figure(menuString(labels.pTotalLabel), model.total)
            figure(menuString(labels.pBalanceLabel), model.balance)
        }
        .padding(12)
        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay { RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Palette.rule.opacity(0.7), lineWidth: 1) }
    }

    private func figure(_ label: String, _ value: Int) -> some View {
        HStack {
            Text(label).font(.system(size: 15)).foregroundColor(Palette.text)
            Spacer()
            Text(verbatim: "\(value)")
                .font(.system(size: 17, weight: .semibold).monospacedDigit())
                .foregroundColor(value < 0 ? Palette.amber : Palette.gold)
        }
        .accessibilityElement(children: .combine)
    }

    private func list(_ title: String, _ column: String, _ rows: [StoreRow], selection: Binding<Int?>?,
                      onDouble: ((Int) -> Void)?, height: CGFloat = 330) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title.uppercased())
                    .font(.system(size: 12, weight: .bold))
                    .tracking(1.1)
                    .foregroundColor(Palette.gold)
                    .lineLimit(1)
                Spacer()
                Text(column)
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(Palette.dimText)
            }
            ScrollView {
                LazyVStack(spacing: 2) {
                    ForEach(rows) { row in
                        rowView(row, selected: selection?.wrappedValue == row.id)
                            .contentShape(Rectangle())
                            .onTapGesture(count: 2) {
                                selection?.wrappedValue = row.id
                                onDouble?(row.id)
                            }
                            .simultaneousGesture(TapGesture().onEnded { selection?.wrappedValue = row.id })
                            .accessibilityAddTraits(selection?.wrappedValue == row.id ? .isSelected : [])
                    }
                }
                .padding(4)
            }
            .frame(height: height)
            .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Palette.rule.opacity(0.7), lineWidth: 1) }
        }
        .frame(maxWidth: .infinity)
    }

    private func rowView(_ row: StoreRow, selected: Bool) -> some View {
        HStack(spacing: 8) {
            if let icon = icons[row.item] {
                Image(nsImage: icon)
                    .interpolation(.none)
                    .resizable()
                    .frame(width: 24, height: 24)
            }
            Text(row.name)
                .font(.system(size: 14))
                .lineLimit(1)
                .minimumScaleFactor(0.8)
            Spacer(minLength: 6)
            Text(verbatim: "\(row.value)")
                .font(.system(size: 14, weight: .semibold).monospacedDigit())
        }
        .foregroundColor(selected ? Color.black.opacity(0.88) : Palette.text)
        .padding(.horizontal, 8)
        .frame(minHeight: 32)
        .background {
            if selected {
                RoundedRectangle(cornerRadius: 5, style: .continuous).fill(Palette.gold)
            }
        }
        .accessibilityElement(children: .combine)
    }
}

@_cdecl("J3DMenuUI_Store")
public func J3DMenuUI_Store(_ gameWindow: UnsafeMutableRawPointer?, _ uiPointer: UnsafePointer<J3DMenuStoreUI>?,
                            _ action: J3DMenuStoreAction?) {
    guard let uiPointer, let action else { return }
    let ui = uiPointer.pointee
    var icons: [Int: NSImage] = [:]
    let size = Int(ui.iconSize)
    if let pixels = ui.pIcons, size > 0 {
        for item in 0..<Int(ui.numIcons) {
            let start = pixels + item * size * size * 4
            guard let provider = CGDataProvider(data: Data(bytes: start, count: size * size * 4) as CFData),
                  let image = CGImage(width: size, height: size, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: size * 4,
                                      space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                                      provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { continue }
            icons[item] = NSImage(cgImage: image, size: NSSize(width: size, height: size))
        }
    }
    MainActor.assumeIsolated {
        let model = StoreModel(action: action)
        _ = runMenu(over: gameWindow) { answer in
            StoreMenu(labels: ui, icons: icons, model: model, answer: answer)
        }
    }
}
