// Pages of figures beside the game's IQ picture (J3DMenuUI_Stats, j3dmenu_ui.h): the statistics menu, one page a
// level and one for the whole game, and the summary at the end of a level. The picture is the game's own art, read
// from the player's data, with the points drawn on it by the game in its own figures (jonesConfig.c).

import AppKit
import SwiftUI

private struct StatsPage {
    let title: String
    let iqPoints: Int
    let rows: [(label: String, value: String)]
    let picture: NSImage?
}

private struct StatsMenu: View {
    let title: String
    let pages: [StatsPage]
    let closeLabel: String
    let answer: (Int) -> Void
    @State var page: Int

    private var current: StatsPage { pages[min(max(page, 0), pages.count - 1)] }

    var body: some View {
        MenuCard(title: title, width: 560) {
            VStack(spacing: 16) {
                HStack(alignment: .top, spacing: 20) {
                    VStack(spacing: 10) {
                        pictureView
                        if pages.count > 1 || !current.title.isEmpty {
                            pager
                        }
                    }
                    .frame(width: 200)
                    VStack(spacing: 0) {
                        ForEach(Array(current.rows.enumerated()), id: \.offset) { index, row in
                            if index > 0 {
                                Rectangle().fill(Palette.rule.opacity(0.5)).frame(height: 1)
                            }
                            HStack {
                                Text(row.label)
                                    .font(.system(size: 15))
                                    .foregroundColor(Palette.text)
                                Spacer(minLength: 12)
                                Text(row.value)
                                    .font(.system(size: 17, weight: .semibold).monospacedDigit())
                                    .foregroundColor(Palette.gold)
                            }
                            .frame(height: 40)
                            .accessibilityElement(children: .combine)
                        }
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 4)
                    .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                    .overlay {
                        RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Palette.rule.opacity(0.7), lineWidth: 1)
                    }
                }
                HStack {
                    Spacer()
                    Button(closeLabel) { answer(1) }
                        .buttonStyle(MenuButtonStyle(prominent: true))
                        .keyboardShortcut(.defaultAction)
                    Button("") { answer(1) }
                        .keyboardShortcut(.cancelAction)
                        .opacity(0)
                        .frame(width: 0, height: 0)
                        .accessibilityHidden(true)
                }
            }
            .padding(20)
        }
    }

    // The picture at the width of its column, at a whole multiple of its size where that fits, without smoothing.
    private var pictureView: some View {
        let width = 200.0
        let picture = current.picture
        let size = picture?.size ?? .zero
        let height = size.width > 0 ? size.height * (width - 16) / size.width + 16 : 120 // the picture keeps its shape inside the 8-point margin
        return Group {
            if let picture {
                Image(nsImage: picture)
                    .resizable()
                    .interpolation(.none)
                    .padding(8)
                    .frame(width: width, height: height)
                    .background(Palette.parchment, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            } else {
                VStack(spacing: 2) {
                    Text("IQ")
                        .font(.system(size: 13, weight: .bold))
                        .tracking(1.2)
                        .foregroundColor(Palette.dimText)
                    Text("\(current.iqPoints)")
                        .font(.system(size: 44, weight: .heavy, design: .serif).monospacedDigit())
                        .foregroundColor(Palette.gold)
                }
                .frame(width: width, height: height)
                .background(Palette.panel, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            }
        }
        .frame(width: width, height: height)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Indy Quotient \(current.iqPoints)")
    }

    private var pager: some View {
        HStack(spacing: 6) {
            if pages.count > 1 {
                arrow("chevron.left", to: page - 1)
                    .keyboardShortcut(.leftArrow, modifiers: [])
            }
            Text(current.title)
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(Palette.text)
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .minimumScaleFactor(0.8)
                .frame(maxWidth: .infinity, minHeight: 32)
            if pages.count > 1 {
                arrow("chevron.right", to: page + 1)
                    .keyboardShortcut(.rightArrow, modifiers: [])
            }
        }
    }

    private func arrow(_ symbol: String, to target: Int) -> some View {
        Button {
            page = target
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .bold))
                .frame(width: 12)
        }
        .buttonStyle(MenuButtonStyle())
        .disabled(target < 0 || target >= pages.count)
        .accessibilityLabel(symbol == "chevron.left" ? "Previous" : "Next")
    }
}

private func makeImage(_ pixels: UnsafePointer<UInt8>?, _ width: Int, _ height: Int) -> NSImage? {
    guard let pixels, width > 0, height > 0,
          let provider = CGDataProvider(data: Data(bytes: pixels, count: width * height * 4) as CFData),
          let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                              space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                              provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return nil }
    return NSImage(cgImage: image, size: NSSize(width: width, height: height))
}

@_cdecl("J3DMenuUI_Stats")
public func J3DMenuUI_Stats(_ gameWindow: UnsafeMutableRawPointer?, _ statsPointer: UnsafePointer<J3DMenuStats>?) {
    guard let statsPointer else { return }
    let stats = statsPointer.pointee
    var pages: [StatsPage] = []
    if let list = stats.pPages {
        for i in 0..<Int(stats.numPages) {
            var page = list[i]
            var rows: [(label: String, value: String)] = []
            let numRows = min(Int(page.numRows), Int(J3DMENU_STATS_MAXROWS))
            withUnsafePointer(to: &page.apLabels) { labels in
                labels.withMemoryRebound(to: UnsafePointer<CChar>?.self, capacity: numRows) { labels in
                    withUnsafePointer(to: &page.aaValues) { values in
                        values.withMemoryRebound(to: CChar.self, capacity: numRows * 32) { values in
                            for r in 0..<numRows {
                                rows.append((menuString(labels[r]), menuString(values + r * 32).trimmingCharacters(in: .whitespaces)))
                            }
                        }
                    }
                }
            }
            pages.append(StatsPage(title: menuString(page.pTitle), iqPoints: Int(page.iqPoints), rows: rows,
                                   picture: makeImage(page.pPicture, Int(page.pictureWidth), Int(page.pictureHeight))))
        }
    }
    guard !pages.isEmpty else { return }

    let title = menuString(stats.pTitle)
    let closeLabel = menuString(stats.pCloseLabel)
    let startPage = min(max(Int(stats.startPage), 0), pages.count - 1)
    MainActor.assumeIsolated {
        _ = runMenu(over: gameWindow) { answer in
            StatsMenu(title: title, pages: pages, closeLabel: closeLabel, answer: answer, page: startPage)
        }
    }
}
