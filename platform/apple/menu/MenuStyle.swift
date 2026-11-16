// The look the launcher and the in-game menus share (PORT-PLAN.md Phase 6, decision 20): dark panels, the gold of
// the game's logo for what is selected or the default, and the system font at sizes that read on a Retina display.
// SwiftUI as of macOS 13, the deployment target.

import SwiftUI

enum Palette {
    static let background = Color(red: 0.07, green: 0.055, blue: 0.045)
    static let panel = Color(red: 0.13, green: 0.105, blue: 0.085)
    static let panelHover = Color(red: 0.20, green: 0.16, blue: 0.12)
    static let rule = Color(red: 0.36, green: 0.27, blue: 0.16)
    static let gold = Color(red: 0.98, green: 0.76, blue: 0.16)
    static let amber = Color(red: 0.93, green: 0.47, blue: 0.10)
    static let text = Color(red: 0.96, green: 0.93, blue: 0.87)
    static let dimText = Color(red: 0.66, green: 0.60, blue: 0.52)
    static let goldFill = LinearGradient(colors: [gold, amber], startPoint: .top, endPoint: .bottom)
    static let goldRule = LinearGradient(colors: [amber, gold, amber], startPoint: .leading, endPoint: .trailing)
}

/// A menu button. `prominent` is the default action; `large` is the launcher's full-width row.
struct MenuButtonStyle: ButtonStyle {
    var prominent = false
    var large = false
    var fullWidth = false

    func makeBody(configuration: Configuration) -> some View {
        MenuButtonBody(configuration: configuration, prominent: prominent, large: large, fullWidth: fullWidth || large)
    }
}

private struct MenuButtonBody: View {
    let configuration: ButtonStyle.Configuration
    let prominent: Bool
    let large: Bool
    let fullWidth: Bool
    @State private var hovering = false
    @Environment(\.isEnabled) private var enabled

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: large ? 7 : 6, style: .continuous)
        configuration.label
            .font(.system(size: large ? 17 : 14, weight: .semibold))
            .foregroundColor(prominent ? Color.black.opacity(0.88) : Palette.text)
            .padding(.horizontal, 16)
            .frame(maxWidth: fullWidth ? .infinity : nil, minHeight: large ? 44 : 32)
            .background {
                if prominent {
                    shape.fill(Palette.goldFill)
                } else {
                    shape.fill(hovering ? Palette.panelHover : Palette.panel)
                }
            }
            .overlay {
                shape.strokeBorder(prominent ? Color.white.opacity(0.35) : (hovering ? Palette.gold.opacity(0.8) : Palette.rule),
                                   lineWidth: 1)
            }
            .brightness(configuration.isPressed ? -0.12 : (prominent && hovering ? 0.06 : 0))
            .opacity(enabled ? 1 : 0.4)
            .contentShape(shape)
            .onHover { hovering = $0 }
    }
}

/// The frame every in-game menu sits in: a titled card with the gold rule under the title.
struct MenuCard<Content: View>: View {
    let title: String
    var width: CGFloat = 420
    @ViewBuilder var content: Content

    var body: some View {
        VStack(spacing: 0) {
            Text(title.uppercased())
                .font(.system(size: 13, weight: .bold))
                .tracking(1.4)
                .foregroundColor(Palette.gold)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 20)
                .padding(.vertical, 13)
            Rectangle().fill(Palette.goldRule).frame(height: 2)
            content
        }
        .frame(width: width)
        .background(Palette.background)
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 10, style: .continuous).strokeBorder(Palette.rule, lineWidth: 1)
        }
        .shadow(color: .black.opacity(0.6), radius: 24, y: 8)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(title)
    }
}
