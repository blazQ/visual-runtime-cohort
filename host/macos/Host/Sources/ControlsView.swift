import SwiftUI

struct ControlsView: View {
    let backendName: String
    @Binding var backgroundColor: Color
    let onZoomIn: () -> Void
    let onZoomOut: () -> Void

    var body: some View {
        ZStack {
            StatusView(backendName: backendName)
                .padding(12)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)

            ColorPicker("Background", selection: $backgroundColor, supportsOpacity: true)
                .padding(10)
                .compatGlassEffect(in: .capsule)
                .accessibilityLabel("Background color")
                .padding(16)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)

            ZoomControls(
                onZoomIn: onZoomIn,
                onZoomOut: onZoomOut
            )
            .padding(16)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
        }
    }
}

#Preview("Controls") {
    ZStack {
        Rectangle()
            .fill(.black.gradient)

        ControlsView(
            backendName: "preview",
            backgroundColor: .constant(.black),
            onZoomIn: {},
            onZoomOut: {}
        )
    }
    .frame(width: 800, height: 600)
}
