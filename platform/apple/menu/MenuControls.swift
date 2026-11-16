// The controls the settings menus are made of, drawn in the menus' colours so that on and off, and what is
// selected, read clearly on the dark panels (PORT-PLAN.md Phase 6). Each has the standard control as its
// accessibility representation.

import SwiftUI

/// A titled group of settings.
struct MenuGroup<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if !title.isEmpty {
                Text(title.uppercased())
                    .font(.system(size: 12, weight: .bold))
                    .tracking(1.2)
                    .foregroundColor(Palette.gold)
            }
            content
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Palette.rule.opacity(0.7), lineWidth: 1)
        }
    }
}

/// On or off.
struct MenuSwitchStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack {
            configuration.label
            Text(configuration.isOn ? "On" : "Off")
                .font(.system(size: 13, weight: .medium))
                .foregroundColor(configuration.isOn ? Palette.gold : Palette.dimText)
            ZStack(alignment: configuration.isOn ? .trailing : .leading) {
                Capsule().fill(configuration.isOn ? Palette.amber : Palette.background)
                Capsule().strokeBorder(configuration.isOn ? Palette.gold : Palette.rule, lineWidth: 1)
                Circle()
                    .fill(configuration.isOn ? Color.white : Palette.dimText)
                    .padding(3)
            }
            .frame(width: 42, height: 24)
        }
        .contentShape(Rectangle())
        .onTapGesture { configuration.isOn.toggle() }
        .accessibilityRepresentation { Toggle(isOn: configuration.$isOn) { configuration.label } }
    }
}

struct MenuToggle: View {
    let title: String
    @Binding var isOn: Bool

    var body: some View {
        Toggle(isOn: $isOn) {
            Text(title)
                .font(.system(size: 15))
                .foregroundColor(Palette.text)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .toggleStyle(MenuSwitchStyle())
    }
}

/// A slider. `step` snaps it and draws a mark at each stop; `onEnd` is called when the player lets go.
struct MenuSlider: View {
    @Binding var value: Double
    var range: ClosedRange<Double> = 0...1
    var step: Double? = nil
    var onEnd: () -> Void = {}

    var body: some View {
        GeometryReader { geo in
            let thumb = 20.0
            let span = max(geo.size.width - thumb, 1)
            let extent = max(range.upperBound - range.lowerBound, 0.0001)
            let fraction = min(max((value - range.lowerBound) / extent, 0), 1)
            ZStack(alignment: .leading) {
                Capsule().fill(Palette.background)
                    .overlay { Capsule().strokeBorder(Palette.rule, lineWidth: 1) }
                    .frame(height: 8)
                Capsule().fill(LinearGradient(colors: [Palette.amber, Palette.gold], startPoint: .leading, endPoint: .trailing))
                    .frame(width: fraction * span + thumb / 2, height: 8)
                if let step, step > 0 {
                    let count = max(Int((extent / step).rounded()), 1)
                    ForEach(0...count, id: \.self) { i in
                        Circle().fill(Palette.dimText)
                            .frame(width: 3, height: 3)
                            .offset(x: Double(i) / Double(count) * span + thumb / 2 - 1.5, y: 11)
                    }
                }
                Circle().fill(Color.white)
                    .overlay { Circle().strokeBorder(Palette.amber, lineWidth: 2) }
                    .frame(width: thumb, height: thumb)
                    .offset(x: fraction * span)
            }
            .frame(height: geo.size.height)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { drag in
                    let f = min(max((drag.location.x - thumb / 2) / span, 0), 1)
                    var v = range.lowerBound + f * extent
                    if let step, step > 0 { v = (v / step).rounded() * step }
                    value = min(max(v, range.lowerBound), range.upperBound)
                }
                .onEnded { _ in onEnd() })
        }
        .frame(height: 26)
        .accessibilityRepresentation { Slider(value: $value, in: range) }
    }
}

/// One of a few names, side by side.
struct MenuChoiceBar: View {
    let options: [String]
    @Binding var selection: Int

    var body: some View {
        HStack(spacing: 6) {
            ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                let selected = index == selection
                Button {
                    selection = index
                } label: {
                    Text(option)
                        .font(.system(size: 14, weight: selected ? .semibold : .regular))
                        .foregroundColor(selected ? Color.black.opacity(0.88) : Palette.text)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                        .padding(.horizontal, 6)
                        .frame(maxWidth: .infinity, minHeight: 30)
                        .background {
                            let shape = RoundedRectangle(cornerRadius: 6, style: .continuous)
                            if selected {
                                shape.fill(Palette.gold)
                            } else {
                                shape.fill(Palette.background)
                                shape.strokeBorder(Palette.rule, lineWidth: 1)
                            }
                        }
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? .isSelected : [])
            }
        }
    }
}

/// One of many names, from a list that opens under the field.
struct MenuListPicker: View {
    let options: [String]
    @Binding var selection: Int

    var body: some View {
        Menu {
            ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                Button(option) { selection = index }
            }
        } label: {
            HStack {
                Text(options.indices.contains(selection) ? options[selection] : "")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(Palette.text)
                    .lineLimit(1)
                Spacer()
                Image(systemName: "chevron.up.chevron.down")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(Palette.gold)
            }
            .padding(.horizontal, 10)
            .frame(maxWidth: .infinity, minHeight: 30)
            .background(Palette.background, in: RoundedRectangle(cornerRadius: 6, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 6, style: .continuous).strokeBorder(Palette.rule, lineWidth: 1)
            }
            .contentShape(Rectangle())
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
    }
}
