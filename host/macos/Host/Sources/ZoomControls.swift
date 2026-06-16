import SwiftUI

struct ZoomControls: View {
    let onZoomIn: () -> Void
    let onZoomOut: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            zoomButton("Zoom Out", systemImage: "minus", action: onZoomOut)
            zoomButton("Zoom In", systemImage: "plus", action: onZoomIn)
        }
        .padding(10)
        .compatGlassEffect(in: .capsule)
    }

    private func zoomButton(
        _ title: LocalizedStringKey,
        systemImage: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .labelStyle(.iconOnly)
                .frame(width: 24, height: 24)
                .contentShape(Rectangle())
        }
        .compatGlassButtonStyle()
        .controlSize(.large)
        .buttonBorderShape(.circle)
    }
}

#Preview("Zoom Controls") {
    ZStack {
        Rectangle()
            .fill(.black.gradient)

        ZoomControls(
            onZoomIn: {},
            onZoomOut: {}
        )
    }
    .frame(width: 320, height: 160)
}
